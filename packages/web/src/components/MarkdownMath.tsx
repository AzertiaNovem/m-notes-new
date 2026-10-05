import { normalizeMarkdownMath, normalizeTitleMath, sanitizeSvg } from "@mistakebook/shared";
import ReactMarkdown, { type Components } from "react-markdown";
import type { ReactNode } from "react";
import rehypeKatex from "rehype-katex";
import remarkMath from "remark-math";
import "katex/dist/katex.min.css";
import "katex/contrib/mhchem";
import "../styles/math-title.css";
import "../styles/highlight.css";
import { remarkHighlight } from "./remarkHighlight.ts";

const KATEX_OPTS = { throwOnError: false, strict: false, trust: false, output: "html" as const };
const REMARK_PLUGINS = [remarkHighlight, remarkMath];

const inlineChildren = ({ children }: { children?: ReactNode }) => <>{children}</>;
const INLINE_COMPONENTS: Components = {
  p: inlineChildren,
  h1: inlineChildren,
  h2: inlineChildren,
  h3: inlineChildren,
  h4: inlineChildren,
  h5: inlineChildren,
  h6: inlineChildren,
  ul: inlineChildren,
  ol: inlineChildren,
  li: inlineChildren,
  blockquote: inlineChildren,
  a: inlineChildren,
  img: () => null,
};

export function MarkdownMath({ children }: { children: string }) {
  return (
    <div className="md">
      <ReactMarkdown remarkPlugins={REMARK_PLUGINS} rehypePlugins={[[rehypeKatex, KATEX_OPTS]]}>
        {normalizeMarkdownMath(children ?? "")}
      </ReactMarkdown>
    </div>
  );
}

export function InlineMath({ children }: { children: string }) {
  return (
    <span className="md md-inline">
      <ReactMarkdown
        remarkPlugins={REMARK_PLUGINS}
        rehypePlugins={[[rehypeKatex, KATEX_OPTS]]}
        components={INLINE_COMPONENTS}
      >
        {normalizeTitleMath(children ?? "")}
      </ReactMarkdown>
    </span>
  );
}

export function InlineSvg({ svg, caption }: { svg: string; caption?: string | null }) {
  const safeSvg = sanitizeSvg(svg).replace(/<svg\b[^>]*>/i, (root) => {
    let normalized = root;
    if (!/\bxmlns\s*=/.test(root)) normalized = normalized.replace("<svg", '<svg xmlns="http://www.w3.org/2000/svg"');
    if (!/\bxmlns:xlink\s*=/.test(root)) normalized = normalized.replace("<svg", '<svg xmlns:xlink="http://www.w3.org/1999/xlink"');
    return normalized;
  });
  return (
    <figure className="diagram">
      <div className="diagram-frame">
        <img src={`data:image/svg+xml;charset=utf-8,${encodeURIComponent(safeSvg)}`} alt={caption || "题目图示"} />
      </div>
      {caption ? <figcaption>{caption}</figcaption> : null}
    </figure>
  );
}
