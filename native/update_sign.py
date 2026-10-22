"""Подпись выпусков CursorPad (RSA-3072, PKCS#1 v1.5, SHA-256). Без зависимостей.

Зачем: version.txt и CursorPad.bin приходят с одних и тех же зеркал (jsDelivr,
raw.githubusercontent.com, github.com, api.github.com). Кто подменит одно,
подменит и другое — отпечаток sha256 в version.txt от этого не спасает.
Подпись делается закрытым ключом, который есть только у того, кто выпускает;
программа сверяет её открытым ключом, вшитым в exe (update_key.h).

    python native/update_sign.py --make-key   # один раз: ключ + update_key.h
    python native/update_sign.py --sign public/version.txt     # подписать собранный выпуск
    python native/update_sign.py --verify public/version.txt   # проверить, без закрытого ключа

Закрытый ключ НЕ хранится в репозитории. Где он:
  CURSORPAD_UPDATE_KEY_PEM — сам ключ (PEM) в переменной окружения, или
  CURSORPAD_UPDATE_KEY     — путь к файлу, иначе
  %LOCALAPPDATA%\\CursorPad-release\\update-signing-key.pem
Подробнее — RELEASE_SIGNING.md.
"""
import base64, hashlib, os, pathlib, re, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
HEADER = HERE / "update_key.h"
BITS = 3072
# DigestInfo для SHA-256 (RFC 8017, 9.2)
_SHA256_INFO = bytes.fromhex("3031300d060960864801650304020105000420")
SIG_PREFIX = "== 0 signature-rsa-sha256 "  # старые версии видят «версию 0» и скрывают строку


def key_path():
    p = os.environ.get("CURSORPAD_UPDATE_KEY")
    if p:
        return pathlib.Path(p)
    base = os.environ.get("LOCALAPPDATA") or str(pathlib.Path.home() / ".local" / "share")
    return pathlib.Path(base) / "CursorPad-release" / "update-signing-key.pem"


def _der(b, i):
    tag, ln = b[i], b[i + 1]
    i += 2
    if ln & 0x80:
        k = ln & 0x7F
        ln = int.from_bytes(b[i:i + k], "big")
        i += k
    return tag, b[i:i + ln], i + ln


def _seq(b):
    out, i = [], 0
    while i < len(b):
        tag, val, i = _der(b, i)
        out.append((tag, val))
    return out


def load_key(required=True):
    """(n, e, d) закрытого ключа; нет ключа — SystemExit (или None, если не required)."""
    pem = os.environ.get("CURSORPAD_UPDATE_KEY_PEM")
    if not pem:
        path = key_path()
        if not path.exists():
            if not required:
                return None
            raise SystemExit(f"Нет ключа подписи выпусков: {path}\n"
                             "Выпуск без подписи программа не поставит. См. native/RELEASE_SIGNING.md")
        pem = path.read_text(encoding="ascii")
    body = "".join(l for l in pem.splitlines() if l and not l.startswith("-----"))
    _, outer, _ = _der(base64.b64decode(body), 0)
    items = _seq(outer)
    if "BEGIN RSA PRIVATE KEY" in pem:  # PKCS#1
        rsa = items
    else:  # PKCS#8: версия, алгоритм, OCTET STRING с RSAPrivateKey
        _, inner, _ = _der(items[2][1], 0)
        rsa = _seq(inner)
    n, e, d = (int.from_bytes(rsa[k][1], "big") for k in (1, 2, 3))
    return n, e, d


def _em(msg, k):
    t = _SHA256_INFO + hashlib.sha256(msg).digest()
    return b"\x00\x01" + b"\xff" * (k - len(t) - 3) + b"\x00" + t


def sign(msg, key):
    n, e, d = key
    k = (n.bit_length() + 7) // 8
    s = pow(int.from_bytes(_em(msg, k), "big"), d, n).to_bytes(k, "big")
    if not verify(msg, s, n, e):
        raise SystemExit("Подпись не проверилась своим же ключом — ключ испорчен")
    return s


def verify(msg, sig, n, e):
    k = (n.bit_length() + 7) // 8
    return len(sig) == k and pow(int.from_bytes(sig, "big"), e, n).to_bytes(k, "big") == _em(msg, k)


def release_message(ver_num, ver_text, sha):
    """Что подписывается: номер, строка версии и отпечаток CursorPad.bin (update.c собирает так же)."""
    return f"CursorPad-update\n{ver_num}\n{ver_text}\n{sha}\n".encode("ascii")


def signature_line(ver_num, ver_text, sha, key):
    return SIG_PREFIX + base64.b64encode(sign(release_message(ver_num, ver_text, sha), key)).decode("ascii")


def public_blob(n, e):
    """PUBLICKEYBLOB для CryptImportKey: BLOBHEADER + RSAPUBKEY + модуль младшими байтами вперёд."""
    bits = n.bit_length()
    head = bytes([0x06, 0x02, 0x00, 0x00]) + (0x2400).to_bytes(4, "little")  # PUBLICKEYBLOB, CALG_RSA_SIGN
    rsa = b"RSA1" + bits.to_bytes(4, "little") + e.to_bytes(4, "little")
    return head + rsa + n.to_bytes((bits + 7) // 8, "little")


def header_text(n, e):
    blob = public_blob(n, e)
    rows = [", ".join(f"0x{b:02x}" for b in blob[i:i + 16]) for i in range(0, len(blob), 16)]
    finger = hashlib.sha256(n.to_bytes((n.bit_length() + 7) // 8, "big")).hexdigest()
    return ("/* Сгенерировано native/update_sign.py --make-key, руками не править.\n"
            f"   Открытый ключ выпусков CursorPad: RSA-{n.bit_length()}, отпечаток модуля sha256 {finger}.\n"
            "   Закрытый ключ — только у того, кто выпускает (RELEASE_SIGNING.md). */\n"
            "static const BYTE kUpdPubKey[] = {\n  " + ",\n  ".join(rows) + "};\n")


def check_header(key):
    """update_key.h должен содержать открытую часть именно этого ключа."""
    n, e, _ = key
    if not HEADER.exists() or HEADER.read_text(encoding="utf-8") != header_text(n, e):
        raise SystemExit("update_key.h не от этого ключа подписи: программа не примет такой выпуск.\n"
                         "Сгенерируйте заново: python native/update_sign.py --make-key")


def public_key():
    """(n, e) из update_key.h — то, чем сверяет программа."""
    blob = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", HEADER.read_text(encoding="utf-8")))
    bits = int.from_bytes(blob[12:16], "little")
    return int.from_bytes(blob[20:20 + (bits + 7) // 8], "little"), int.from_bytes(blob[16:20], "little")


def _release_lines(path):
    """Строки готового version.txt и проверка, что sha256 — от CursorPad.bin рядом."""
    lines = path.read_text(encoding="utf-8").replace("\r\n", "\n").split("\n")
    sha = lines[2][7:] if len(lines) > 3 and lines[2].startswith("sha256 ") else ""
    if not re.fullmatch(r"[0-9a-f]{64}", sha):
        raise SystemExit(f"{path}: 3-я строка — не «sha256 <отпечаток>»")
    exe = path.parent / "CursorPad.bin"
    if exe.exists() and hashlib.sha256(exe.read_bytes()).hexdigest() != sha:
        raise SystemExit(f"{path}: sha256 не от {exe.name} рядом")
    return lines, sha


def sign_file(path):
    """Вставить (или заменить) 4-ю строку — подпись — в собранный version.txt."""
    key = load_key()
    check_header(key)
    lines, sha = _release_lines(path)
    if lines[3].startswith(SIG_PREFIX):
        del lines[3]
    lines.insert(3, signature_line(lines[0], lines[1], sha, key))
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def verify_file(path):
    lines, sha = _release_lines(path)
    if not lines[3].startswith(SIG_PREFIX):
        return False
    n, e = public_key()
    return verify(release_message(lines[0], lines[1], sha), base64.b64decode(lines[3][len(SIG_PREFIX):]), n, e)


def make_key():
    path = key_path()
    if path.exists():
        print("Ключ уже есть:", path)
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", f"rsa_keygen_bits:{BITS}",
                        "-out", str(path)], check=True)
        print("Создан ключ:", path, "— сделайте его резервную копию (RELEASE_SIGNING.md)")
    n, e, _ = key = load_key()
    HEADER.write_text(header_text(n, e), encoding="utf-8", newline="\n")
    probe = release_message("1", "1", "0" * 64)
    assert verify(probe, sign(probe, key), n, e)
    print("Записан", HEADER.name)


if __name__ == "__main__":
    if sys.argv[1:] == ["--make-key"]:
        make_key()
    elif sys.argv[1:] == ["--check"]:  # ключ есть и от него update_key.h
        check_header(load_key())
        print("Ключ подписи выпусков на месте")
    elif len(sys.argv) == 3 and sys.argv[1] == "--sign":
        sign_file(pathlib.Path(sys.argv[2]))
        print("Подписан", sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "--verify":
        if not verify_file(pathlib.Path(sys.argv[2])):
            sys.exit(f"{sys.argv[2]}: нет верной подписи ключом из update_key.h — программа такой выпуск не поставит")
        print("Подпись верна:", sys.argv[2])
    else:
        sys.exit(__doc__)
