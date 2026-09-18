import { createServerFn } from "@tanstack/react-start";

type Hit = { url: string; title: string; snippet: string };

function stripTags(s: string) {
  return s
    .replace(/<[^>]+>/g, " ")
    .replace(/&nbsp;/g, " ")
    .replace(/&amp;/g, "&")
    .replace(/&quot;/g, '"')
    .replace(/&#34;/g, '"')
    .replace(/\s+/g, " ")
    .trim();
}

function decodeUddg(href: string): string | null {
  const m = href.match(/uddg=([^&"']+)/);
  if (!m) return null;
  try {
    const u = decodeURIComponent(m[1]);
    return u.startsWith("https://") ? u : null;
  } catch {
    return null;
  }
}

function parseDdgLite(html: string): Hit[] {
  const hits: Hit[] = [];
  const re =
    /uddg=([^&"']+)[\s\S]{0,500}?class=['"]result-link['"][^>]*>([\s\S]*?)<\/a>[\s\S]{0,900}?class=['"]result-snippet['"][^>]*>([\s\S]*?)<\/td>/gi;
  let m: RegExpExecArray | null;
  while ((m = re.exec(html)) && hits.length < 6) {
    let url: string | null = null;
    try {
      url = decodeURIComponent(m[1]);
    } catch {
      url = null;
    }
    if (!url?.startsWith("https://")) continue;
    hits.push({ url, title: stripTags(m[2]), snippet: stripTags(m[3]) });
  }
  return hits;
}

function recipeScore(url: string) {
  if (/\/search|bytype|\/tag\//i.test(url)) return 0;
  if (/recipe|recept|eda\.ru|povarenok|koolinar|gastronom|russianfood|iamcook/i.test(url))
    return 3;
  return 1;
}

function extractRecipe(html: string): string | null {
  const scripts = html.match(/<script[^>]*ld\+json[^>]*>[\s\S]*?<\/script>/gi) || [];
  for (const s of scripts) {
    const json = s.replace(/^[\s\S]*?>/, "").replace(/<\/script>[\s\S]*$/, "");
    if (!/Recipe|recipeIngredient/i.test(json)) continue;
    try {
      const data = JSON.parse(json) as Record<string, unknown> | unknown[];
      const nodes = Array.isArray(data)
        ? data
        : data && typeof data === "object" && Array.isArray((data as { "@graph"?: unknown[] })["@graph"])
          ? ((data as { "@graph": unknown[] })["@graph"] as unknown[])
          : [data];
      for (const node of nodes) {
        if (!node || typeof node !== "object") continue;
        const o = node as Record<string, unknown>;
        const typ = String(o["@type"] ?? "");
        if (!/Recipe/i.test(typ) && !Array.isArray(o.recipeIngredient)) continue;
        const name = String(o.name ?? "Рецепт");
        const ing = Array.isArray(o.recipeIngredient)
          ? o.recipeIngredient.map((x) => `• ${stripTags(String(x))}`).join("\n")
          : "";
        let steps = "";
        const ins = o.recipeInstructions;
        if (typeof ins === "string") steps = stripTags(ins);
        else if (Array.isArray(ins)) {
          steps = ins
            .map((x, i) => {
              if (typeof x === "string") return `${i + 1}. ${stripTags(x)}`;
              if (x && typeof x === "object" && "text" in x)
                return `${i + 1}. ${stripTags(String((x as { text: string }).text))}`;
              return "";
            })
            .filter(Boolean)
            .slice(0, 8)
            .join("\n");
        }
        if (ing || steps) return `${name}\n\n${ing}${ing && steps ? "\n\n" : ""}${steps}`;
      }
    } catch {
      /* not json */
    }
  }
  return null;
}

async function fetchText(url: string, ms = 12000): Promise<string | null> {
  const ac = new AbortController();
  const t = setTimeout(() => ac.abort(), ms);
  try {
    const res = await fetch(url, {
      signal: ac.signal,
      headers: {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) CursorPad/1.0",
        Accept: "text/html,application/json;q=0.9",
        "Accept-Language": "ru,en;q=0.8",
      },
      redirect: "follow",
    });
    if (!res.ok) return null;
    return await res.text();
  } catch {
    return null;
  } finally {
    clearTimeout(t);
  }
}

export async function searchWeb(qRaw: string): Promise<{ source: string; text: string }> {
  const q = qRaw.replace(/\s+/g, " ").trim();
  if (!q) return { source: "", text: "Нечего искать" };
    const url =
      "https://lite.duckduckgo.com/lite/?q=" + encodeURIComponent(q) + "&kl=ru-ru";
    const html = await fetchText(url);
    if (!html) return { source: "", text: "" };
    const hits = parseDdgLite(html).sort((a, b) => recipeScore(b.url) - recipeScore(a.url));
    let recipe: string | null = null;
    let n = 0;
    for (const h of hits) {
      if (!h.url || recipeScore(h.url) <= 0 || n >= 2) continue;
      n++;
      const page = await fetchText(h.url, 10000);
      if (!page) continue;
      recipe = extractRecipe(page);
      if (recipe) break;
    }
    if (recipe) return { source: "Интернет", text: recipe.slice(0, 1400) };
    const brief = hits
      .slice(0, 3)
      .map((h) => [h.title, h.snippet].filter(Boolean).join("\n"))
      .filter(Boolean)
      .join("\n\n");
    return { source: "Интернет", text: brief.slice(0, 1200) };
}

export const webAsk = createServerFn({ method: "POST" })
  .validator((input: { q: string }) => input)
  .handler(async ({ data }) => searchWeb(data.q));
