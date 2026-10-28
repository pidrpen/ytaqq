"""Адаптер LoRA (peft) → GGUF для llama.cpp, имя draw-lora-<модель>.gguf: CursorPad
подключает его сам, если файл лежит рядом с моделью (ai_local.c), и только для чертежей.
Адаптер инструментов агента (train_tools_lora.py) — tool-lora-<модель>.gguf.

  python to_gguf.py --adapter runs/r1/adapter --base base/Qwen3.5-4B --llama llama.cpp \\
      --out runs/r1/draw-lora-qwen3.5-4b.gguf
llama.cpp — исходники той же сборки, что llama-server у CursorPad (llama-server --version)."""
import argparse
import pathlib
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--adapter', required=True)
    ap.add_argument('--base', required=True, help='папка весов Hugging Face, на которых учили')
    ap.add_argument('--llama', required=True, help='папка исходников llama.cpp (convert_lora_to_gguf.py)')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    if not out.name.startswith(('draw-lora', 'tool-lora')):
        sys.exit('имя файла должно начинаться с draw-lora или tool-lora — так его находит CursorPad')
    subprocess.run([sys.executable, str(pathlib.Path(a.llama) / 'convert_lora_to_gguf.py'), '--base', a.base,
                    '--outfile', str(out), '--outtype', 'f16', a.adapter], check=True)
    print('готово:', out, out.stat().st_size >> 20, 'МБ')


if __name__ == '__main__':
    main()
