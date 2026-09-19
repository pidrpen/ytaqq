import { useEffect, useRef, useState, type MutableRefObject } from "react";
import { Pin, MousePointer2, X, Minus, ScanText, Settings2 } from "lucide-react";
import { cn } from "@/lib/utils";

const PAD_W = 300;
const PAD_H = 228;
const OFFSET_X = 18;
const OFFSET_Y = 22;
const LERP = 0.22;

type CursorChoice = {
  id: string;
  name: string;
  src: string;
};

type CursorPadProps = {
  following: boolean;
  notes: string;
  onNotes: (value: string) => void;
  onToggle: () => void;
  onClose?: () => void;
  hotkeyLabel: string;
  pointerRef: MutableRefObject<{ x: number; y: number }>;
  liveRef: MutableRefObject<boolean>;
  mainId: string;
  onMain: (id: string) => void;
  choices: readonly CursorChoice[];
  alphaFollow: number;
  alphaPinned: number;
  onAlphaFollow: (n: number) => void;
  onAlphaPinned: (n: number) => void;
  onMin: () => void;
  onOcr: () => void;
  images: Record<string, string>;
  onImages: (next: Record<string, string>) => void;
  engine: "wiki" | "ddg" | "yandex" | "ai" | "plm";
  onEngine: (e: "wiki" | "ddg" | "yandex" | "ai" | "plm") => void;
  autostart: boolean;
  onAutostart: (v: boolean) => void;
  onSearch: (q: string) => void;
  clipBuf: string;
  onClipBuf: (v: string) => void;
};

export function CursorPad({
  following,
  notes,
  onNotes,
  onToggle,
  onClose,
  hotkeyLabel,
  pointerRef,
  liveRef,
  mainId,
  onMain,
  choices,
  alphaFollow,
  alphaPinned,
  onAlphaFollow,
  onAlphaPinned,
  onMin,
  onOcr,
  images,
  onImages,
  engine,
  onEngine,
  autostart,
  onAutostart,
  onSearch,
  clipBuf,
  onClipBuf,
}: CursorPadProps) {
  const [settings, setSettings] = useState(false);
  const elRef = useRef<HTMLDivElement>(null);
  const pos = useRef({ x: 48, y: 96 });
  const placed = useRef(false);
  const drag = useRef<{ dx: number; dy: number } | null>(null);
  const textareaRef = useRef<HTMLTextAreaElement>(null);

  useEffect(() => {
    let raf = 0;
    const tick = () => {
      const el = elRef.current;
      if (!el) {
        raf = requestAnimationFrame(tick);
        return;
      }
      const parent = el.offsetParent as HTMLElement | null;
      const bounds = parent
        ? { w: parent.clientWidth, h: parent.clientHeight }
        : { w: window.innerWidth, h: window.innerHeight };

      if (!placed.current && bounds.w > 0 && bounds.h > 0) {
        placed.current = true;
        if (bounds.w < 720) {
          pos.current.x = 12;
          pos.current.y = Math.max(12, bounds.h - Math.min(PAD_H, bounds.h * 0.62) - 12);
        } else {
          pos.current.x = Math.min(bounds.w - PAD_W - 20, Math.max(280, bounds.w * 0.52));
          pos.current.y = 64;
        }
      }

      if (following && liveRef.current && !drag.current) {
        const pointer = pointerRef.current;
        let tx = pointer.x + OFFSET_X;
        let ty = pointer.y + OFFSET_Y;
        if (tx + PAD_W > bounds.w - 12) tx = pointer.x - PAD_W - 16;
        if (ty + PAD_H > bounds.h - 12) ty = pointer.y - PAD_H - 16;
        tx = Math.max(8, Math.min(tx, bounds.w - PAD_W - 8));
        ty = Math.max(8, Math.min(ty, bounds.h - PAD_H - 8));
        pos.current.x += (tx - pos.current.x) * LERP;
        pos.current.y += (ty - pos.current.y) * LERP;
      }

      el.style.transform = `translate3d(${pos.current.x}px, ${pos.current.y}px, 0)`;
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [following, pointerRef, liveRef]);

  useEffect(() => {
    if (!following) {
      const id = window.setTimeout(() => textareaRef.current?.focus(), 40);
      return () => window.clearTimeout(id);
    }
  }, [following]);

  const onTitlePointerDown = (e: React.PointerEvent) => {
    if (following) return;
    if ((e.target as HTMLElement).closest("button")) return;
    const el = elRef.current;
    if (!el) return;
    el.setPointerCapture(e.pointerId);
    drag.current = { dx: e.clientX - pos.current.x, dy: e.clientY - pos.current.y };
  };

  const onTitlePointerMove = (e: React.PointerEvent) => {
    if (!drag.current) return;
    pos.current.x = e.clientX - drag.current.dx;
    pos.current.y = e.clientY - drag.current.dy;
  };

  const onTitlePointerUp = (e: React.PointerEvent) => {
    if (!drag.current) return;
    drag.current = null;
    try {
      elRef.current?.releasePointerCapture(e.pointerId);
    } catch {
      /* already released */
    }
  };

  return (
    <div
      ref={elRef}
      className={cn(
        "absolute top-0 left-0 z-30 flex flex-col bg-paper text-ink",
        "w-[300px] max-w-[calc(100%-24px)] h-[196px] rounded-2xl shadow-pad",
        settings && !following ? "overflow-visible" : "overflow-hidden",
        following
          ? "pointer-events-none"
          : "pointer-events-auto shadow-pad-pinned",
      )}
      style={{
        opacity: (following ? alphaFollow : alphaPinned) / 255,
        transition: "opacity 180ms cubic-bezier(0.22, 1, 0.36, 1)",
      }}
      aria-label="CursorPad"
    >
      <div className="absolute inset-y-0 left-0 w-1 bg-sage" aria-hidden="true" />
      <header
        className={cn(
          "flex h-12 shrink-0 items-center gap-2 bg-paper-dark pr-2 pl-4",
          !following && "cursor-grab active:cursor-grabbing",
        )}
        onPointerDown={onTitlePointerDown}
        onPointerMove={onTitlePointerMove}
        onPointerUp={onTitlePointerUp}
        onPointerCancel={onTitlePointerUp}
      >
        <p className="min-w-0 flex-1 font-display text-lg leading-none tracking-tight text-ink">
          CursorPad
        </p>
        <button
          type="button"
          onClick={onToggle}
          disabled={following}
          className="inline-flex h-9 items-center gap-1.5 rounded-md bg-ink px-2.5 text-xs font-medium text-paper transition-transform duration-150 ease-out enabled:hover:opacity-90 enabled:active:scale-[0.96] disabled:opacity-40"
        >
          {following ? (
            <MousePointer2 className="size-3.5" strokeWidth={1.75} />
          ) : (
            <Pin className="size-3.5" strokeWidth={1.75} />
          )}
          <span>{following ? "Следует" : "Закреплено"}</span>
        </button>
        <button
          type="button"
          onClick={onMin}
          disabled={following}
          className="inline-flex size-9 items-center justify-center rounded-md text-ink-muted transition-colors duration-150 hover:bg-ink/10 hover:text-ink enabled:active:scale-[0.96] disabled:opacity-40"
          aria-label="Свернуть"
        >
          <Minus className="size-4" strokeWidth={1.75} />
        </button>
        {onClose ? (
          <button
            type="button"
            onClick={onClose}
            disabled={following}
            className="inline-flex size-9 items-center justify-center rounded-md text-ink-muted transition-colors duration-150 hover:bg-ink/10 hover:text-ink enabled:active:scale-[0.96] disabled:opacity-40"
            aria-label="Скрыть"
          >
            <X className="size-4" strokeWidth={1.75} />
          </button>
        ) : null}
      </header>

      <input
        value={clipBuf}
        onChange={(e) => onClipBuf(e.target.value)}
        readOnly={following}
        spellCheck={false}
        placeholder="буфер копии · F3"
        className="h-7 shrink-0 border-b border-paper-line bg-paper-dark px-2 font-sans text-[12px] text-ink outline-none placeholder:text-ink-muted/70"
      />

      <div className="flex min-h-0 flex-1">
        <div className="flex w-7 shrink-0 flex-col gap-0.5 bg-paper-dark/60 py-1 pl-0.5">
          {notes.split(/\n/).slice(0, 5).map((line, i) => {
            const m = line.match(/^IMG:(\d+)/);
            const src = m ? images[m[1]] : null;
            return src ? (
              <img key={i} src={src} alt="" className="size-5 rounded-[2px] object-cover" />
            ) : (
              <span key={i} className="h-5" />
            );
          })}
        </div>
        <textarea
          ref={textareaRef}
          value={notes}
          onChange={(e) => onNotes(e.target.value)}
          onPaste={(e) => {
            const file = [...e.clipboardData.items].find((it) => it.type.startsWith("image/"));
            if (!file) return;
            e.preventDefault();
            const blob = file.getAsFile();
            if (!blob) return;
            const id = String(Date.now() % 90000);
            const reader = new FileReader();
            reader.onload = () => {
              onImages({ ...images, [id]: String(reader.result) });
              const ta = textareaRef.current;
              const start = ta?.selectionStart ?? notes.length;
              const before = notes.slice(0, start);
              const lineStart = before.lastIndexOf("\n") + 1;
              const after = notes.slice(start);
              const lineEndRel = after.indexOf("\n");
              const lineEnd = lineEndRel < 0 ? notes.length : start + lineEndRel;
              onNotes(`${notes.slice(0, lineStart)}IMG:${id}${notes.slice(lineEnd)}`);
            };
            reader.readAsDataURL(blob);
          }}
          readOnly={following}
          spellCheck={false}
          suppressHydrationWarning
          placeholder="5 строк · Ctrl+V картинку"
          className="min-h-0 flex-1 resize-none bg-transparent px-2 py-1.5 font-sans text-sm leading-5 text-ink outline-none placeholder:text-ink-muted/70"
        />
      </div>

      <footer className="flex shrink-0 items-center gap-2 bg-paper-dark px-2 py-1.5">
        <button
          type="button"
          disabled={following}
          onClick={() => setSettings((v) => !v)}
          className="inline-flex h-8 flex-1 items-center justify-center gap-1.5 rounded-md bg-paper text-xs font-medium text-ink ring-1 ring-paper-line enabled:active:scale-[0.98] disabled:opacity-40"
        >
          <Settings2 className="size-3.5" strokeWidth={1.75} />
          Настройки
        </button>
        <span className="text-[10px] tracking-wide text-ink-muted">F9</span>
      </footer>

      {settings && !following ? (
        <div className="absolute inset-x-0 top-full z-20 mt-1 flex flex-col gap-2 rounded-md bg-paper p-3 shadow-pad ring-1 ring-paper-line">
          <form
            className="flex gap-1.5"
            onSubmit={(e) => {
              e.preventDefault();
              const q = (e.currentTarget.elements.namedItem("q") as HTMLInputElement | null)?.value?.trim();
              if (q) onSearch(q);
            }}
          >
            <input
              name="q"
              placeholder={engine === "plm" ? "масло И-12А ГОСТ 20799-2022" : "рецепт пельменей"}
              className="h-8 min-w-0 flex-1 rounded-md bg-paper-dark px-2 text-xs text-ink outline-none ring-1 ring-paper-line"
            />
            <button type="submit" className="h-8 rounded-md bg-ink px-2 text-xs text-paper">
              Спросить
            </button>
          </form>
          <button
            type="button"
            onClick={() => onEngine("ai")}
            className={cn(
              "h-8 rounded-md text-xs font-medium",
              engine === "ai" ? "bg-ink text-paper" : "bg-paper-dark text-ink",
            )}
          >
            {engine === "ai" ? "● Мини-ИИ в программе" : "Мини-ИИ в программе"}
          </button>
          <div className="grid grid-cols-2 gap-1.5">
            {(
              [
                ["wiki", "Вики"],
                ["ddg", "DDG"],
                ["yandex", "Яндекс"],
                ["plm", "PLM"],
              ] as const
            ).map(([id, label]) => (
              <button
                key={id}
                type="button"
                onClick={() => onEngine(id)}
                className={cn(
                  "h-8 rounded-md px-1 text-xs font-medium",
                  engine === id ? "bg-ink text-paper" : "bg-paper-dark text-ink",
                )}
              >
                {engine === id ? `● ${label}` : label}
              </button>
            ))}
          </div>
          <div className="grid grid-cols-2 gap-1.5">
            {choices.map((c) => {
              const on = c.id === mainId;
              return (
                <button
                  key={c.id}
                  type="button"
                  onClick={() => onMain(c.id)}
                  className={cn(
                    "flex h-10 items-center gap-2 rounded-md px-2 text-left text-xs font-medium",
                    on ? "bg-ink text-paper" : "bg-paper-dark text-ink",
                  )}
                >
                  <img src={c.src} alt="" className="size-6 object-contain" />
                  {c.name}
                </button>
              );
            })}
          </div>
          <label className="flex flex-col gap-1 text-[11px] text-ink-muted">
            фон {Math.round((alphaFollow / 255) * 100)}%
            <input type="range" min={40} max={255} value={alphaFollow} onChange={(e) => onAlphaFollow(Number(e.target.value))} className="w-full accent-sage" />
          </label>
          <label className="flex flex-col gap-1 text-[11px] text-ink-muted">
            окно {Math.round((alphaPinned / 255) * 100)}%
            <input type="range" min={40} max={255} value={alphaPinned} onChange={(e) => onAlphaPinned(Number(e.target.value))} className="w-full accent-sage" />
          </label>
          <label className="flex items-center gap-2 text-xs text-ink">
            <input
              type="checkbox"
              checked={autostart}
              onChange={(e) => onAutostart(e.target.checked)}
              className="size-3.5 accent-sage"
            />
            Автозапуск с Windows
          </label>
          <button type="button" onClick={onOcr} className="inline-flex h-8 items-center justify-center gap-1.5 rounded-md bg-ink text-xs text-paper">
            <ScanText className="size-3.5" />
            Выделить и прочитать
          </button>
        </div>
      ) : null}
    </div>
  );
}
