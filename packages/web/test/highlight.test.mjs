import assert from "node:assert/strict";
import test from "node:test";
import React from "react";
import { renderToStaticMarkup } from "react-dom/server";
import ReactMarkdown from "react-markdown";
import remarkMath from "remark-math";
import rehypeKatex from "rehype-katex";
import { readHighlight, normalizeHighlightColor } from "../../shared/src/highlight.ts";
import { normalizeMarkdownMath } from "../../shared/src/markdown-math.ts";
import { remarkHighlight } from "../src/components/remarkHighlight.ts";

const render = (markdown) => renderToStaticMarkup(React.createElement(ReactMarkdown, {
  remarkPlugins: [remarkHighlight, remarkMath],
  rehypePlugins: [[rehypeKatex, { throwOnError: false, strict: false, trust: false, output: "html" }]],
}, normalizeMarkdownMath(markdown)));

test("plain and colored emphasis retains the upstream aliases and consumes only its marker", () => {
  assert.deepEqual(readHighlight("==重点== trailing"), { raw: "==重点==", color: "yellow", body: "重点" });
  for (const [color, alias] of [["yellow", "黄"], ["red", "红"], ["green", "绿"], ["blue", "蓝"]]) {
    assert.equal(normalizeHighlightColor(alias), color);
    assert.equal(readHighlight(`==${color}:重点==`)?.color, color);
    assert.equal(readHighlight(`==${alias}:重点==`)?.color, color);
    assert.match(render(`==${alias}:重点==`), new RegExp(`<mark class="hl hl-${color}">重点</mark>`));
  }
  assert.equal(readHighlight("==red:a==b==")?.raw, "==red:a==");
});

test("invalid, empty and unterminated emphasis remains readable", () => {
  for (const markdown of ["==foo:bar==", "==constructor:正文==", "==toString:正文==", "====", "==red:  ==", "==red:重点", "==red:甲\n乙==", "==red:甲\r乙=="]) {
    assert.equal(readHighlight(markdown), null, markdown);
    assert.doesNotMatch(render(markdown), /<mark\b/, markdown);
  }
  assert.match(render("前 ==foo:bar== 后 ==blue:有效=="), /<mark class="hl hl-blue">有效<\/mark>/);
  const mixed = render("==foo:$x$== 后 ==blue:有效==");
  assert.match(mixed, /<mark class="hl hl-blue">有效<\/mark>/);
  assert.doesNotMatch(mixed, /hl-yellow/);
  for (const name of ["constructor", "toString", "__proto__"]) assert.equal(normalizeHighlightColor(name), null);
});

test("colored emphasis wraps real KaTeX inline math and Markdown emphasis", () => {
  const html = render("条件 ==blue:$a_{n+1}$ **大于** $a_n$==，结论 ==green:递增==。");
  assert.match(html, /<mark class="hl hl-blue">[\s\S]*class="katex"[\s\S]*<strong>大于<\/strong>[\s\S]*class="katex"[\s\S]*<\/mark>/);
  assert.match(html, /<mark class="hl hl-green">递增<\/mark>/);
  assert.doesNotMatch(html, /==blue:|==green:/);
});

test("code and mathematical contents never interpret their own highlight markers", () => {
  const html = render("`==red:代码==`\n\n```text\n==green:代码块==\n```\n\n$a ==red:b== c$\n\n$$\na ==blue:b== c\n$$");
  assert.doesNotMatch(html, /<mark\b/);
  assert.match(html, /<code>==red:代码==<\/code>/);
  assert.match(html, /==green:代码块==/);
  assert.match(html, /class="katex"/);
});

test("markers do not cross soft breaks, hard breaks, paragraphs or list items", () => {
  for (const markdown of [
    "==red:甲\n乙==",
    "==red:甲  \n乙==",
    "==red:甲\\\n乙==",
    "==red:$x\n+y$==",
    "==red:`甲\n乙`==",
    "==red:甲\n\n乙==",
    "- ==red:甲\n- 乙==",
    "==red:甲\n\n$$\nx^2\n$$\n\n乙==",
  ]) assert.doesNotMatch(render(markdown), /<mark\b/, markdown);
  const withValid = render("==red:甲  \n乙== 后 ==blue:有效==");
  assert.match(withValid, /<mark class="hl hl-blue">有效<\/mark>/);
  assert.doesNotMatch(withValid, /hl-red|hl-yellow/);
});

test("adjacent colored markers render independently without swallowing following text", () => {
  const html = render("前==red:易错====green:结论==后");
  assert.equal(html, '<p>前<mark class="hl hl-red">易错</mark><mark class="hl hl-green">结论</mark>后</p>');
});
