/* Existing Word formatting: owned COM session, checked source, atomic replace
   with an original backup. Never reconstruct the document from model text. */
#include <wincrypt.h>
static int of_word_alignment(const wchar_t *mode) {
  if (!wcscmp(mode, L"left")) return 0;
  if (!wcscmp(mode, L"center")) return 1;
  if (!wcscmp(mode, L"right")) return 2;
  if (!wcscmp(mode, L"justify")) return 3;
  return -1;
}
static const wchar_t *ag_format_mode(const wchar_t *goal) {
  wchar_t q[4000]; lstrcpynW(q, goal, 4000); CharLowerBuffW(q, (DWORD)wcslen(q));
  if ((wcsstr(q, L"создай") || wcsstr(q, L"создать")) && !wcsstr(q, L"отредакт")) return NULL;
  if (!(wcsstr(q, L"выравнив") || wcsstr(q, L"выровн") || wcsstr(q, L"отредакт") || wcsstr(q, L"сделай текст"))) return NULL;
  if (wcsstr(q, L"по центру")) return L"center";
  if (wcsstr(q, L"по левому") || wcsstr(q, L"слева")) return L"left";
  if (wcsstr(q, L"по правому") || wcsstr(q, L"справа")) return L"right";
  if (wcsstr(q, L"по ширине")) return L"justify";
  return NULL;
}
static BOOL ag_word_hash(const wchar_t *path, wchar_t hex[65]) {
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
  BOOL ok = CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) && CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
  BYTE block[65536], digest[32]; DWORD got, size = 32;
  while (ok) { if (!ReadFile(f, block, sizeof(block), &got, NULL)) { ok = FALSE; break; } if (!got) break; ok = CryptHashData(hash, block, got, 0); }
  if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0) && size == 32;
  if (ok) { for (int i = 0; i < 32; i++) swprintf(hex + i * 2, 3, L"%02x", digest[i]); hex[64] = 0; }
  if (hash) CryptDestroyHash(hash);
  if (provider) CryptReleaseContext(provider, 0);
  CloseHandle(f); return ok;
}
static BOOL of_word_format_copy(const wchar_t *src, const wchar_t *dst, const wchar_t *mode, wchar_t *out, int cap) {
  int alignment = of_word_alignment(mode); OfSession s = {0}; BOOL ok = FALSE;
  IDispatch *content = NULL, *format = NULL; VARIANT before, after, value;
  VariantInit(&before); VariantInit(&after); VariantInit(&value);
  if (alignment < 0 || !of_begin(&s, FALSE, src)) { swprintf(out, cap, L"Word недоступен: 0x%08lx. Исходник не изменён.", (unsigned long)s.hr); goto done; }
  content = of_obj(s.doc, L"Content", 0, NULL); format = of_obj(content, L"ParagraphFormat", 0, NULL);
  if (FAILED(of_call(content, L"Text", DISPATCH_PROPERTYGET, &before, 0, NULL)) || before.vt != VT_BSTR ||
      FAILED(of_put(format, L"Alignment", of_int(alignment))) ||
      FAILED(of_call(format, L"Alignment", DISPATCH_PROPERTYGET, &value, 0, NULL)) || value.vt != VT_I4 || value.lVal != alignment ||
      FAILED(of_call(content, L"Text", DISPATCH_PROPERTYGET, &after, 0, NULL)) || after.vt != VT_BSTR ||
      SysStringLen(before.bstrVal) != SysStringLen(after.bstrVal) || wcscmp(before.bstrVal, after.bstrVal)) {
    lstrcpynW(out, L"Word не подтвердил выравнивание с сохранением текста. Исходник не изменён.", cap); goto done;
  }
  HANDLE reservation = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (reservation == INVALID_HANDLE_VALUE) { lstrcpynW(out, L"Временное имя занято. Исходник не изменён.", cap); goto done; }
  CloseHandle(reservation);
  HRESULT hr = of_call(s.doc, L"SaveAs2", DISPATCH_METHOD, NULL, 2, (VARIANT[]){of_str(dst), of_int(12)});
  if (FAILED(hr)) { DeleteFileW(dst); swprintf(out, cap, L"Word не сохранил форматированную копию: 0x%08lx. Исходник не изменён.", (unsigned long)hr); goto done; }
  ok = TRUE;
done:
  VariantClear(&before); VariantClear(&after); VariantClear(&value);
  of_release(format); of_release(content); of_end(&s); return ok;
}
static BOOL ag_word_apply(AgFs *fs, const AgAction *a, wchar_t *out, int cap) {
  wchar_t hash[65], parent[MAX_PATH], temp[MAX_PATH]; GUID id;
  if (!ag_word_hash(a->src, hash) || wcscmp(hash, a->sheet)) { lstrcpynW(out, L"Word изменился после предпросмотра. Форматирование не применено.", cap); return FALSE; }
  lstrcpynW(parent, a->src, MAX_PATH); wchar_t *slash = wcsrchr(parent, L'\\'); if (!slash) return FALSE; *slash = 0;
  if (FAILED(CoCreateGuid(&id)) || wcslen(parent) + 55 >= MAX_PATH) return FALSE;
  swprintf(temp, MAX_PATH, L"%ls\\cpw-%08lx-%04x-%04x-%08lx.docx", parent, (unsigned long)id.Data1, id.Data2, id.Data3, (unsigned long)GetTickCount());
  if (!of_word_format_copy(a->src, temp, a->cell, out, cap)) return FALSE;
  BOOL ok = FALSE;
  if ((fs->cancel && *fs->cancel) || !ag_word_hash(a->src, hash) || wcscmp(hash, a->sheet)) {
    lstrcpynW(out, L"Отмена или Word изменился во время подготовки. Исходник не заменён.", cap); goto done;
  }
  HANDLE reserve = CreateFileW(a->text, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (reserve == INVALID_HANDLE_VALUE) { lstrcpynW(out, L"Имя резервной копии занято. Исходник не заменён.", cap); goto done; }
  CloseHandle(reserve);
  ok = ReplaceFileW(a->src, temp, a->text, 0, NULL, NULL);
  if (!ok) {
    DWORD error = GetLastError(); WIN32_FILE_ATTRIBUTE_DATA b;
    if (GetFileAttributesW(a->src) == INVALID_FILE_ATTRIBUTES && GetFileAttributesExW(a->text, GetFileExInfoStandard, &b) && (b.nFileSizeHigh || b.nFileSizeLow)) MoveFileExW(a->text, a->src, 0);
    if (GetFileAttributesExW(a->text, GetFileExInfoStandard, &b) && !b.nFileSizeHigh && !b.nFileSizeLow) DeleteFileW(a->text);
    swprintf(out, cap, L"Не удалось заменить Word, ошибка Windows %lu. Проверьте исходник %ls и резервный путь %ls. Возможно, документ открыт в Word.", error, a->src, a->text);
  } else swprintf(out, cap, L"Изменено выравнивание Word. Исходный документ сохранён: %ls.", a->text);
done:
  DeleteFileW(temp); return ok;
}
