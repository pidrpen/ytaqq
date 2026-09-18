/* Search the web from the exe (DuckDuckGo lite + recipe JSON-LD). */

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

static void url_decode_str(const char *in, char *out, int cap) {
  int o = 0;
  for (int i = 0; in[i] && o + 1 < cap; i++) {
    if (in[i] == '%' && in[i + 1] && in[i + 2]) {
      out[o++] = (char)((hexval(in[i + 1]) << 4) | hexval(in[i + 2]));
      i += 2;
    } else if (in[i] == '+') {
      out[o++] = ' ';
    } else {
      out[o++] = in[i];
    }
  }
  out[o] = 0;
}

static void html_unescape_copy(const char *in, char *out, int cap) {
  int o = 0;
  for (int i = 0; in[i] && o + 1 < cap; i++) {
    if (in[i] == '&') {
      if (!strncmp(in + i, "&amp;", 5)) {
        out[o++] = '&';
        i += 4;
        continue;
      }
      if (!strncmp(in + i, "&quot;", 6) || !strncmp(in + i, "&#34;", 5)) {
        out[o++] = '"';
        i += in[i + 1] == '#' ? 4 : 5;
        continue;
      }
      if (!strncmp(in + i, "&lt;", 4)) {
        out[o++] = '<';
        i += 3;
        continue;
      }
      if (!strncmp(in + i, "&gt;", 4)) {
        out[o++] = '>';
        i += 3;
        continue;
      }
      if (!strncmp(in + i, "&nbsp;", 6) || !strncmp(in + i, "&#160;", 6)) {
        out[o++] = ' ';
        i += 5;
        continue;
      }
    }
    out[o++] = in[i];
  }
  out[o] = 0;
}

static void strip_tags(const char *in, char *out, int cap) {
  int o = 0, skip = 0;
  for (int i = 0; in[i] && o + 1 < cap; i++) {
    if (in[i] == '<') {
      skip = 1;
      continue;
    }
    if (in[i] == '>') {
      skip = 0;
      continue;
    }
    if (skip) continue;
    if (in[i] == '\n' || in[i] == '\r' || in[i] == '\t') {
      if (o && out[o - 1] != ' ') out[o++] = ' ';
      continue;
    }
    out[o++] = in[i];
  }
  while (o > 0 && out[o - 1] == ' ') o--;
  out[o] = 0;
}

static void utf8_to_wide(const char *in, wchar_t *out, int cap) {
  char tmp[2400];
  html_unescape_copy(in, tmp, (int)sizeof(tmp));
  MultiByteToWideChar(CP_UTF8, 0, tmp, -1, out, cap);
  out[cap - 1] = 0;
}

typedef struct {
  char url[512];
  char title[240];
  char snippet[560];
} NetHit;

static int parse_ddg_lite(const char *html, NetHit *hits, int maxn) {
  int n = 0;
  const char *p = html;
  while (n < maxn && (p = strstr(p, "result-link"))) {
    const char *tag = p;
    while (tag > html && *tag != '<') tag--;
    const char *href = NULL;
    const char *q = tag;
    while (q < p && n < maxn) {
      const char *u = strstr(q, "uddg=");
      if (!u || u > p) break;
      u += 5;
      const char *e = u;
      while (*e && *e != '&' && *e != '"' && *e != '\'') e++;
      char enc[800], dec[800];
      int el = (int)(e - u);
      if (el > 0 && el < 799) {
        memcpy(enc, u, (size_t)el);
        enc[el] = 0;
        url_decode_str(enc, dec, 800);
        if (!strncmp(dec, "https://", 8)) href = dec;
      }
      q = e;
      break;
    }
    const char *gt = strchr(p, '>');
    const char *ae = gt ? strstr(gt, "</a>") : NULL;
    char title[240] = {0}, snippet[560] = {0};
    if (gt && ae && ae - gt < 400) {
      char raw[400];
      int tl = (int)(ae - gt - 1);
      if (tl > 0) {
        if (tl > 399) tl = 399;
        memcpy(raw, gt + 1, (size_t)tl);
        raw[tl] = 0;
        strip_tags(raw, title, 240);
      }
    }
    const char *sn = strstr(p, "result-snippet");
    const char *next = strstr(p + 12, "result-link");
    if (sn && (!next || sn < next)) {
      const char *sg = strchr(sn, '>');
      const char *se = sg ? strstr(sg, "</td>") : NULL;
      if (sg && se && se - sg < 800) {
        char raw[800];
        int sl = (int)(se - sg - 1);
        if (sl > 799) sl = 799;
        if (sl > 0) {
          memcpy(raw, sg + 1, (size_t)sl);
          raw[sl] = 0;
          strip_tags(raw, snippet, 560);
        }
      }
    }
    if (href && (title[0] || snippet[0])) {
      lstrcpynA(hits[n].url, href, 512);
      lstrcpynA(hits[n].title, title, 240);
      lstrcpynA(hits[n].snippet, snippet, 560);
      n++;
    }
    p += 12;
  }
  return n;
}

static const char *find_ci(const char *s, const char *needle) {
  return strstr(s, needle);
}

static int json_string_array_after(const char *json, const char *key, char *out, int cap) {
  const char *p = strstr(json, key);
  if (!p) return 0;
  p = strchr(p, '[');
  if (!p) return 0;
  p++;
  int o = 0, count = 0;
  while (*p && *p != ']' && count < 12 && o + 4 < cap) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',') p++;
    if (*p != '"') break;
    wchar_t w[240];
    if (!parse_json_string(&p, w, 240) || !w[0]) break;
    char u8[480];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, 480, NULL, NULL);
    int n = (int)strlen(u8);
    if (o && o + 2 < cap) {
      out[o++] = '\n';
      out[o++] = 0xE2;
      out[o++] = 0x80;
      out[o++] = 0xA2;
      out[o++] = ' ';
    } else if (!o && o + 4 < cap) {
      out[o++] = 0xE2;
      out[o++] = 0x80;
      out[o++] = 0xA2;
      out[o++] = ' ';
    }
    if (o + n >= cap - 1) break;
    memcpy(out + o, u8, (size_t)n);
    o += n;
    count++;
  }
  out[o] = 0;
  return count;
}

static int json_steps_after(const char *json, const char *key, char *out, int cap) {
  const char *p = strstr(json, key);
  if (!p) return 0;
  const char *end = p + 8000;
  int o = 0, nstep = 0;
  const char *q = p;
  while (nstep < 8 && q && q < end) {
    q = strstr(q, "\"text\"");
    if (!q || q > end) break;
    q = strchr(q, ':');
    if (!q) break;
    q++;
    wchar_t w[400];
    if (!parse_json_string(&q, w, 400) || !w[0]) continue;
    char u8[800];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, 800, NULL, NULL);
    int n = (int)strlen(u8);
    char prefix[8];
    int pn = snprintf(prefix, sizeof(prefix), "%d. ", nstep + 1);
    if (o + pn + n + 2 >= cap) break;
    if (o) out[o++] = '\n';
    memcpy(out + o, prefix, (size_t)pn);
    o += pn;
    memcpy(out + o, u8, (size_t)n);
    o += n;
    nstep++;
  }
  out[o] = 0;
  return nstep;
}

static BOOL extract_recipe(const char *html, char *out, int cap) {
  const char *p = html;
  char ing[1400] = {0}, steps[1600] = {0}, name[240] = {0};
  while ((p = strstr(p, "ld+json"))) {
    const char *gt = strchr(p, '>');
    const char *end = gt ? strstr(gt, "</script>") : NULL;
    if (!gt || !end) break;
    int block = (int)(end - gt);
    if (block > 8 && block < 120000) {
      if (strstr(gt, "Recipe") || strstr(gt, "recipeIngredient")) {
        wchar_t wname[200];
        if (json_field_string(gt, "name", wname, 200) && wname[0])
          WideCharToMultiByte(CP_UTF8, 0, wname, -1, name, 240, NULL, NULL);
        json_string_array_after(gt, "recipeIngredient", ing, 1400);
        if (!steps[0]) json_steps_after(gt, "recipeInstructions", steps, 1600);
        if (ing[0] || steps[0]) {
          if (!name[0]) lstrcpynA(name, "Рецепт", 240);
          snprintf(out, (size_t)cap, "%s\n\n%s%s%s", name, ing,
                    (ing[0] && steps[0]) ? "\n\n" : "", steps);
          out[cap - 1] = 0;
          return TRUE;
        }
      }
    }
    p = end ? end + 1 : p + 7;
  }
  (void)find_ci;
  return FALSE;
}

static int looks_like_recipe_url(const char *url) {
  if (strstr(url, "recipe") || strstr(url, "recept") || strstr(url, "рецепт")) return 3;
  if (strstr(url, "eda.ru") || strstr(url, "povarenok") || strstr(url, "koolinar") ||
      strstr(url, "gastronom") || strstr(url, "russianfood") || strstr(url, "iamcook") ||
      strstr(url, "ovkuse"))
    return 2;
  if (strstr(url, "/search") || strstr(url, "bytype") || strstr(url, "/tag/")) return 0;
  return 1;
}

static BOOL internet_ask(const wchar_t *query, wchar_t *out, int cap) {
  char enc[2400];
  url_encode_utf8(query, enc, (int)sizeof(enc));
  wchar_t wenc[2400], path[2800];
  MultiByteToWideChar(CP_UTF8, 0, enc, -1, wenc, 2400);
  _snwprintf(path, 2800, L"/lite/?q=%s&kl=ru-ru", wenc);
  char *html = NULL;
  DWORD n = 0;
  if (!http_get(L"lite.duckduckgo.com", path, &html, &n) || !html) return FALSE;
  NetHit hits[6];
  memset(hits, 0, sizeof(hits));
  int nh = parse_ddg_lite(html, hits, 6);
  free(html);
  if (nh <= 0) return FALSE;

  char recipe[2000] = {0};
  int order[6];
  for (int i = 0; i < nh; i++) order[i] = i;
  for (int i = 0; i < nh; i++) {
    for (int j = i + 1; j < nh; j++) {
      if (looks_like_recipe_url(hits[order[j]].url) > looks_like_recipe_url(hits[order[i]].url)) {
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
      }
    }
  }
  int fetched = 0;
  for (int k = 0; k < nh && fetched < 2 && !recipe[0]; k++) {
    int i = order[k];
    if (looks_like_recipe_url(hits[i].url) <= 0) continue;
    char *page = NULL;
    DWORD pn = 0;
    if (!http_get_url(hits[i].url, &page, &pn) || !page) continue;
    fetched++;
    extract_recipe(page, recipe, 2000);
    free(page);
  }

  char buf[2200];
  if (recipe[0]) {
    lstrcpynA(buf, recipe, 2200);
  } else {
    int o = 0;
    for (int i = 0; i < nh && i < 3; i++) {
      char line[800];
      if (hits[i].snippet[0])
        snprintf(line, sizeof(line), "%s\n%s\n", hits[i].title, hits[i].snippet);
      else
        snprintf(line, sizeof(line), "%s\n", hits[i].title);
      int ln = (int)strlen(line);
      if (o + ln >= 2100) break;
      memcpy(buf + o, line, (size_t)ln);
      o += ln;
      buf[o++] = '\n';
      buf[o] = 0;
    }
    if (!o) {
      return FALSE;
    }
  }
  utf8_to_wide(buf, out, cap);
  return out[0] != 0;
}
