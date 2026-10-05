import test from 'node:test';
import assert from 'node:assert/strict';
import { normalizeMarkdownMath, normalizeTitleMath } from '../packages/shared/src/markdown-math.ts';

test('bare title formulas keep surrounding Chinese phrases and normalize operators', () => {
  assert.equal(normalizeMarkdownMath('分式递推：a_{n+1}=2a_n/(a_n+2)，取倒数化等差'),
    '分式递推：$a_{n+1}=2a_n/(a_n+2)$，取倒数化等差');
  assert.equal(normalizeMarkdownMath('一阶线性递推：a_{n+1}=2a_n+5·2^n，同底时除以 2^{n+1} 化等差'),
    '一阶线性递推：$a_{n+1}=2a_n+5\\cdot 2^n$，同底时除以 $2^{n+1}$ 化等差');
  assert.equal(normalizeMarkdownMath('待定 A·3^n 构造等比'), '待定 $A\\cdot 3^n$ 构造等比');
  assert.equal(normalizeMarkdownMath('x_1×2^n'), '$x_1\\times 2^n$');
  assert.equal(normalizeMarkdownMath('==red:a_n== 与 ==blue:2^{n+1}=='),
    '==red:$a_n$== 与 ==blue:$2^{n+1}$==');
});

test('ordinary titles, code, links and existing delimiters are preserved', () => {
  for (const value of [
    '函数与导数', '待定 An+B 构造等比', 'file_name 与 [^1]',
    '$a_{n+1}$', '`a_n` 与 ```\n2^n\n```',
    '见 [a_n](https://example.com/a_n) 和 [^1] 与 [ ] 待办',
    '![x_1](https://example.com/(x_1).svg)', '[a_n][formula_name]',
    'https://example.com/a_n?q=2^n',
  ]) assert.equal(normalizeMarkdownMath(value), value, value);
});

test('title math uses inline layout for every supported delimiter', () => {
  assert.equal(normalizeTitleMath('分式 \\[\\frac{1}{2}\\]'), '分式 $\\frac{1}{2}$');
  assert.equal(normalizeTitleMath('分式 $$\n\\frac{1}{2}\n$$'), '分式 $\\frac{1}{2}$');
  assert.equal(normalizeTitleMath('根式 \\(\\sqrt{5}\\)'), '根式 $\\sqrt{5}$');
  assert.equal(normalizeTitleMath('H₂O 与 H_2S'), '$H_2O$ 与 $H_2S$');
  assert.equal(normalizeTitleMath('`$$x$$` 与普通标题'), '`$$x$$` 与普通标题');
  assert.equal(normalizeMarkdownMath('\\[\\frac{1}{2}\\]'), '$$\n\\frac{1}{2}\n$$');
});
