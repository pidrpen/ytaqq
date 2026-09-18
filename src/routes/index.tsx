import { createFileRoute } from "@tanstack/react-router";
import { useCallback, useEffect, useRef, useState, type MutableRefObject } from "react";
import {
  Download,
  Keyboard,
  MousePointer2,
  Pin,
  Shield,
} from "lucide-react";
import { CursorPad } from "@/components/cursor-pad";
import { lookupBrief, type SearchEngine } from "@/lib/lookup";

import { cn } from "@/lib/utils";

export const Route = createFileRoute("/")({ component: Home });

const NOTES_KEY = "cursorpad.notes";
const DESK_KEY = "cursorpad.desk";
const HINT_KEY = "cursorpad.hintDismissed";
const CURSOR_KEY = "cursorpad.main";
const ENGINE_KEY = "cursorpad.engine.v2";
const AUTO_KEY = "cursorpad.autostart";
const HOTKEY = "F8";

const DEFAULT_NOTES =
  "Первая фраза для вставки\nВторая фраза\nТретья фраза\nЧетвёртая фраза\nПятая фраза";

const DEFAULT_DESK =
  "Поставьте курсор сюда и нажмите Ctrl+1 — сюда вставится первая строка из CursorPad.\n\nCtrl+2 и Ctrl+3 — вторая и третья.\n\nF8 закрепляет блокнот, чтобы править шаблоны.";

const MAINS = [
  {
    id: "k2",
    name: "Мечник",
    src: "/cursors/k2.png",
    preview: "/cursors/k2.png",
    frames: [
      "/cursors/k2-load-01.png",
      "/cursors/k2-load-02.png",
      "/cursors/k2-load-03.png",
      "/cursors/k2-load-04.png",
      "/cursors/k2-load-05.png",
      "/cursors/k2-load-06.png",
      "/cursors/k2-load-07.png",
      "/cursors/k2-load-08.png",
    ],
    hotx: 3,
    hoty: 1,
    ibeam: "/cursors/k2-ibeam.png",
    hand: "/cursors/k2-hand.png",
    ibeamHot: [9, 3] as const,
    handHot: [8, 3] as const,
  },
  {
    id: "k3",
    name: "Рукавица",
    src: "/cursors/k3.png",
    preview: "/cursors/k3.png",
    frames: [
      "/cursors/k3-load-01.png",
      "/cursors/k3-load-02.png",
      "/cursors/k3-load-03.png",
      "/cursors/k3-load-04.png",
      "/cursors/k3-load-05.png",
      "/cursors/k3-load-06.png",
      "/cursors/k3-load-07.png",
      "/cursors/k3-load-08.png",
    ],
    hotx: 3,
    hoty: 2,
    ibeam: "/cursors/k3-ibeam.png",
    hand: "/cursors/k3-hand.png",
    ibeamHot: [9, 3] as const,
    handHot: [8, 3] as const,
  },
] as const;

type MainId = (typeof MAINS)[number]["id"];

function nthNonEmptyLine(text: string, n: number): string | null {
  const lines = text.split(/\r\n|\n|\r/).filter((line) => line.length > 0);
  return lines[n - 1] ?? null;
}

function readStore(key: string, fallback: string) {
  if (typeof window === "undefined") return fallback;
  try {
    return localStorage.getItem(key) ?? fallback;
  } catch {
    return fallback;
  }
}

function Home() {
  const stageRef = useRef<HTMLDivElement>(null);
  const pointer = useRef({ x: 72, y: 120 });
  const cursorPos = useRef({ x: 72, y: 120 });
  const live = useRef(false);
  const notesRef = useRef(DEFAULT_NOTES);
  const deskRef = useRef<HTMLTextAreaElement>(null);
  const [following, setFollowing] = useState(true);
  const [notes, setNotes] = useState(DEFAULT_NOTES);
  const [desk, setDesk] = useState(DEFAULT_DESK);
  const [hint, setHint] = useState(true);
  const [hydrated, setHydrated] = useState(false);
  const [coarse, setCoarse] = useState(false);
  const [now, setNow] = useState(() => new Date());
  const [toast, setToast] = useState<string | null>(null);
  const [picked, setPicked] = useState<MainId>("k2");
  const [hover, setHover] = useState<MainId | null>(null);
  const [padHidden, setPadHidden] = useState(false);
  const [alphaFollow, setAlphaFollow] = useState(180);
  const [alphaPinned, setAlphaPinned] = useState(250);
  const [cursorShape, setCursorShape] = useState<"arrow" | "text" | "hand">("arrow");
  const [images, setImages] = useState<Record<string, string>>({});
  const imagesRef = useRef(images);
  imagesRef.current = images;
  const [copyChip, setCopyChip] = useState<{ text: string; x: number; y: number } | null>(null);
  const [engine, setEngine] = useState<SearchEngine>("ai");
  const [autostart, setAutostart] = useState(false);
  const [answer, setAnswer] = useState<{ title: string; body: string; x: number; y: number } | null>(null);
  const [picking, setPicking] = useState(false);
  const [pick, setPick] = useState<{ x0: number; y0: number; x1: number; y1: number } | null>(null);
  const pickRef = useRef<{ x0: number; y0: number; x1: number; y1: number } | null>(null);

  useEffect(() => {
    setNotes(readStore(NOTES_KEY, DEFAULT_NOTES));
    setDesk(readStore(DESK_KEY, DEFAULT_DESK));
    setHint(readStore(HINT_KEY, "0") !== "1");
    const stored = readStore(CURSOR_KEY, "k2");
    if (MAINS.some((s) => s.id === stored)) setPicked(stored as MainId);
    const eng = readStore(ENGINE_KEY, "ai");
    if (eng === "ddg" || eng === "yandex" || eng === "wiki" || eng === "ai") setEngine(eng);
    setAutostart(readStore(AUTO_KEY, "0") === "1");
    const mq = window.matchMedia("(pointer: coarse)");
    setCoarse(mq.matches);
    if (mq.matches) setFollowing(false);
    setHydrated(true);
    const onMq = () => setCoarse(mq.matches);
    mq.addEventListener("change", onMq);
    const clock = window.setInterval(() => setNow(new Date()), 1000);
    return () => {
      mq.removeEventListener("change", onMq);
      window.clearInterval(clock);
    };
  }, []);

  useEffect(() => {
    if (!hydrated) return;
    try {
      localStorage.setItem(NOTES_KEY, notes);
    } catch {
      /* ignore quota */
    }
  }, [notes, hydrated]);

  useEffect(() => {
    if (!hydrated) return;
    try {
      localStorage.setItem(DESK_KEY, desk);
    } catch {
      /* ignore quota */
    }
  }, [desk, hydrated]);

  notesRef.current = notes;

  const activeId = hover ?? picked;
  const active = MAINS.find((s) => s.id === activeId) ?? MAINS[0];

  const pickSprite = useCallback((id: MainId) => {
    setPicked(id);
    try {
      localStorage.setItem(CURSOR_KEY, id);
    } catch {
      /* ignore */
    }
  }, []);

  const toggle = useCallback(() => {
    setFollowing((v) => !v);
    setHint(false);
    try {
      localStorage.setItem(HINT_KEY, "1");
    } catch {
      /* ignore */
    }
  }, []);

  const pickEngine = useCallback((id: SearchEngine) => {
    setEngine(id);
    try {
      localStorage.setItem(ENGINE_KEY, id);
    } catch {
      /* ignore */
    }
  }, []);

  const pickAutostart = useCallback((v: boolean) => {
    setAutostart(v);
    try {
      localStorage.setItem(AUTO_KEY, v ? "1" : "0");
    } catch {
      /* ignore */
    }
  }, []);

  const runLookup = useCallback(
    (raw: string) => {
      const q = raw.replace(/\s+/g, " ").trim();
      if (!q) {
        setToast("Нечего искать — скопируйте текст");
        return;
      }
      if (/^\d{4,8}$/.test(q)) return;
      setCopyChip(null);
      const x = cursorPos.current.x;
      const y = cursorPos.current.y;
      setAnswer({ title: engine === "ai" ? "Ищу в интернете…" : "Ищу…", body: q, x, y });
      void lookupBrief(q, engine).then((r) => {
        setAnswer({
          title: r.source || "Выжимка",
          body: r.text,
          x,
          y,
        });
      });
    },
    [engine],
  );

  const insertSnippet = useCallback((n: number) => {
    const line = nthNonEmptyLine(notesRef.current, n);
    if (!line) {
      setToast(`Строки ${n} нет`);
      return;
    }
    const img = /^IMG:(\d+)/.exec(line.trim());
    if (img) {
      const data = imagesRef.current[img[1]];
      if (data) {
        void fetch(data)
          .then((r) => r.blob())
          .then((blob) =>
            navigator.clipboard.write([new ClipboardItem({ [blob.type]: blob })]),
          )
          .then(() => setToast(`Строка ${n} · картинка в буфере`))
          .catch(() => setToast(`Строка ${n} · картинка`));
        return;
      }
    }
    const el = deskRef.current;
    if (el && !coarse) {
      const start = el.selectionStart;
      const end = el.selectionEnd;
      const next = el.value.slice(0, start) + line + el.value.slice(end);
      setDesk(next);
      window.requestAnimationFrame(() => {
        const caret = start + line.length;
        el.focus();
        el.setSelectionRange(caret, caret);
      });
      setToast(`Строка ${n} вставлена`);
      return;
    }
    void navigator.clipboard?.writeText(line).then(
      () => setToast(`Строка ${n} скопирована`),
      () => setToast(`Строка ${n}: ${line}`),
    );
  }, [coarse]);

  useEffect(() => {
    if (!toast) return;
    const id = window.setTimeout(() => setToast(null), 1600);
    return () => window.clearTimeout(id);
  }, [toast]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "F8" || e.code === "F8") {
        e.preventDefault();
        toggle();
        return;
      }
      if (e.key === "F9" || e.code === "F9") {
        e.preventDefault();
        setPadHidden((v) => !v);
        return;
      }
      if (e.key === "F3" || e.code === "F3") {
        e.preventDefault();
        const q = (copyChip?.text || window.getSelection()?.toString() || "").trim();
        runLookup(q);
        return;
      }
      if (e.key === "F6" || e.code === "F6") {
        e.preventDefault();
        setPicking((v) => !v);
        setPick(null);
        return;
      }
      if ((e.key === "Escape" || e.code === "Escape") && picking) {
        e.preventDefault();
        setPicking(false);
        setPick(null);
        return;
      }
      if (!e.ctrlKey || e.altKey || e.metaKey || e.shiftKey) return;
      const m = /^Digit([1-9])$/.exec(e.code);
      if (!m) return;
      e.preventDefault();
      insertSnippet(Number(m[1]));
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [toggle, insertSnippet, copyChip, runLookup, picking]);

  useEffect(() => {
    const onCopy = () => {
      window.setTimeout(() => {
        void navigator.clipboard.readText().then((t) => {
          const text = t.trim();
          if (!text || /^\d{4,8}$/.test(text)) return;
          setCopyChip({
            text: text.slice(0, 180),
            x: cursorPos.current.x,
            y: cursorPos.current.y,
          });
        }).catch(() => {
          const sel = window.getSelection()?.toString().trim();
          if (!sel) return;
          setCopyChip({ text: sel.slice(0, 180), x: cursorPos.current.x, y: cursorPos.current.y });
        });
      }, 40);
    };
    document.addEventListener("copy", onCopy);
    return () => document.removeEventListener("copy", onCopy);
  }, []);

  useEffect(() => {
    if (!copyChip) return;
    const id = window.setTimeout(() => setCopyChip(null), 6000);
    return () => window.clearTimeout(id);
  }, [copyChip]);

  useEffect(() => {
    const onMove = (e: PointerEvent) => {
      cursorPos.current = { x: e.clientX, y: e.clientY };
      live.current = true;
      const el = document.elementFromPoint(e.clientX, e.clientY);
      const texty = Boolean(el?.closest("textarea, input, [contenteditable='true']"));
      const clicky = Boolean(
        el?.closest("a, button, [role='button'], [href], label, summary"),
      );
      const next = texty ? "text" : clicky ? "hand" : "arrow";
      setCursorShape((s) => (s === next ? s : next));
    };
    window.addEventListener("pointermove", onMove);
    return () => window.removeEventListener("pointermove", onMove);
  }, []);

  useEffect(() => {
    const stage = stageRef.current;
    if (!stage) return;
    const onMove = (e: PointerEvent) => {
      const rect = stage.getBoundingClientRect();
      pointer.current = { x: e.clientX - rect.left, y: e.clientY - rect.top };
      live.current = true;
    };
    stage.addEventListener("pointermove", onMove);
    return () => stage.removeEventListener("pointermove", onMove);
  }, []);

  const time = now.toLocaleTimeString("ru-RU", {
    hour: "2-digit",
    minute: "2-digit",
  });

  return (
    <main className="relative flex min-h-dvh flex-col bg-bg text-fg">
      <div className="pointer-events-none absolute inset-0 opacity-40 desk-glow" aria-hidden="true" />

      <header className="relative z-10 flex items-center justify-between gap-4 px-5 pt-5 pb-3 sm:px-8">
        <div className="min-w-0">
          <p className="font-display text-3xl leading-none tracking-tight text-fg sm:text-4xl">
            CursorPad
          </p>
          <p className="mt-1.5 max-w-xl text-sm leading-relaxed text-muted">
            F8 закрепить · F3 мини-ИИ в блокноте (рецепт пельменей и т.п.) · F6 рамка OCR.
          </p>
        </div>
        <div className="hidden shrink-0 items-center gap-2 sm:flex">
          {MAINS.map((sprite) => {
            const on = picked === sprite.id;
            return (
              <button
                key={sprite.id}
                type="button"
                onClick={() => pickSprite(sprite.id)}
                className={cn(
                  "inline-flex h-11 items-center gap-2 rounded-md px-3 text-sm font-medium transition-transform duration-150 ease-out active:scale-[0.96]",
                  on ? "bg-fg text-bg" : "bg-bg-elevated text-fg ring-1 ring-border",
                )}
                aria-pressed={on}
              >
                <img src={sprite.src} alt="" className="size-7 rounded-xs bg-paper object-contain" draggable={false} />
                {sprite.name}
              </button>
            );
          })}
          <a
            href="/CursorPad.exe"
            download="CursorPad.exe"
            className="inline-flex h-11 items-center gap-2 rounded-md bg-fg px-4 text-sm font-medium text-bg transition-transform duration-150 ease-out hover:opacity-90 active:scale-[0.96]"
          >
            <Download className="size-4" strokeWidth={1.75} />
            Скачать .exe
          </a>
          <a
            href="/CursorPad.zip"
            download="CursorPad.zip"
            className="text-sm font-medium text-muted underline-offset-4 transition-colors duration-150 hover:text-fg hover:underline"
          >
            zip
          </a>
        </div>
      </header>

      <section
        ref={stageRef}
        className="relative mx-4 mb-4 flex min-h-0 flex-1 flex-col overflow-hidden rounded-xl bg-desk ring-1 ring-border sm:mx-8 cursor-none"
      >
        <div
          className="pointer-events-none absolute inset-0 opacity-[0.35] mix-blend-overlay desk-grain"
          aria-hidden="true"
        />

        <div className="relative z-10 flex min-h-0 flex-1 flex-col overflow-hidden">
        <div className="relative z-10 grid min-h-0 flex-1 gap-5 overflow-hidden p-4 sm:p-6 lg:grid-cols-[1fr_18rem] lg:p-8">
          <article className="flex min-h-0 flex-col rounded-lg bg-paper text-ink shadow-pad lg:max-w-xl">
            <div className="flex items-center justify-between gap-3 border-b border-paper-line px-5 py-3">
              <div>
                <p className="text-xs font-medium tracking-wide text-ink-muted">
                  {coarse ? "CursorPad" : "Черновик"}
                </p>
                <h1 className="font-display text-xl leading-tight text-ink">
                  {coarse ? "Заметки" : "Рабочая поверхность"}
                </h1>
              </div>
              <span
                className={cn(
                  "rounded-full px-2.5 py-1 text-xs font-medium tabular-nums",
                  following && !coarse ? "bg-sage/15 text-sage" : "bg-ink/10 text-ink-muted",
                )}
              >
                {coarse
                  ? "для Windows"
                  : following
                    ? "можно печатать"
                    : "блокнот закреплён"}
              </span>
            </div>
            <textarea
              ref={coarse ? undefined : deskRef}
              value={coarse ? notes : desk}
              onChange={(e) => (coarse ? setNotes(e.target.value) : setDesk(e.target.value))}
              spellCheck={false}
              suppressHydrationWarning
              className="min-h-0 flex-1 resize-none bg-transparent px-5 py-4 font-sans text-sm leading-relaxed text-ink outline-none placeholder:text-ink-muted/70"
            />
          </article>

          <aside className="hidden flex-col justify-end gap-3 lg:flex">
            <Fact
              icon={MousePointer2}
              title="Следует за курсором"
              body="Окно едет рядом с указателем и не перекрывает клик."
            />
            <Fact
              icon={Keyboard}
              title="Ctrl+1…9 вставляют строку"
              body="Непустая строка N из блокнота вставляется в активное окно. F3 — мини-ИИ внутри программы. F6 — выделить фрагмент экрана."
            />
            <Fact
              icon={Shield}
              title="Без прав администратора"
              body="Портативный .exe, asInvoker. Ничего не ставит в Program Files."
            />
          </aside>
        </div>

        {hydrated && !coarse && !padHidden ? (
          <CursorPad
            following={following}
            notes={notes}
            onNotes={setNotes}
            onToggle={toggle}
            hotkeyLabel={HOTKEY}
            pointerRef={pointer}
            liveRef={live}
            mainId={picked}
            onMain={(id) => pickSprite(id as MainId)}
            choices={MAINS}
            alphaFollow={alphaFollow}
            alphaPinned={alphaPinned}
            onAlphaFollow={setAlphaFollow}
            onAlphaPinned={setAlphaPinned}
            onMin={() => setPadHidden(true)}
            images={images}
            onImages={setImages}
            engine={engine}
            onEngine={pickEngine}
            autostart={autostart}
            onAutostart={pickAutostart}
            onSearch={runLookup}
            onOcr={() => {
              setPicking(true);
              setPick(null);
            }}
          />
        ) : null}

        {padHidden && !coarse ? (
          <button
            type="button"
            onClick={() => setPadHidden(false)}
            className="absolute top-4 right-4 z-40 rounded-md bg-ink px-3 py-2 text-xs font-medium text-paper shadow-pad"
          >
            Показать CursorPad
          </button>
        ) : null}

        {copyChip && !coarse ? (
          <div
            className="fixed z-50 flex items-center gap-2 rounded-md bg-paper px-2 py-1.5 shadow-pad ring-1 ring-paper-line"
            style={{ left: copyChip.x + 16, top: copyChip.y + 18 }}
          >
            <p className="max-w-[9rem] truncate text-[11px] text-ink">{copyChip.text}</p>
            <button
              type="button"
              className="inline-flex h-8 shrink-0 items-center rounded-md bg-ink px-2.5 text-xs font-medium text-paper"
              onClick={() => runLookup(copyChip.text)}
            >
              Спросить · F3
            </button>
            <button type="button" className="size-8 text-ink-muted" onClick={() => setCopyChip(null)} aria-label="Закрыть">
              ×
            </button>
          </div>
        ) : null}

        {answer && !coarse ? (
          <div
            className="fixed z-50 flex w-[min(21rem,calc(100%-24px))] flex-col gap-2 rounded-md bg-paper p-3 shadow-pad ring-1 ring-paper-line"
            style={{ left: Math.min(answer.x + 16, window.innerWidth - 360), top: Math.min(answer.y + 22, window.innerHeight - 220) }}
          >
            <p className="font-display text-sm text-ink">{answer.title}</p>
            <p className="max-h-40 overflow-auto text-xs leading-relaxed text-ink">{answer.body}</p>
            <div className="flex gap-1.5">
              <button
                type="button"
                className="h-8 flex-1 rounded-md bg-ink text-xs font-medium text-paper"
                onClick={() => {
                  void navigator.clipboard.writeText(answer.body);
                  setToast("Выжимка скопирована");
                }}
              >
                Копировать
              </button>
              <button
                type="button"
                className="h-8 flex-1 rounded-md bg-paper-dark text-xs font-medium text-ink"
                onClick={() => {
                  setNotes((n) => `${n.replace(/\s+$/, "")}\n\n${answer.body}`);
                  setToast("Выжимка в блокноте");
                }}
              >
                В блокнот
              </button>
              <button type="button" className="h-8 w-8 rounded-md text-ink-muted" onClick={() => setAnswer(null)} aria-label="Закрыть">
                ×
              </button>
            </div>
          </div>
        ) : null}

        {picking && !coarse ? (
          <div
            className="fixed inset-0 z-[60] cursor-crosshair bg-ink/45"
            onPointerDown={(e) => {
              const next = { x0: e.clientX, y0: e.clientY, x1: e.clientX, y1: e.clientY };
              pickRef.current = next;
              setPick(next);
            }}
            onPointerMove={(e) => {
              const cur = pickRef.current;
              if (!cur || e.buttons === 0) return;
              const next = { ...cur, x1: e.clientX, y1: e.clientY };
              pickRef.current = next;
              setPick(next);
            }}
            onPointerUp={() => {
              const cur = pickRef.current;
              pickRef.current = null;
              setPicking(false);
              setPick(null);
              if (!cur) return;
              const w = Math.abs(cur.x1 - cur.x0);
              const h = Math.abs(cur.y1 - cur.y0);
              if (w < 12 || h < 12) {
                setToast("OCR: область слишком маленькая");
                return;
              }
              const sel = window.getSelection()?.toString().trim();
              const clip = sel || "В CursorPad.exe читает выделенный фрагмент экрана";
              setNotes((n) => `${n.replace(/\s+$/, "")}\n\nOCR: ${clip}`);
              setToast("OCR: текст добавлен в блокнот");
            }}
          >
            <p className="absolute top-5 left-5 text-sm text-paper">Выделите фрагмент · Esc отмена</p>
            {pick ? (
              <div
                className="absolute bg-paper/80 ring-2 ring-sage"
                style={{
                  left: Math.min(pick.x0, pick.x1),
                  top: Math.min(pick.y0, pick.y1),
                  width: Math.abs(pick.x1 - pick.x0),
                  height: Math.abs(pick.y1 - pick.y0),
                }}
              />
            ) : null}
          </div>
        ) : null}

        {toast ? (
          <div className="pointer-events-none absolute top-4 left-1/2 z-40 w-[min(22rem,calc(100%-32px))] -translate-x-1/2 rounded-md bg-ink px-4 py-3 text-center text-sm leading-relaxed text-paper shadow-pad">
            {toast}
          </div>
        ) : hint && !coarse ? (
          <div className="pointer-events-none absolute top-4 left-1/2 z-40 w-[min(22rem,calc(100%-32px))] -translate-x-1/2 rounded-md bg-ink px-4 py-3 text-center text-sm leading-relaxed text-paper shadow-pad">
            Спрайты: F8 закрепить блокнот, внизу Мечник / Рукавица.
          </div>
        ) : null}
        </div>

        <div className="relative z-40 shrink-0 border-t border-border bg-bg-elevated/90 p-2.5 sm:p-3">
          <div className="mb-2 flex items-end justify-between gap-3 px-0.5">
            <p className="text-xs font-medium text-muted">
              основной курсор · одинаковая анимация загрузки
            </p>
            <p className="text-xs text-subtle">
              сейчас: {active.name}
            </p>
          </div>
          <div className="grid grid-cols-2 gap-2">
            {MAINS.map((sprite) => {
              const on = picked === sprite.id;
              const over = hover === sprite.id;
              return (
                <button
                  key={sprite.id}
                  type="button"
                  data-sprite-cursor=""
                  onClick={() => pickSprite(sprite.id)}
                  onPointerEnter={() => setHover(sprite.id)}
                  onPointerLeave={() => setHover((h) => (h === sprite.id ? null : h))}
                  className={cn(
                    "flex min-h-20 items-center gap-3 overflow-hidden rounded-md bg-desk px-2 py-2 text-left ring-1 transition-[box-shadow,transform] duration-150 ease-out sm:px-3",
                    on || over ? "ring-border-strong" : "ring-border",
                    on ? "shadow-pad" : "hover:ring-border-strong",
                    "active:scale-[0.99]",
                  )}
                  aria-pressed={on}
                  aria-label={`Основной: ${sprite.name}`}
                >
                  <span className="flex size-16 shrink-0 items-center justify-center rounded-sm bg-paper sm:size-20">
                    <img
                      src={sprite.src}
                      alt=""
                      draggable={false}
                      className="size-full object-contain"
                    />
                  </span>
                  <span className="min-w-0">
                    <span className="block font-display text-lg leading-tight text-fg">
                      {sprite.name}
                    </span>
                    <span className="mt-0.5 block text-xs text-muted">
                      {on ? "основной" : "нажмите, чтобы сделать основным"}
                    </span>
                  </span>
                </button>
              );
            })}
          </div>
          <div className="mt-2 flex flex-wrap gap-1.5">
            {(
              [
                ["стрелка", active.src],
                ["текст", active.ibeam],
                ["ссылка", active.hand],
                ["загрузка", active.frames[0]],
                ["запрет", `/cursors/${active.id}-no.png`],
                ["справка", `/cursors/${active.id}-help.png`],
                ["вверх", `/cursors/${active.id}-up.png`],
              ] as const
            ).map(([label, src]) => (
              <span
                key={label}
                className="inline-flex items-center gap-1.5 rounded-sm bg-paper px-1.5 py-1 text-xs text-ink"
              >
                <img src={src} alt="" className="size-6 object-contain" draggable={false} />
                {label}
              </span>
            ))}
          </div>
        </div>
      </section>

      <footer className="relative z-10 flex flex-wrap items-center justify-between gap-3 px-5 pb-[max(1rem,env(safe-area-inset-bottom))] sm:px-8">
        <div className="flex flex-wrap items-center gap-2">
          {coarse ? (
            <a
              href="/CursorPad.exe"
              download="CursorPad.exe"
              className="inline-flex h-11 items-center gap-2 rounded-md bg-fg px-4 text-sm font-medium text-bg transition-transform duration-150 ease-out hover:opacity-90 active:scale-[0.96]"
            >
              <Download className="size-4" strokeWidth={1.75} />
              Скачать .exe
            </a>
          ) : (
            <button
              type="button"
              onClick={toggle}
              className="inline-flex h-11 items-center gap-2 rounded-md bg-fg px-4 text-sm font-medium text-bg transition-transform duration-150 ease-out hover:opacity-90 active:scale-[0.96]"
            >
              {following ? (
                <Pin className="size-4" strokeWidth={1.75} />
              ) : (
                <MousePointer2 className="size-4" strokeWidth={1.75} />
              )}
              {following ? `Закрепить · ${HOTKEY}` : `Следовать · ${HOTKEY}`}
            </button>
          )}
        </div>
        <p className="text-xs text-subtle">
          <span className="tabular-nums">{time}</span>
          <span className="mx-2 text-border-strong">·</span>
          {following && !coarse ? "режим следования" : "закреплено"}
          <span className="mx-2 text-border-strong">·</span>
          без прав администратора
        </p>
      </footer>
      {!coarse && hydrated && !picking ? (
        <LiveCursor
          key={active.id}
          src={
            cursorShape === "text"
              ? active.ibeam
              : cursorShape === "hand"
                ? active.hand
                : active.src
          }
          frames={active.frames}
          busy={Boolean(toast)}
          hotx={
            cursorShape === "text"
              ? active.ibeamHot[0]
              : cursorShape === "hand"
                ? active.handHot[0]
                : active.hotx
          }
          hoty={
            cursorShape === "text"
              ? active.ibeamHot[1]
              : cursorShape === "hand"
                ? active.handHot[1]
                : active.hoty
          }
          pointerRef={cursorPos}
          liveRef={live}
        />
      ) : null}
    </main>
  );
}

function Fact({
  icon: Icon,
  title,
  body,
}: {
  icon: typeof MousePointer2;
  title: string;
  body: string;
}) {
  return (
    <div className="rounded-md bg-bg-subtle px-4 py-3 ring-1 ring-border">
      <div className="flex items-center gap-2 text-fg">
        <Icon className="size-4 text-sage" strokeWidth={1.75} />
        <p className="text-sm font-medium">{title}</p>
      </div>
      <p className="mt-1.5 text-xs leading-relaxed text-muted">{body}</p>
    </div>
  );
}

function LiveCursor({
  src,
  frames,
  busy,
  hotx,
  hoty,
  pointerRef,
  liveRef,
}: {
  src: string;
  frames: readonly string[];
  busy: boolean;
  hotx: number;
  hoty: number;
  pointerRef: MutableRefObject<{ x: number; y: number }>;
  liveRef: MutableRefObject<boolean>;
}) {
  const elRef = useRef<HTMLImageElement>(null);
  const [frame, setFrame] = useState(0);

  useEffect(() => {
    if (!busy) {
      setFrame(0);
      return;
    }
    const id = window.setInterval(() => {
      setFrame((n) => (n + 1) % frames.length);
    }, 90);
    return () => window.clearInterval(id);
  }, [busy, frames.length]);

  useEffect(() => {
    let raf = 0;
    const tick = () => {
      const el = elRef.current;
      if (el) {
        const { x, y } = pointerRef.current;
        el.style.transform = `translate3d(${x - hotx}px, ${y - hoty}px, 0)`;
        el.style.opacity = liveRef.current ? "1" : "0.85";
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [hotx, hoty, pointerRef, liveRef]);

  return (
    <img
      ref={elRef}
      src={busy ? frames[frame] ?? src : src}
      alt=""
      draggable={false}
      className="pointer-events-none fixed top-0 left-0 z-50 size-12 will-change-transform"
    />
  );
}
