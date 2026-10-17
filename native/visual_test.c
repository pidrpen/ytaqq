/* Included only in the VISUAL_TEST build. Render production pixels directly;
   no windows, hooks, settings, cursors or updater are started. */
static int visual_test(const wchar_t *path) {
  enum { W = 900, H = 500 }; BYTE *bits = (BYTE *)malloc(W * H * 4);
  if (!bits) return 2;
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    BYTE *p = bits + (y * W + x) * 4; p[0] = 245; p[1] = 245; p[2] = 245; p[3] = 255;
  }
  for (int mode = 0; mode < 2; mode++) for (int size = 0; size < 3; size++) {
    int n = 32 + size * 16, scale = 3;
    for (int y = 0; y < n * scale; y++) for (int x = 0; x < n * scale; x++) {
      float c[3];
      if (st_aubergine((x + 0.5f) * 32 / (n * scale), (y + 0.5f) * 32 / (n * scale), mode, c)) {
        int xx = 20 + size * 210 + x, yy = 10 + mode * 220 + y;
        BYTE *p = bits + (yy * W + xx) * 4; p[0] = (BYTE)c[2]; p[1] = (BYTE)c[1]; p[2] = (BYTE)c[0];
      }
    }
  }
  int frames[] = {4, 14, 35, 62};
  for (int k = 0; k < 4; k++) {
    FxWin f; memset(&f, 0, sizeof(f)); f.kind = 15; f.W = f.H = 170;
    f.bits = (BYTE *)calloc(170 * 170, 4); if (!f.bits) { free(bits); return 2; }
    g_fxScale = 1.0f; g_fxSeed = 12345; fx_seed_new(&f, 15, 170);
    for (f.frame = 0; f.frame < frames[k]; f.frame++) fx_step_new(&f);
    fx_caviar_paint(&f);
    for (int y = 0; y < 170; y++) for (int x = 0; x < 170; x++) {
      BYTE *src = f.bits + (y * 170 + x) * 4;
      BYTE *dst = bits + (((k / 2) * 220 + y + 10) * W + (k % 2) * 150 + x + 580) * 4;
      float a = src[3] / 255.0f;
      for (int c = 0; c < 3; c++) dst[c] = (BYTE)(src[c] + dst[c] * (1 - a));
    }
    free(f.bits);
  }
  BITMAPFILEHEADER fh = {0}; BITMAPINFOHEADER ih = {0};
  fh.bfType = 0x4d42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + W * H * 4;
  ih.biSize = sizeof(ih); ih.biWidth = W; ih.biHeight = -H; ih.biPlanes = 1; ih.biBitCount = 32;
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL); BOOL ok = FALSE;
  if (h != INVALID_HANDLE_VALUE) { ok = ag_bytes(h, &fh, sizeof(fh)) && ag_bytes(h, &ih, sizeof(ih)) && ag_bytes(h, bits, W * H * 4); CloseHandle(h); }
  free(bits); return ok ? 0 : 1;
}
