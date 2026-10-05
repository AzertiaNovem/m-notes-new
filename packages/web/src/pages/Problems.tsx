import { useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { api } from "../api.ts";
import { useAuth } from "../auth.tsx";
import type { ProblemSummary } from "@mistakebook/shared";
import { InlineMath } from "../components/MarkdownMath.tsx";

export function ProblemsPage() {
  const { user } = useAuth();
  const readOnly = user?.role === "readonly";
  const [subjects, setSubjects] = useState<string[]>([]);
  const [subject, setSubject] = useState("");
  const [tag, setTag] = useState("");
  const [items, setItems] = useState<ProblemSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [total, setTotal] = useState(0);
  const [error, setError] = useState("");
  const [deletingId, setDeletingId] = useState<number | null>(null);

  async function load() {
    setLoading(true);
    try {
      setError("");
      const data = await api.listProblems({ subject, tag });
      setItems(data.items);
      setTotal(data.total);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    api.subjects().then((res) => setSubjects(res.items.map((item) => item.name))).catch(() => {});
  }, []);

  useEffect(() => {
    void load();
  }, [subject, tag]);

  async function remove(item: ProblemSummary) {
    if (readOnly) return;
    if (!window.confirm(`确定删除「${item.title}」？此操作不能恢复。`)) return;
    setDeletingId(item.id);
    setError("");
    try {
      await api.deleteProblem(item.id);
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setDeletingId(null);
    }
  }

  return (
    <section>
      <div className="page-head">
        <h1>题库</h1>
        <p className="muted">{total} 道错题</p>
      </div>
      <div className="filters">
        <select value={subject} onChange={(e) => setSubject(e.target.value)}>
          <option value="">全部学科</option>
          {subjects.map((name) => (
            <option key={name} value={name}>
              {name}
            </option>
          ))}
        </select>
        <input placeholder="考点" value={tag} onChange={(e) => setTag(e.target.value)} />
      </div>
      {error ? <p className="error">{error}</p> : null}
      {loading ? <p className="muted">加载中…</p> : null}
      {!loading && !items.length && !error ? (
        <p className="empty">{subject || tag ? "没有符合筛选条件的题目。" : readOnly ? "所属主账户还没有题目。" : "还没有题目。打开「录入」添加第一道错题。"}</p>
      ) : null}
      <div className="cards">
        {items.map((item) => (
          <article className="card" key={item.id}>
            <Link className="card-body" to={`/problems/${item.id}`}>
              <div className="card-top">
                <strong><InlineMath>{item.title}</InlineMath></strong>
              </div>
              <div className="meta">
                {item.subject}
                {item.source ? ` · ${item.source}` : ""}
              </div>
              <div className="tags">
                {item.tags.map((name) => (
                  <span key={name} className="tag">
                    {name}
                  </span>
                ))}
              </div>
            </Link>
            {!readOnly ? <div className="card-actions">
              <button
                type="button"
                className="danger"
                disabled={deletingId === item.id}
                onClick={() => void remove(item)}
              >
                {deletingId === item.id ? "删除中…" : "删除"}
              </button>
            </div> : null}
          </article>
        ))}
      </div>
    </section>
  );
}
