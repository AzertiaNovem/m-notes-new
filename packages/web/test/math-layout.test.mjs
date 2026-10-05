import assert from "node:assert/strict";
import test from "node:test";
import React from "react";
import { renderToStaticMarkup } from "react-dom/server";
import ReactMarkdown from "react-markdown";
import remarkMath from "remark-math";
import rehypeKatex from "rehype-katex";
import { normalizeMarkdownMath, normalizeTitleMath } from "../../shared/src/markdown-math.ts";
import { remarkHighlight } from "../src/components/remarkHighlight.ts";

function render(markdown) {
  return renderToStaticMarkup(React.createElement(ReactMarkdown, {
    remarkPlugins: [remarkHighlight, remarkMath],
    rehypePlugins: [[rehypeKatex, { throwOnError: false, strict: false, trust: false, output: "html" }]],
  }, markdown));
}

function assertMathLayoutStyles(html) {
  assert.match(html, /class="katex"/);
  assert.match(html, /class="strut"[^>]*style="[^"]*height:\d/);
  assert.match(html, /class="pstrut"[^>]*style="[^"]*height:\d/);
  assert.match(html, /class="vlist"[^>]*style="[^"]*height:\d/);
  assert.match(html, /<span style="[^"]*top:-?\d/);
  assert.doesNotMatch(html, /style=""/);
}

test("rendered title subscripts, superscripts and fractions retain KaTeX positioning styles", () => {
  for (const title of [
    "数列 a_{n+1}",
    "幂次 2^{n+1}",
    String.raw`分式 $\frac{a_{n+1}}{a_n}$`,
    String.raw`分式 \[\frac{1}{2}\]`,
  ]) assertMathLayoutStyles(render(normalizeTitleMath(title)));
});

test("colored body formulas and display fractions retain their positioning and fraction rule", () => {
  const inline = render(normalizeMarkdownMath(String.raw`==blue:$a_{n+1}$ 与 $\frac{1}{2}$==`));
  assertMathLayoutStyles(inline);
  assert.match(inline, /<mark class="hl hl-blue">[\s\S]*class="katex"/);
  assert.match(inline, /class="frac-line"[^>]*style="[^"]*border-bottom-width:/);
  const display = render(normalizeMarkdownMath(String.raw`\[\frac{a_{n+1}}{a_n}\]`));
  assertMathLayoutStyles(display);
  assert.match(display, /class="katex-display"/);
});
