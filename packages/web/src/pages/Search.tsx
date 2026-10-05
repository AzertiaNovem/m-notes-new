import { useState, type FormEvent } from "react";
import { Link } from "react-router-dom";
import type { SearchHit } from "@mistakebook/shared";
import { api } from "../api.ts";
import { InlineMath, MarkdownMath } from "../components/MarkdownMath.tsx";

export function SearchPage() {
  const [query, setQuery] = useState("");
  const [mode, setMode] = useState("hybrid");
  const [target, setTarget] = useState("all");
  const [subject, setSubject] = useState("");
  const [tag, setTag] = useState("");
  const [hits, setHits] = useState<SearchHit[]>([]);
  const [error, setError] = useState("");
  const [searched, setSearched] = useState(false);
  const [searching, setSearching] = useState(false);

  async function run(event?: FormEvent) {
    event?.preventDefault();
    if (!query.trim() || searching) return;
    setSearching(true);
    try {
      setError("");
      const result = await api.search({
        query: query.trim(),
        mode,
        target,
        subject: subject || undefined,
        tag: tag || undefined,
      });
      setHits(result.hits);
      setSearched(true);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setSearching(false);
    }
  }

  return (
    <section>
      <div className="page-head">
        <h1>搜索</h1>
        <p className="muted">搜索题目、思路和笔记，可选择关键词、混合检索或本地文本相似度。</p>
      </div>
      <form className="filters" onSubmit={(e) => void run(e)}>
        <input
          className="grow"
          placeholder="考点、题干或思路关键字"
          value={query}
          onChange={(e) => setQuery(e.target.value)}
        />
        <select value={mode} onChange={(e) => setMode(e.target.value)}>
          <option value="hybrid">混合检索</option>
          <option value="keyword">仅关键词</option>
          <option value="rag">本地相似度</option>
        </select>
        <select value={target} onChange={(e) => setTarget(e.target.value)}>
          <option value="all">题+笔记</option>
          <option value="problems">仅题目</option>
          <option value="notes">仅笔记</option>
        </select>
        <input placeholder="学科" value={subject} onChange={(e) => setSubject(e.target.value)} />
        <input placeholder="考点" value={tag} onChange={(e) => setTag(e.target.value)} />
        <button type="submit" disabled={searching || !query.trim()}>{searching ? "搜索中…" : "搜索"}</button>
      </form>
      {error ? <p className="error">{error}</p> : null}
      {searched && !searching && !error && !hits.length ? <p className="empty">没有命中。</p> : null}
      <div className="hits">
        {hits.map((hit) => {
          const entity = hit.entity;
          const href = entity.kind === "note" ? `/notes/${entity.id}` : `/problems/${entity.id}`;
          const kindLabel =
            hit.kind === "note" ? "笔记" : hit.kind === "knowledge_point" ? "考点" : "思路";
          return (
            <article className="hit" key={hit.chunk_id}>
              <div className="card-top">
                <Link to={href}><InlineMath>{entity.title}</InlineMath></Link>
                <span className="muted">
                  {kindLabel} · {entity.subject ?? "未分科"}
                </span>
              </div>
              <p className="hit-text">{highlight(hit.text, query)}</p>
              {entity.kind === "problem" ? (
                <details>
                  <summary>题干</summary>
                  <MarkdownMath>{entity.stem_md}</MarkdownMath>
                </details>
              ) : (
                <details>
                  <summary>笔记正文</summary>
                  <MarkdownMath>{entity.body_md}</MarkdownMath>
                </details>
              )}
            </article>
          );
        })}
      </div>
    </section>
  );
}

function highlight(text: string, query: string) {
  const q = query.trim();
  if (!q) return text;
  const idx = text.indexOf(q);
  if (idx < 0) return text;
  return (
    <>
      {text.slice(0, idx)}
      <mark>{text.slice(idx, idx + q.length)}</mark>
      {text.slice(idx + q.length)}
    </>
  );
}
