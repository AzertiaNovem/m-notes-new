/**
 * Preprocess LaTeX Markdown so remark-math (web) and marked+KaTeX (print)
 * see the same dollar delimiters.
 *
 * remark-math only understands `$...$` / `$$...$$`. CommonMark treats `\[`
 * as an escaped `[`, so `\[ \angle ... \]` and bracket blocks like
 * `[ \n \angle ... \n ]` otherwise print as raw TeX. Unicode subscripts
 * such as H₂O pick up a wide CJK glyph; lift those (and ASCII H_2O) into
 * `$H_2O$` so KaTeX typesets the subscript.
 */

const SUBSCRIPTS: Record<string, string> = {
  "₀": "0",
  "₁": "1",
  "₂": "2",
  "₃": "3",
  "₄": "4",
  "₅": "5",
  "₆": "6",
  "₇": "7",
  "₈": "8",
  "₉": "9",
  "₊": "+",
  "₋": "-",
  "₍": "(",
  "₎": ")",
};

const SUPERSCRIPTS: Record<string, string> = {
  "⁰": "0",
  "¹": "1",
  "²": "2",
  "³": "3",
  "⁴": "4",
  "⁵": "5",
  "⁶": "6",
  "⁷": "7",
  "⁸": "8",
  "⁹": "9",
  "⁺": "+",
  "⁻": "-",
  "⁽": "(",
  "⁾": ")",
};

const UNICODE_SCRIPT_RE = /[\u2070-\u207F\u2080-\u208F]/;
const UNICODE_FORMULA_RE =
  /^[A-Za-z][A-Za-z0-9]*[\u2070-\u207F\u2080-\u208F][A-Za-z0-9\u2070-\u207F\u2080-\u208F]*/;
const UNDERSCORE_FORMULA_RE = /^(?:[A-Z][a-z]?_\d+[A-Za-z]*)+/;

export function looksLikeTex(value: string): boolean {
  const text = value.trim();
  if (!text) return false;
  if (/\\[a-zA-Z]+/.test(text)) return true;
  if (/\^(\{|\d|\\)/.test(text)) return true;
  return false;
}

function isTexDisplayBlock(inner: string): boolean {
  if (!looksLikeTex(inner)) return false;
  if (inner.length > 4000) return false;
  if (/^[ \t]*([-*+] |\d+\. |#{1,6} |> )/m.test(inner)) return false;
  return true;
}

function nextUnescaped(src: string, needle: string, from: number): number {
  let i = from;
  while (i < src.length) {
    const j = src.indexOf(needle, i);
    if (j === -1) return -1;
    let slashes = 0;
    for (let k = j - 1; k >= 0 && src[k] === "\\"; k -= 1) slashes += 1;
    if (slashes % 2 === 0) return j;
    i = j + needle.length;
  }
  return -1;
}

function matchingBrace(src: string, open: number): number {
  if (src[open] !== "{") return -1;
  let depth = 0;
  for (let i = open; i < src.length; i += 1) {
    const ch = src[i];
    if (ch === "\\" && i + 1 < src.length) {
      i += 1;
      continue;
    }
    if (ch === "{") depth += 1;
    else if (ch === "}") {
      depth -= 1;
      if (depth === 0) return i;
    }
  }
  return -1;
}

function isLineStart(src: string, i: number): boolean {
  return i === 0 || src[i - 1] === "\n";
}

function tokenToTex(token: string): string {
  let out = "";
  let buf = "";
  let mode: "n" | "sub" | "sup" = "n";
  const flush = () => {
    if (!buf) return;
    if (mode === "sub") out += buf.length === 1 ? `_${buf}` : `_{${buf}}`;
    else if (mode === "sup") out += buf.length === 1 ? `^${buf}` : `^{${buf}}`;
    else out += buf;
    buf = "";
  };
  for (const ch of token) {
    const sub = SUBSCRIPTS[ch];
    const sup = SUPERSCRIPTS[ch];
    const next: "n" | "sub" | "sup" = sub !== undefined ? "sub" : sup !== undefined ? "sup" : "n";
    const mapped = sub ?? sup ?? ch;
    if (next !== mode) {
      flush();
      mode = next;
    }
    buf += mapped;
  }
  flush();
  return out;
}

function wrapFormulaToken(token: string): string {
  return `$${tokenToTex(token)}$`;
}

function wrapDisplay(inner: string): string {
  const body = inner.replace(/^\n+|\n+$/g, "");
  return `$$\n${body}\n$$`;
}

function consumeFence(src: string, i: number): number {
  const fence = src.startsWith("```", i) ? "```" : src.startsWith("~~~", i) ? "~~~" : "";
  if (!fence) return -1;
  const end = src.indexOf(fence, i + fence.length);
  return end === -1 ? -1 : end + fence.length;
}

function consumeInlineCode(src: string, i: number): number {
  if (src[i] !== "`") return -1;
  let n = 0;
  while (src[i + n] === "`") n += 1;
  const fence = src.slice(i, i + n);
  const end = src.indexOf(fence, i + n);
  return end === -1 ? -1 : end + n;
}

function consumeLineBracketTex(src: string, i: number): { end: number; inner: string } | null {
  if (!isLineStart(src, i)) return null;
  const open = src.slice(i).match(/^[ \t]*\[[ \t]*\n/);
  if (!open) return null;
  const innerStart = i + open[0].length;
  const closeRe = /\n[ \t]*\][ \t]*(?=\n|$)/g;
  closeRe.lastIndex = 0;
  const rest = src.slice(innerStart);
  const close = closeRe.exec(rest);
  if (!close) return null;
  const inner = rest.slice(0, close.index);
  if (!isTexDisplayBlock(inner)) return null;
  return { end: innerStart + close.index + close[0].length, inner };
}

function consumeInlineBracketTex(src: string, i: number): { end: number; inner: string } | null {
  if (src[i] !== "[") return null;
  if (src[i + 1] === "^") return null;
  const close = src.indexOf("]", i + 1);
  if (close === -1) return null;
  const inner = src.slice(i + 1, close);
  if (inner.includes("\n")) return null;
  const after = src[close + 1];
  if (after === "(" || after === "[") return null;
  if (!looksLikeTex(inner)) return null;
  if (inner.length > 500) return null;
  return { end: close + 1, inner: inner.trim() };
}

/**
 * Rewrite stored LaTeX Markdown into `$` / `$$` form without touching code
 * fences or math that is already delimited.
 */
export function normalizeMarkdownMath(markdown: string): string {
  const src = markdown ?? "";
  let i = 0;
  let out = "";

  while (i < src.length) {
    const fenceEnd = consumeFence(src, i);
    if (fenceEnd !== -1) {
      out += src.slice(i, fenceEnd);
      i = fenceEnd;
      continue;
    }

    const codeEnd = consumeInlineCode(src, i);
    if (codeEnd !== -1) {
      out += src.slice(i, codeEnd);
      i = codeEnd;
      continue;
    }

    if (src.startsWith("$$", i)) {
      const end = nextUnescaped(src, "$$", i + 2);
      if (end !== -1) {
        out += src.slice(i, end + 2);
        i = end + 2;
        continue;
      }
    }

    if (src.startsWith("\\[", i)) {
      const end = src.indexOf("\\]", i + 2);
      if (end !== -1) {
        out += wrapDisplay(src.slice(i + 2, end));
        i = end + 2;
        continue;
      }
    }

    if (src.startsWith("\\(", i)) {
      const end = src.indexOf("\\)", i + 2);
      if (end !== -1) {
        out += `$${src.slice(i + 2, end)}$`;
        i = end + 2;
        continue;
      }
    }

    if (src[i] === "$" && src[i + 1] !== "$") {
      const end = nextUnescaped(src, "$", i + 1);
      if (end !== -1) {
        out += src.slice(i, end + 1);
        i = end + 1;
        continue;
      }
    }

    if (src.startsWith("\\ce{", i)) {
      const close = matchingBrace(src, i + 3);
      if (close !== -1) {
        out += `$${src.slice(i, close + 1)}$`;
        i = close + 1;
        continue;
      }
    }

    const lineBracket = consumeLineBracketTex(src, i);
    if (lineBracket) {
      out += wrapDisplay(lineBracket.inner);
      i = lineBracket.end;
      continue;
    }

    const inlineBracket = consumeInlineBracketTex(src, i);
    if (inlineBracket) {
      out += `$${inlineBracket.inner}$`;
      i = inlineBracket.end;
      continue;
    }

    const prev = i > 0 ? src[i - 1] : "";
    if (!/[A-Za-z0-9]/.test(prev)) {
      const rest = src.slice(i);
      const unicode = rest.match(UNICODE_FORMULA_RE);
      if (unicode && UNICODE_SCRIPT_RE.test(unicode[0])) {
        out += wrapFormulaToken(unicode[0]);
        i += unicode[0].length;
        continue;
      }
      const underscore = rest.match(UNDERSCORE_FORMULA_RE);
      if (underscore) {
        out += wrapFormulaToken(underscore[0]);
        i += underscore[0].length;
        continue;
      }
    }

    out += src[i];
    i += 1;
  }

  return out;
}
