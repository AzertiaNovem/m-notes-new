import { useEffect, useState } from "react";
import type { DuplicatePair, TagRef } from "@mistakebook/shared";
import { api } from "../api.ts";
import { useAuth } from "../auth.tsx";

export function TagsPage() {
  const { user } = useAuth();
  const readOnly = user?.role === "readonly";
  const [merging, setMerging] = useState(false);
  const [tags, setTags] = useState<TagRef[]>([]);
  const [pairs, setPairs] = useState<DuplicatePair[]>([]);
  const [source, setSource] = useState("");
  const [target, setTarget] = useState("");
  const [error, setError] = useState("");
  const [message, setMessage] = useState("");

  async function load() {
    const [tax, dups] = await Promise.all([api.taxonomy(), api.duplicates()]);
    setTags(tax.tags);
    setPairs(dups.pairs);
  }

  useEffect(() => {
    load().catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, []);

  async function merge(sourceId: number, targetId: number) {
    if (readOnly || merging || sourceId === targetId) return;
    const from = tags.find((tag) => tag.id === sourceId);
    const to = tags.find((tag) => tag.id === targetId);
    if (!window.confirm(`确定将「${from?.name ?? sourceId}」并入「${to?.name ?? targetId}」？相关题目的考点将一并更新。`)) return;
    setMerging(true);
    setMessage("");
    try {
      setError("");
      const result = await api.mergeTags(sourceId, targetId);
      setMessage(`已将「${result.absorbed_name}」并入「${result.target.name}」`);
      setSource("");
      setTarget("");
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setMerging(false);
    }
  }

  return (
    <section>
      <div className="page-head">
        <h1>考点</h1>
        <p className="muted">规范名、别名和疑似重复考点。</p>
      </div>
      {error ? <p className="error">{error}</p> : null}
      {message ? <p className="ok">{message}</p> : null}

      {!readOnly ? <>
      <h2>手动合并</h2>
      <div className="filters">
        <select value={source} onChange={(e) => setSource(e.target.value)}>
          <option value="">被合并（废弃名）</option>
          {tags.map((tag) => (
            <option key={tag.id} value={tag.id}>
              {tag.subject} / {tag.name}
            </option>
          ))}
        </select>
        <select value={target} onChange={(e) => setTarget(e.target.value)}>
          <option value="">保留（规范名）</option>
          {tags.map((tag) => (
            <option key={tag.id} value={tag.id}>
              {tag.subject} / {tag.name}
            </option>
          ))}
        </select>
        <button
          type="button"
          disabled={!source || !target || source === target || merging}
          onClick={() => void merge(Number(source), Number(target))}
        >
          合并
        </button>
      </div>
      </> : null}

      <h2>疑似重复</h2>
      {!pairs.length ? <p className="muted">当前阈值下没有成对的近邻考点。</p> : null}
      <ul className="dup-list">
        {pairs.map((pair) => (
          <li key={`${pair.tag_a.id}-${pair.tag_b.id}`}>
            <span>
              {pair.tag_a.subject} · {pair.tag_a.name} ≈ {pair.tag_b.name}（{pair.score.toFixed(3)}）
            </span>
            {!readOnly ? <button type="button" disabled={merging} onClick={() => void merge(pair.tag_b.id, pair.tag_a.id)}>
              并入 {pair.tag_a.name}
            </button> : null}
          </li>
        ))}
      </ul>

      <h2>全部考点</h2>
      <table className="table">
        <thead>
          <tr>
            <th>学科</th>
            <th>规范名</th>
            <th>别名</th>
          </tr>
        </thead>
        <tbody>
          {tags.map((tag) => (
            <tr key={tag.id}>
              <td>{tag.subject}</td>
              <td>{tag.name}</td>
              <td>{tag.aliases.join("、") || "—"}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </section>
  );
}
