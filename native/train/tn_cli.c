/* tn_cli вход.txt — маршрут правилами techno.c, как в CursorPad, для проверки моделей (native/train).
   Вход (UTF-8): наименование, материал и текст листа, разделённые символом \f (материал бывает
   в две строки: прочитанное моделью и Tesseract).
   Выход (UTF-8): вид|операция|операция…||заготовка; с ключом --text — маршрут, как его видит человек.
   Сборка: gcc -O2 -finput-charset=UTF-8 -Wno-unused-function -o tn_cli.exe tn_cli.c -luser32 -s */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../techno.c"

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 2;
  static char buf[1 << 20];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = 0;
  static wchar_t w[1 << 20];
  if (MultiByteToWideChar(CP_UTF8, 0, buf, -1, w, 1 << 20) <= 0) return 2;
  wchar_t *name = w, *mat = wcschr(w, L'\f'), *text = NULL;
  if (mat) *mat++ = 0, text = wcschr(mat, L'\f');
  if (text) *text++ = 0;
  TnRoute *r = (TnRoute *)malloc(sizeof(TnRoute));
  if (!r) return 2;
  tn_route(name, mat ? mat : L"", text ? text : L"", r);
  static wchar_t out[8000];
  if (argc > 2 && !strcmp(argv[2], "--text")) {
    tn_text(r, out, 8000);
  } else {
    swprintf(out, 8000, L"%d", r->cls);
    for (int i = 0; i < r->n; i++) wcscat(out, L"|"), wcscat(out, r->op[i].op);
    wcscat(out, L"||");
    wcscat(out, r->blank);
  }
  static char u[32000];
  WideCharToMultiByte(CP_UTF8, 0, out, -1, u, sizeof(u), NULL, NULL);
  fwrite(u, 1, strlen(u), stdout);
  return 0;
}
