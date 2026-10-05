import { normalizeHighlightColor, readHighlight, type HighlightColor } from "@mistakebook/shared";

type MdNode = {
  type: string;
  value?: string;
  children?: MdNode[];
  position?: { start: { line: number }; end: { line: number } };
  data?: {
    hName?: string;
    hProperties?: { className?: string[] };
  };
};

const OPEN_RE = /^==(?:([A-Za-z\u4e00-\u9fff]+):)?/;

function highlightNode(color: HighlightColor, children: MdNode[]): MdNode {
  return {
    type: "highlight",
    data: {
      hName: "mark",
      hProperties: { className: ["hl", `hl-${color}`] },
    },
    children,
  };
}

function opener(value: string): { raw: string; color: HighlightColor | null } | null {
  const open = value.match(OPEN_RE);
  if (!open) return null;
  const color = normalizeHighlightColor(open[1]);
  return { raw: open[0], color };
}

function mergeHighlights(children: MdNode[]): MdNode[] {
  const out: MdNode[] = [];
  const pending = children.slice();
  let i = 0;
  while (i < pending.length) {
    const node = pending[i];
    if (node?.type === "text" && !node.value) {
      i += 1;
      continue;
    }
    if (!node || node.type !== "text" || !node.value || !node.value.includes("==")) {
      if (node) out.push(node);
      i += 1;
      continue;
    }
    const value = node.value;
    const idx = value.indexOf("==");
    if (idx > 0) {
      out.push({ type: "text", value: value.slice(0, idx) });
      pending[i] = { type: "text", value: value.slice(idx) };
      continue;
    }

    const single = readHighlight(value);
    if (single) {
      out.push(highlightNode(single.color, [{ type: "text", value: single.body }]));
      const rest = value.slice(single.raw.length);
      if (rest) pending[i] = { type: "text", value: rest };
      else i += 1;
      continue;
    }

    const open = opener(value);
    const closeInSelf = open ? value.indexOf("==", open.raw.length) : -1;
    if (!open || closeInSelf !== -1) {
      // Preserve a rejected complete marker together: its closer must not become
      // a new yellow opener and consume the next valid colored marker.
      const literalClose = open ? closeInSelf : value.indexOf("==", 2);
      const literalEnd = literalClose === -1 ? 2 : literalClose + 2;
      out.push({ type: "text", value: value.slice(0, literalEnd) });
      const rest = value.slice(literalEnd);
      if (rest) pending[i] = { type: "text", value: rest };
      else i += 1;
      continue;
    }

    const lead = value.slice(open.raw.length);
    const collected: MdNode[] = [];
    if (lead) collected.push({ type: "text", value: lead });
    let closed: { index: number; rest: string } | null = null;
    let aborted = /[\r\n]/.test(lead);
    for (let j = i + 1; j < pending.length; j += 1) {
      const next = pending[j];
      if (!next) break;
      if (next.type === "break" || next.type === "math" || next.type === "code") {
        aborted = true;
        collected.push(next);
        continue;
      }
      if (next.type !== "text") {
        if ((next.position && next.position.start.line !== next.position.end.line) || /[\r\n]/.test(next.value ?? "")) {
          aborted = true;
        }
        collected.push(next);
        continue;
      }
      const text = next.value ?? "";
      const newline = text.search(/[\r\n]/);
      const close = text.indexOf("==");
      if (newline !== -1 && (close === -1 || newline < close)) {
        aborted = true;
      }
      if (close !== -1) {
        const before = text.slice(0, close);
        if (before) collected.push({ type: "text", value: before });
        closed = { index: j, rest: text.slice(close + 2) };
        break;
      }
      collected.push(next);
    }

    if (!closed || collected.length === 0) {
      out.push({ type: "text", value: "==" });
      pending[i] = { type: "text", value: value.slice(2) };
      continue;
    }

    if (aborted || !open.color) {
      // Consume an invalid pair as literal Markdown so its closing delimiter
      // cannot accidentally open a mark around the next valid marker.
      out.push({ type: "text", value: open.raw }, ...collected, { type: "text", value: "==" });
    } else out.push(highlightNode(open.color, collected));
    if (closed.rest) pending[closed.index] = { type: "text", value: closed.rest };
    i = closed.rest ? closed.index : closed.index + 1;
  }
  return out;
}

function transform(node: MdNode) {
  // Literal code and math own their contents; markers are only Markdown syntax.
  if (node.type === "code" || node.type === "inlineCode" || node.type === "math" || node.type === "inlineMath") return;
  if (!node.children) return;
  node.children = mergeHighlights(node.children);
  for (const child of node.children) transform(child);
}

export function remarkHighlight() {
  return (tree: MdNode) => {
    transform(tree);
  };
}
