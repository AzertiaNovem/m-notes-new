import { useEffect, useState } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";
import type { ProblemDetail } from "@mistakebook/shared";
import { api } from "../api.ts";
import { useAuth } from "../auth.tsx";
import { ChangeLogs } from "../components/ChangeLogs.tsx";
import { InlineMath, InlineSvg, MarkdownMath } from "../components/MarkdownMath.tsx";

export function ProblemDetailPage() {
  const { user } = useAuth();
  const readOnly = user?.role === "readonly";
  const { id } = useParams();
  const navigate = useNavigate();
  const problemId = Number(id);
  const [problem, setProblem] = useState<ProblemDetail | null>(null);
  const [saving, setSaving] = useState(false);
  const [deleting, setDeleting] = useState(false);
  const [tags, setTags] = useState("");
  const [note, setNote] = useState("");
  const [summary, setSummary] = useState("");
  const [error, setError] = useState("");
  const [saved, setSaved] = useState("");

  useEffect(() => {
    if (!problemId) return;
    api
      .getProblem(problemId)
      .then((data) => {
        setProblem(data);
        setTags(data.tags.join("，"));
        setNote(data.mistake_note ?? "");
      })
      .catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, [problemId]);

  async function save() {
    if (!problem || readOnly || saving) return;
    const change_summary = summary.trim();
    if (!change_summary) {
      setError("请填写变更大意");
      return;
    }
    setSaving(true);
    try {
      setError("");
      const updated = await api.updateProblem(problem.id, {
        tags: tags.split(/[,，]/).map((s) => s.trim()).filter(Boolean),
        mistake_note: note || null,
        editor_tool: "web",
        change_summary,
      });
      setProblem(updated);
      setTags(updated.tags.join("，"));
      setSummary("");
      setSaved("已保存");
      setTimeout(() => setSaved(""), 1500);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setSaving(false);
    }
  }

  if (error && !problem) return <p className="error">{error}</p>;
  if (!problem) return <p className="muted">加载中…</p>;

  return (
    <article className="detail">
      <p className="crumb">
        <Link to="/">题库</Link> / {problem.subject}
      </p>
      <div className="page-head">
        <h1><InlineMath>{problem.title}</InlineMath></h1>
      </div>
      <div className="tags">
        {problem.tags.map((name) => (
          <span key={name} className="tag">
            {name}
          </span>
        ))}
      </div>
      <h2>题干</h2>
      <MarkdownMath>{problem.stem_md}</MarkdownMath>
      {problem.diagrams.map((diagram) => (
        <InlineSvg key={diagram.id} svg={diagram.svg} caption={diagram.caption} />
      ))}
      <h2>解题思路</h2>
      <p className="muted">卷面之外的分析：审题、方法选择、易错点。</p>
      <MarkdownMath>{problem.solution.approach_md}</MarkdownMath>
      <h2>参考答案</h2>
      <p className="muted">计算或推理过程，以及最终答案。</p>
      <MarkdownMath>{problem.solution.answer_md}</MarkdownMath>
      {problem.links.length ? (
        <>
          <h2>关联笔记</h2>
          <ul className="link-list">
            {problem.links.map((link) => {
              const other =
                link.left.kind === "problem" && link.left.id === problem.id ? link.right : link.left;
              if (other.kind !== "note") return null;
              return (
                <li key={link.id}>
                  <Link to={`/notes/${other.id}`}><InlineMath>{other.title}</InlineMath></Link>
                  <span className="muted">
                    {link.via === "embedding" ? " · 向量" : " · 手工"}
                    {link.label ? ` · ${link.label}` : ""}
                  </span>
                </li>
              );
            })}
          </ul>
        </>
      ) : null}

      {readOnly ? (problem.mistake_note ? <><h2>错因</h2><MarkdownMath>{problem.mistake_note}</MarkdownMath></> : null) : <div className="edit-box">
        <h2>订正</h2>
        {error ? <p className="error">{error}</p> : null}
        <label>
          考点（逗号分隔，会按别名/近邻并入规范名）
          <input value={tags} onChange={(e) => setTags(e.target.value)} />
        </label>
        <label>
          错因
          <textarea value={note} onChange={(e) => setNote(e.target.value)} rows={3} />
        </label>
        <label>
          变更大意
          <input
            value={summary}
            onChange={(e) => setSummary(e.target.value)}
            placeholder="例如：订正错因与考点"
            required
          />
        </label>
        <p className="muted">编辑工具：web</p>
        <div className="actions">
          <button type="button" disabled={saving || deleting} onClick={() => void save()}>
            {saving ? "保存中…" : "保存"}
          </button>
          <button
            type="button"
            className="danger"
            disabled={deleting || saving}
            onClick={() => {
              if (!window.confirm(`确定删除「${problem.title}」？此操作不能恢复。`)) return;
              setDeleting(true);
              setError("");
              api
                .deleteProblem(problem.id)
                .then(() => navigate("/", { replace: true }))
                .catch((err) => {
                  setDeleting(false);
                  setError(err instanceof Error ? err.message : String(err));
                });
            }}
          >
            {deleting ? "删除中…" : "删除题目"}
          </button>
          {saved ? <span className="muted">{saved}</span> : null}
        </div>
      </div>}
      <ChangeLogs items={problem.change_logs ?? []} />
    </article>
  );
}
