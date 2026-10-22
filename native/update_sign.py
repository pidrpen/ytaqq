"""Подпись выпусков для старых CursorPad (RSA-3072, PKCS#1 v1.5, SHA-256). Без зависимостей.

Версии 2026.10.21.2–2026.10.22.1 ставят обновление, только если version.txt
подписан этим ключом (открытый ключ вшит в них). С 2026.10.22.2 подпись не
проверяется и для сборки не нужна. package_release.py подписывает выпуск,
только если ключ есть на этом ПК: так застрявшие на старых версиях тоже
обновятся. Нет ключа — выпуск выходит без подписи (RELEASE_SIGNING.md).

Закрытый ключ НЕ хранится в репозитории. Где он:
  CURSORPAD_UPDATE_KEY_PEM — сам ключ (PEM) в переменной окружения, или
  CURSORPAD_UPDATE_KEY     — путь к файлу, иначе
  %LOCALAPPDATA%\\CursorPad-release\\update-signing-key.pem
"""
import base64, hashlib, os, pathlib

# sha256 модуля открытого ключа, вшитого в 2026.10.21.2–2026.10.22.1:
# подпись другим ключом они всё равно не примут
OLD_KEY_SHA256 = "0db9453b126a36267ae97063156e3a164efb812043522879bdbd274a2627fcf3"
# DigestInfo для SHA-256 (RFC 8017, 9.2)
_SHA256_INFO = bytes.fromhex("3031300d060960864801650304020105000420")
SIG_PREFIX = "== 0 signature-rsa-sha256 "  # программа видит «версию 0» и скрывает строку


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


def load_key():
    """(n, e, d) закрытого ключа или None, если ключа нет."""
    pem = os.environ.get("CURSORPAD_UPDATE_KEY_PEM")
    if not pem:
        path = key_path()
        if not path.exists():
            return None
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
    if hashlib.sha256(n.to_bytes((n.bit_length() + 7) // 8, "big")).hexdigest() != OLD_KEY_SHA256:
        raise SystemExit("Ключ подписи не тот, что вшит в 2026.10.21.2–2026.10.22.1: они такую подпись не примут")
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
    """Что подписывается: номер, строка версии и отпечаток CursorPad.bin (так сверяли старые update.c)."""
    return f"CursorPad-update\n{ver_num}\n{ver_text}\n{sha}\n".encode("ascii")


def signature_line(ver_num, ver_text, sha, key):
    return SIG_PREFIX + base64.b64encode(sign(release_message(ver_num, ver_text, sha), key)).decode("ascii")
