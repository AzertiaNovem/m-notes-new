export const HIGHLIGHT_COLORS = ["yellow", "red", "green", "blue"] as const;
export type HighlightColor = (typeof HIGHLIGHT_COLORS)[number];

const COLOR_ALIAS: Record<string, HighlightColor> = {
  yellow: "yellow",
  red: "red",
  green: "green",
  blue: "blue",
  黄: "yellow",
  红: "red",
  绿: "green",
  蓝: "blue",
};

export type HighlightMatch = {
  raw: string;
  color: HighlightColor;
  body: string;
};

const OPEN_RE = /^==(?:([A-Za-z\u4e00-\u9fff]+):)?/;

export function normalizeHighlightColor(name: string | undefined): HighlightColor | null {
  if (!name) return "yellow";
  return Object.hasOwn(COLOR_ALIAS, name) ? COLOR_ALIAS[name] : null;
}

/** Read one `==text==` or `==red:text==` marker at the start of `src`. */
export function readHighlight(src: string): HighlightMatch | null {
  if (!src.startsWith("==")) return null;
  const open = src.match(OPEN_RE);
  if (!open) return null;
  if (open[1] && !normalizeHighlightColor(open[1])) return null;
  const bodyStart = open[0].length;
  const close = src.indexOf("==", bodyStart);
  if (close === -1) return null;
  const body = src.slice(bodyStart, close);
  if (!body.trim() || /[\r\n]/.test(body)) return null;
  const color = normalizeHighlightColor(open[1]);
  if (!color) return null;
  return { raw: src.slice(0, close + 2), color, body };
}
