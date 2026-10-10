"""Пакет Tesseract для CursorPad → public/tess/ (его скачивает «Проверить обновления»).

    python native/make_tess_pack.py <папка tesseract> [--share <папка>]

<папка tesseract> — tesseract.exe, его DLL и tessdata/ (rus, eng). Берётся,
например, из VedomostiOCR.zip (pidrpen/cursor, dist/) — там уже отобраны
только нужные DLL — плюс eng.traineddata из tessdata_fast.

Пишет public/tess/pack.txt и каждый файл как public/tess/<имя>.bin: jsDelivr
не отдаёт .exe и .dll, а .bin отдаёт. Номер пакета — сегодняшняя дата + 01,
02…: на один больше прежнего, если пакет в тот же день уже выпускали.
Файлы, которые не поменялись, на ПК не перекачиваются — сверка по sha256.

--share <папка> — то же самое обычными файлами в <папка>\\CursorPad-PLM\\tesseract
для ПК без интернета (CursorPad берёт его оттуда сам).
"""
import datetime, hashlib, pathlib, shutil, sys

HERE = pathlib.Path(__file__).resolve().parent
PUB = HERE.parent / "public" / "tess"
SKIP = {"pack.txt"}
MAX = 24 << 20  # tess.c: TESS_FILE_MAX


def files_of(src):
    out = []
    for p in sorted(src.rglob("*")):
        rel = p.relative_to(src).as_posix()
        if p.is_dir() or rel in SKIP or rel.startswith("ocr-in-"):
            continue
        if rel.startswith("tessdata/") and not rel.endswith(".traineddata"):
            continue  # configs и прочее для «Выделить и прочитать» не нужны
        if p.stat().st_size > MAX:
            sys.exit(f"{rel}: больше 24 МБ — CursorPad такой файл не примет")
        out.append((rel, p))
    names = {r for r, _ in out}
    for need in ("tesseract.exe", "tessdata/rus.traineddata"):
        if need not in names:
            sys.exit(f"в папке нет {need}")
    return out


def next_version(prev_text):
    today = int(datetime.date.today().strftime("%Y%m%d")) * 100
    prev = 0
    if prev_text:
        first = prev_text.split("\n", 1)[0].strip()
        prev = int(first) if first.isdigit() else 0
    return max(today + 1, prev + 1)


def main():
    args = sys.argv[1:]
    share = None
    if "--share" in args:
        i = args.index("--share")
        share = pathlib.Path(args[i + 1]) / "CursorPad-PLM" / "tesseract"
        del args[i:i + 2]
    if len(args) != 1:
        sys.exit(__doc__)
    src = pathlib.Path(args[0])
    files = files_of(src)

    langs = sorted((r.split("/")[1].removesuffix(".traineddata") for r, _ in files if r.startswith("tessdata/")),
                   key=lambda l: (l != "rus", l))
    lines = []
    for rel, p in files:
        data = p.read_bytes()
        lines.append(f"f {hashlib.sha256(data).hexdigest()} {len(data)} {rel}")

    old = PUB / "pack.txt"
    prev = old.read_text(encoding="utf-8") if old.exists() else ""
    body_now = "\n".join(lines)
    if prev and prev.split("\n", 2)[-1].strip() == body_now.strip():
        print("пакет не изменился — номер прежний:", prev.split("\n", 1)[0])
        ver = int(prev.split("\n", 1)[0])
    else:
        ver = next_version(prev)
    text = f"{ver}\nTesseract 5 · {' + '.join(langs)}\n{body_now}\n"

    if PUB.exists():
        shutil.rmtree(PUB)
    PUB.mkdir(parents=True)
    for rel, p in files:
        dst = PUB / (rel + ".bin")
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, dst)
    (PUB / "pack.txt").write_text(text, encoding="utf-8", newline="\n")
    total = sum(p.stat().st_size for _, p in files)
    print(f"public/tess: пакет {ver}, {len(files)} файлов, {total / 1048576:.1f} МБ, языки {', '.join(langs)}")

    if share:
        if share.exists():
            shutil.rmtree(share)
        share.mkdir(parents=True)
        for rel, p in files:
            dst = share / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, dst)
        (share / "pack.txt").write_text(text, encoding="utf-8", newline="\n")
        print("общая папка:", share)


if __name__ == "__main__":
    main()
