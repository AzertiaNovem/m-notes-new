export type LineHunkKind = "equal" | "add" | "remove" | "replace";

export type LineHunk = {
  kind: LineHunkKind;
  left: string[];
  right: string[];
};

const MAX_DP_CELLS = 400_000;

/** Split on newlines, but keep `$...$` / `$$...$$` blocks in one unit when they span lines. */
export function splitMarkdownLines(text: string): string[] {
  if (text.length === 0) return [];
  const raw = text.split("\n");
  const out: string[] = [];
  let buf: string[] = [];
  let display = false;
  let inline = false;

  const feed = (line: string) => {
    let i = 0;
    while (i < line.length) {
      const prev = i > 0 ? line[i - 1] : "";
      if (prev === "\\") {
        i += 1;
        continue;
      }
      if (!inline && line.startsWith("$$", i)) {
        display = !display;
        i += 2;
        continue;
      }
      if (!display && line[i] === "$") {
        inline = !inline;
        i += 1;
        continue;
      }
      i += 1;
    }
  };

  for (const line of raw) {
    buf.push(line);
    feed(line);
    if (!display && !inline) {
      out.push(buf.join("\n"));
      buf = [];
    }
  }
  if (buf.length) out.push(buf.join("\n"));
  return out;
}

export function diffLines(oldLines: string[], newLines: string[]): LineHunk[] {
  let start = 0;
  const oldLen = oldLines.length;
  const newLen = newLines.length;
  while (start < oldLen && start < newLen && oldLines[start] === newLines[start]) start += 1;
  let oldEnd = oldLen;
  let newEnd = newLen;
  while (oldEnd > start && newEnd > start && oldLines[oldEnd - 1] === newLines[newEnd - 1]) {
    oldEnd -= 1;
    newEnd -= 1;
  }

  const prefix = oldLines.slice(0, start);
  const suffix = oldLines.slice(oldEnd);
  const a = oldLines.slice(start, oldEnd);
  const b = newLines.slice(start, newEnd);

  const middle = diffMiddle(a, b);
  const hunks: LineHunk[] = [];
  if (prefix.length) hunks.push({ kind: "equal", left: prefix, right: prefix });
  hunks.push(...middle);
  if (suffix.length) hunks.push({ kind: "equal", left: suffix, right: suffix });
  return mergeEqualHunks(hunks);
}

export function diffMarkdown(oldText: string, newText: string): LineHunk[] {
  return diffLines(splitMarkdownLines(oldText), splitMarkdownLines(newText));
}

function diffMiddle(a: string[], b: string[]): LineHunk[] {
  if (!a.length && !b.length) return [];
  if (!a.length) return [{ kind: "add", left: [], right: b }];
  if (!b.length) return [{ kind: "remove", left: a, right: [] }];
  if (a.length * b.length > MAX_DP_CELLS) {
    return [{ kind: "replace", left: a, right: b }];
  }
  return hunksFromTokens(lcsTokens(a, b));
}

function lcsTokens(a: string[], b: string[]): Array<{ kind: "equal" | "remove" | "add"; line: string }> {
  const n = a.length;
  const m = b.length;
  const dp: Uint16Array[] = Array.from({ length: n + 1 }, () => new Uint16Array(m + 1));
  for (let i = n - 1; i >= 0; i--) {
    const row = dp[i]!;
    const next = dp[i + 1]!;
    for (let j = m - 1; j >= 0; j--) {
      row[j] = a[i] === b[j] ? next[j + 1]! + 1 : Math.max(next[j]!, row[j + 1]!);
    }
  }
  const tokens: Array<{ kind: "equal" | "remove" | "add"; line: string }> = [];
  let i = 0;
  let j = 0;
  while (i < n && j < m) {
    if (a[i] === b[j]) {
      tokens.push({ kind: "equal", line: a[i]! });
      i += 1;
      j += 1;
    } else if (dp[i + 1]![j]! >= dp[i]![j + 1]!) {
      tokens.push({ kind: "remove", line: a[i]! });
      i += 1;
    } else {
      tokens.push({ kind: "add", line: b[j]! });
      j += 1;
    }
  }
  while (i < n) {
    tokens.push({ kind: "remove", line: a[i]! });
    i += 1;
  }
  while (j < m) {
    tokens.push({ kind: "add", line: b[j]! });
    j += 1;
  }
  return tokens;
}

function hunksFromTokens(
  tokens: Array<{ kind: "equal" | "remove" | "add"; line: string }>,
): LineHunk[] {
  const groups: Array<{ kind: "equal" | "remove" | "add"; lines: string[] }> = [];
  for (const token of tokens) {
    const last = groups[groups.length - 1];
    if (last && last.kind === token.kind) last.lines.push(token.line);
    else groups.push({ kind: token.kind, lines: [token.line] });
  }
  const hunks: LineHunk[] = [];
  for (let i = 0; i < groups.length; i++) {
    const group = groups[i]!;
    const next = groups[i + 1];
    if (group.kind === "remove" && next?.kind === "add") {
      hunks.push({ kind: "replace", left: group.lines, right: next.lines });
      i += 1;
    } else if (group.kind === "add" && next?.kind === "remove") {
      hunks.push({ kind: "replace", left: next.lines, right: group.lines });
      i += 1;
    } else if (group.kind === "equal") {
      hunks.push({ kind: "equal", left: group.lines, right: group.lines });
    } else if (group.kind === "remove") {
      hunks.push({ kind: "remove", left: group.lines, right: [] });
    } else {
      hunks.push({ kind: "add", left: [], right: group.lines });
    }
  }
  return hunks;
}

function mergeEqualHunks(hunks: LineHunk[]): LineHunk[] {
  const out: LineHunk[] = [];
  for (const hunk of hunks) {
    const last = out[out.length - 1];
    if (last && last.kind === "equal" && hunk.kind === "equal") {
      last.left.push(...hunk.left);
      last.right.push(...hunk.right);
    } else {
      out.push({ kind: hunk.kind, left: [...hunk.left], right: [...hunk.right] });
    }
  }
  return out;
}
