import { useEffect, useState } from "react";
import { Link, useParams } from "react-router-dom";
import type { NoteNodeDetail, NoteTocNode, NoteVersion, NoteVersionDiff, NoteVersionSummary } from "@mistakebook/shared";
import { api } from "../api.ts";
import { MarkdownMath } from "../components/MarkdownMath.tsx";
import { NoteSplitDiff } from "../components/NoteSplitDiff.tsx";

type Reader =
  | { kind: "current" }
  | { kind: "version"; data: NoteVersion }
  | { kind: "diff"; data: NoteVersionDiff };

function formatChangedAt(value: string): string {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleString("zh-CN", { hour12: false });
}

export function NotesPage() {
  const { id } = useParams();
  const selectedId = id ? Number(id) : null;
  const [tree, setTree] = useState<NoteTocNode[]>([]);
  const [node, setNode] = useState<NoteNodeDetail | null>(null);
  const [versions, setVersions] = useState<NoteVersionSummary[]>([]);
  const [reader, setReader] = useState<Reader>({ kind: "current" });
  const [error, setError] = useState("");

  useEffect(() => {
    api
      .listNoteToc()
      .then((res) => setTree(res.items))
      .catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, []);

  useEffect(() => {
    setReader({ kind: "current" });
    setVersions([]);
    if (!selectedId) {
      setNode(null);
      return;
    }
    let cancelled = false;
    Promise.all([api.getNoteNode(selectedId), api.listNoteVersions(selectedId)])
      .then(([nextNode, listed]) => {
        if (cancelled) return;
        setNode(nextNode);
        setVersions(listed.items);
      })
      .catch((err) => {
        if (!cancelled) setError(err instanceof Error ? err.message : String(err));
      });
    return () => {
      cancelled = true;
    };
  }, [selectedId]);

  async function openVersion(versionId: number) {
    if (!selectedId) return;
    try {
      setError("");
      const data = await api.getNoteVersion(selectedId, versionId);
      setReader({ kind: "version", data });
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }

  async function openDiff(versionId: number, against: "previous" | "current") {
    if (!selectedId) return;
    try {
      setError("");
      const data = await api.diffNoteVersion(selectedId, versionId, against);
      setReader({ kind: "diff", data });
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }

  const title =
    reader.kind === "version" ? reader.data.title ?? node?.title : node?.title;
  const body =
    reader.kind === "version" ? reader.data.body_md : node?.body_md;

  return (
    <section className="notes-layout">
      <aside className="note-toc">
        <div className="page-head">
          <h1>笔记</h1>
        </div>
        <p className="muted">目录由 MCP write_note 维护。这里只浏览，可打开历史版本。</p>
        {error ? <p className="error">{error}</p> : null}
        {!tree.length && !error ? <p className="empty">还没有笔记节点。</p> : null}
        <ul className="toc-tree">{tree.map((item) => <TocItem key={item.id} node={item} selectedId={selectedId} />)}</ul>
      </aside>
      <article className="detail note-reader">
        {!node ? (
          <p className="muted">从左侧目录选一条笔记。</p>
        ) : (
          <>
            <p className="crumb">
              <Link to="/notes">笔记</Link>
              {node.subject ? ` / ${node.subject}` : ""}
            </p>
            <div className="page-head">
              <h1>{title}</h1>
            </div>
            {reader.kind === "version" ? (
              <div className="version-banner">
                <span className="tag">{reader.data.editor_tool}</span>
                <time dateTime={reader.data.changed_at}>{formatChangedAt(reader.data.changed_at)}</time>
                <span>{reader.data.change_summary}</span>
                <div className="version-banner-actions">
                  <button type="button" className="ghost compact" onClick={() => setReader({ kind: "current" })}>
                    返回当前
                  </button>
                  <button type="button" className="ghost compact" onClick={() => openDiff(reader.data.id, "previous")}>
                    对比上一版
                  </button>
                  <button type="button" className="ghost compact" onClick={() => openDiff(reader.data.id, "current")}>
                    与当前对比
                  </button>
                </div>
              </div>
            ) : null}
            {reader.kind === "diff" ? (
              <div className="version-banner">
                <span>版本对比</span>
                <div className="version-banner-actions">
                  <button type="button" className="ghost compact" onClick={() => setReader({ kind: "current" })}>
                    返回当前
                  </button>
                </div>
              </div>
            ) : null}
            {reader.kind === "diff" ? (
              <NoteSplitDiff diff={reader.data} />
            ) : (
              <MarkdownMath>{body || "（无正文）"}</MarkdownMath>
            )}
            {reader.kind === "current" && node.links.length ? (
              <>
                <h2>关联</h2>
                <ul className="link-list">
                  {node.links.map((link) => {
                    const other = link.left.kind === "note" && link.left.id === node.id ? link.right : link.left;
                    const href = other.kind === "note" ? `/notes/${other.id}` : `/problems/${other.id}`;
                    return (
                      <li key={link.id}>
                        <Link to={href}>{other.title}</Link>
                        <span className="muted">
                          {" "}
                          · {other.kind === "note" ? "笔记" : "题目"}
                          {link.via === "embedding" ? " · 向量" : " · 手工"}
                          {link.label ? ` · ${link.label}` : ""}
                        </span>
                      </li>
                    );
                  })}
                </ul>
              </>
            ) : null}
            <section className="change-logs">
              <h2>版本</h2>
              {!versions.length ? (
                <p className="muted">还没有版本记录。</p>
              ) : (
                <ol>
                  {versions.map((log, index) => (
                    <li key={log.id}>
                      <div className="change-log-meta">
                        <span className="tag">{log.editor_tool}</span>
                        <time dateTime={log.changed_at}>{formatChangedAt(log.changed_at)}</time>
                        {log.title ? <span className="muted">{log.title}</span> : null}
                      </div>
                      <p>{log.change_summary}</p>
                      <div className="version-actions">
                        {log.has_snapshot ? (
                          <button type="button" className="ghost compact" onClick={() => openVersion(log.id)}>
                            查看
                          </button>
                        ) : (
                          <span className="muted">无快照</span>
                        )}
                        <button type="button" className="ghost compact" onClick={() => openDiff(log.id, "previous")}>
                          {index === versions.length - 1 ? "对比空版" : "对比上一版"}
                        </button>
                        <button type="button" className="ghost compact" onClick={() => openDiff(log.id, "current")}>
                          与当前对比
                        </button>
                      </div>
                    </li>
                  ))}
                </ol>
              )}
            </section>
          </>
        )}
      </article>
    </section>
  );
}

function TocItem({ node, selectedId }: { node: NoteTocNode; selectedId: number | null }) {
  return (
    <li>
      <Link className={node.id === selectedId ? "toc-active" : undefined} to={`/notes/${node.id}`}>
        {node.title}
      </Link>
      {node.children.length ? (
        <ul className="toc-tree">
          {node.children.map((child) => (
            <TocItem key={child.id} node={child} selectedId={selectedId} />
          ))}
        </ul>
      ) : null}
    </li>
  );
}
