import { miniAiAsk } from "@/lib/mini-ai";

export type SearchEngine = "wiki" | "ddg" | "yandex" | "ai" | "plm";

async function wikiExtract(lang: "ru" | "en", query: string): Promise<string | null> {
  const q = query.trim();
  if (!q) return null;
  const open = await fetch(
    `https://${lang}.wikipedia.org/w/api.php?action=opensearch&search=${encodeURIComponent(q)}&limit=1&namespace=0&format=json&origin=*`,
  );
  if (!open.ok) return null;
  const data = (await open.json()) as unknown;
  const title = Array.isArray(data) && Array.isArray(data[1]) ? String(data[1][0] ?? "") : "";
  if (!title) return null;
  const sum = await fetch(
    `https://${lang}.wikipedia.org/api/rest_v1/page/summary/${encodeURIComponent(title.replace(/ /g, "_"))}`,
  );
  if (!sum.ok) return null;
  const body = (await sum.json()) as { extract?: string; description?: string };
  const text = (body.extract || body.description || "").trim();
  return text || null;
}

async function ddgExtract(query: string): Promise<string | null> {
  try {
    const res = await fetch(
      `https://api.duckduckgo.com/?q=${encodeURIComponent(query)}&format=json&no_html=1&skip_disambig=1&t=cursorpad`,
    );
    if (!res.ok) return null;
    const body = (await res.json()) as {
      AbstractText?: string;
      Answer?: string;
      Definition?: string;
      RelatedTopics?: { Text?: string }[];
    };
    const text =
      body.AbstractText ||
      body.Answer ||
      body.Definition ||
      body.RelatedTopics?.[0]?.Text ||
      "";
    return text.trim() || null;
  } catch {
    return null;
  }
}

function clip(text: string, max = 720) {
  if (text.length <= max) return text;
  const cut = text.slice(0, max);
  const dot = cut.lastIndexOf(".");
  return (dot > 80 ? cut.slice(0, dot + 1) : cut) + "…";
}

export async function lookupBrief(
  query: string,
  engine: SearchEngine,
): Promise<{ source: string; text: string }> {
  const q = query.replace(/\s+/g, " ").trim();
  if (!q) return { source: "", text: "Нечего искать — скопируйте текст" };

  let text: string | null = null;
  let source = "";

  if (engine === "plm") {
    const sample =
      "pmsz-plm:um-splmsrv[a4484722]:4450/IO.6089001";
    return {
      source: "PLM",
      text:
        `В .exe: SQL-логин к UM-SQLSRV (не Windows). Ссылка pmsz-plm:um-splmsrv[логин]:4450/IO.{id}\n${sample}\n\n` +
        `Укажите пользователя и пароль SQL в Настройках. Пример названия: «Масло индустриальное И-12А ГОСТ 20799-2022».`,
    };
  }

  if (engine === "ai") {
    try {
      const res = await fetch("/api/ask", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ q }),
      });
      if (res.ok) {
        const net = (await res.json()) as { source?: string; text?: string };
        if (net.text) return { source: net.source || "Интернет", text: net.text };
      }
    } catch {
      /* offline */
    }
    const hit = miniAiAsk(q);
    return { source: `${hit.title} (офлайн)`, text: hit.text };
  }

  if (engine === "ddg") {
    text = await ddgExtract(q);
    if (text) source = "DuckDuckGo";
    if (!text) {
      text = (await wikiExtract("en", q)) || (await wikiExtract("ru", q));
      if (text) source = "Википедия";
    }
  } else if (engine === "yandex") {
    text = await wikiExtract("ru", q);
    if (text) source = "Википедия";
    if (!text) {
      text = await ddgExtract(q);
      if (text) source = "DuckDuckGo";
    }
    if (!text) {
      text = await wikiExtract("en", q);
      if (text) source = "Википедия";
    }
  } else {
    text = (await wikiExtract("ru", q)) || (await wikiExtract("en", q));
    if (text) source = "Википедия";
    if (!text) {
      text = await ddgExtract(q);
      if (text) source = "DuckDuckGo";
    }
  }

  if (!text) {
    const hit = miniAiAsk(q);
    return { source: hit.title, text: hit.text };
  }
  return { source, text: clip(text) };
}
