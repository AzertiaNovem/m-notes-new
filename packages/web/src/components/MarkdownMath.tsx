import { normalizeMarkdownMath, sanitizeSvg } from "@mistakebook/shared";
import ReactMarkdown from "react-markdown";
import rehypeKatex from "rehype-katex";
import remarkMath from "remark-math";
import "katex/dist/katex.min.css";
import "katex/contrib/mhchem";

const KATEX_OPTS = { throwOnError: false, strict: false, trust: false, output: "html" as const };

export function MarkdownMath({ children }: { children: string }) {
  return (
    <div className="md">
      <ReactMarkdown remarkPlugins={[remarkMath]} rehypePlugins={[[rehypeKatex, KATEX_OPTS]]}>
        {normalizeMarkdownMath(children ?? "")}
      </ReactMarkdown>
    </div>
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
