/** Strip script / event handlers / foreignObject / external hrefs. Not a full HTML engine. */

const SCRIPT_RE = /<script\b[^>]*>[\s\S]*?<\/script>/gi;
const SCRIPT_EMPTY_RE = /<script\b[^>]*\/?>/gi;
const FOREIGN_RE = /<foreignObject\b[^>]*>[\s\S]*?<\/foreignObject>/gi;
const FOREIGN_EMPTY_RE = /<foreignObject\b[^>]*\/?>/gi;
const HANDLER_RE = /\son[a-z]+\s*=\s*("[^"]*"|'[^']*'|[^\s>]+)/gi;
const HREF_RE = /((?:xlink:)?href)\s*=\s*(["'])([\s\S]*?)\2/gi;
const HREF_UNQUOTED_RE = /((?:xlink:)?href)\s*=\s*([^\s>]+)/gi;

function isSafeHref(value: string): boolean {
  const v = value.trim();
  if (!v || v.startsWith("#")) return true;
  return false;
}

export function sanitizeSvg(raw: string): string {
  let svg = (raw ?? "").trim();
  if (!svg) return "";
  svg = svg.replace(SCRIPT_RE, "").replace(SCRIPT_EMPTY_RE, "");
  svg = svg.replace(FOREIGN_RE, "").replace(FOREIGN_EMPTY_RE, "");
  svg = svg.replace(HANDLER_RE, "");
  svg = svg.replace(HREF_RE, (full, attr: string, quote: string, value: string) => {
    if (isSafeHref(value)) return full;
    return `${attr}=${quote}${quote}`;
  });
  svg = svg.replace(HREF_UNQUOTED_RE, (full, attr: string, value: string) => {
    if (value.startsWith('"') || value.startsWith("'")) return full;
    if (isSafeHref(value)) return full;
    return `${attr}=""`;
  });
  return svg.trim();
}
