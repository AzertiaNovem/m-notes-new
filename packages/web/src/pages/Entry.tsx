import { useEffect, useState, type FormEvent } from "react";
import { Navigate, useNavigate } from "react-router-dom";
import { api } from "../api.ts";
import { useAuth } from "../auth.tsx";

export function EntryPage() {
  const { user } = useAuth();
  const navigate = useNavigate();
  const [subjects, setSubjects] = useState<string[]>(["数学", "语文", "英语", "物理", "化学", "生物", "历史", "地理", "政治"]);
  const [saving, setSaving] = useState(false);
  const [title, setTitle] = useState("");
  const [subject, setSubject] = useState("数学");
  const [tags, setTags] = useState("");
  const [stem, setStem] = useState("");
  const [svg, setSvg] = useState("");
  const [caption, setCaption] = useState("");
  const [approach, setApproach] = useState("");
  const [answer, setAnswer] = useState("");
  const [source, setSource] = useState("");
  const [note, setNote] = useState("");
  const [summary, setSummary] = useState("");
  const [error, setError] = useState("");

  useEffect(() => {
    api.subjects().then((res) => {
      if (res.items.length) setSubjects([...new Set(["数学", ...res.items.map((item) => item.name)])]);
    }).catch(() => {});
  }, []);

  async function submit(event: FormEvent) {
    event.preventDefault();
    if (user?.role === "readonly" || saving) return;
    setSaving(true);
    try {
      setError("");
      const created = await api.createProblem({
        title,
        subject,
        tags: tags.split(/[,，]/).map((s) => s.trim()).filter(Boolean),
        stem_md: stem,
        diagrams: svg.trim() ? [{ svg: svg.trim(), caption: caption || null }] : [],
        approach_md: approach,
        answer_md: answer,
        source: source || null,
        mistake_note: note || null,
        editor_tool: "web",
        change_summary: summary.trim(),
      });
      navigate(`/problems/${created.id}`);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setSaving(false);
    }
  }

  if (user?.role === "readonly") return <Navigate to="/" replace />;

  return (
    <section>
      <div className="page-head">
        <h1>粘贴录入</h1>
        <p className="muted">使用 Markdown 和 LaTeX 记录题目、思路与答案，也可添加 SVG 图示。</p>
      </div>
      <div className="callout">也可通过 MCP 连接器录入已整理的题目。这里可直接粘贴 Markdown、LaTeX 和 SVG。</div>
      {error ? <p className="error">{error}</p> : null}
      <form className="form" onSubmit={(e) => void submit(e)}>
        <label>
          标题
          <input value={title} onChange={(e) => setTitle(e.target.value)} required />
        </label>
        <label>
          学科
          <select value={subject} onChange={(e) => setSubject(e.target.value)}>
            {subjects.map((name) => (
              <option key={name}>{name}</option>
            ))}
          </select>
        </label>
        <label>
          考点（逗号分隔）
          <input value={tags} onChange={(e) => setTags(e.target.value)} placeholder="二次函数，顶点坐标" />
        </label>
        <label>
          题干 Markdown
          <textarea value={stem} onChange={(e) => setStem(e.target.value)} rows={6} required />
        </label>
        <label>
          SVG（可选，不要贴扫描件）
          <textarea value={svg} onChange={(e) => setSvg(e.target.value)} rows={5} placeholder="<svg ...>" />
        </label>
        <label>
          图注
          <input value={caption} onChange={(e) => setCaption(e.target.value)} />
        </label>
        <label>
          解题思路
          <textarea
            value={approach}
            onChange={(e) => setApproach(e.target.value)}
            rows={6}
            required
            placeholder="卷面之外的分析：审题、方法选择、为什么、易错点。不要写计算过程。"
          />
        </label>
        <label>
          参考答案
          <textarea
            value={answer}
            onChange={(e) => setAnswer(e.target.value)}
            rows={8}
            required
            placeholder="卷面上的完整计算或推理过程，以及最终答案。"
          />
        </label>
        <label>
          来源
          <input value={source} onChange={(e) => setSource(e.target.value)} />
        </label>
        <label>
          错因
          <input value={note} onChange={(e) => setNote(e.target.value)} />
        </label>
        <label>
          变更大意
          <input
            value={summary}
            onChange={(e) => setSummary(e.target.value)}
            required
            placeholder="例如：粘贴录入新题"
          />
        </label>
        <p className="muted">编辑工具：web</p>
        <button type="submit" disabled={saving}>{saving ? "保存中…" : "写入题库"}</button>
      </form>
    </section>
  );
}
