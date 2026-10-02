import { useEffect, useMemo, useState } from "react";
import { Link, useNavigate, useParams, useSearchParams } from "react-router-dom";
import type { PrintBookDetail, PrintBookSummary, PrintPlan, TagRef } from "@mistakebook/shared";
import { api, type PrintDocument } from "../api.ts";
import { useAuth } from "../auth.tsx";
import { InlineSvg, MarkdownMath } from "../components/MarkdownMath.tsx";

export function PrintPage() {
  const { id } = useParams();
  const bookId = id ? Number(id) : null;
  if (bookId) return <PrintBookView bookId={bookId} />;
  return <PrintBookList />;
}

function PrintBookList() {
  const { user } = useAuth();
  const readOnly = user?.role === "readonly";
  const navigate = useNavigate();
  const [books, setBooks] = useState<PrintBookSummary[]>([]);
  const [subjects, setSubjects] = useState<string[]>([]);
  const [tags, setTags] = useState<TagRef[]>([]);
  const [title, setTitle] = useState("");
  const [pickedSubjects, setPickedSubjects] = useState<string[]>([]);
  const [pickedTags, setPickedTags] = useState<number[]>([]);
  const [error, setError] = useState("");
  const [creating, setCreating] = useState(false);

  async function load() {
    const [listed, tax] = await Promise.all([api.listPrintBooks(), api.taxonomy()]);
    setBooks(listed.items);
    setSubjects(tax.subjects);
    setTags(tax.tags);
  }

  useEffect(() => {
    load().catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, []);

  const visibleTags = useMemo(
    () => (pickedSubjects.length ? tags.filter((tag) => pickedSubjects.includes(tag.subject)) : tags),
    [tags, pickedSubjects],
  );

  async function create() {
    if (readOnly || creating) return;
    setCreating(true);
    setError("");
    try {
      const created = await api.createPrintBook({
        title: title.trim() || undefined,
        subjects: pickedSubjects,
        tag_ids: pickedTags,
      });
      navigate(`/print/${created.id}`);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setCreating(false);
    }
  }

  return (
    <section>
      <div className="page-head">
        <h1>纸质本</h1>
        <p className="muted">按学科和考点整理纸质本，在浏览器中打印或另存为 PDF。</p>
      </div>
      {error ? <p className="error">{error}</p> : null}

      {!readOnly ? <div className="edit-box">
        <h2>新建</h2>
        <label>
          名称（可空，默认用学科）
          <input value={title} onChange={(e) => setTitle(e.target.value)} placeholder="全科 / 数学" />
        </label>
        <div className="chip-list">
          {subjects.map((name) => (
            <label key={name} className="chip">
              <input
                type="checkbox"
                checked={pickedSubjects.includes(name)}
                onChange={() =>
                  setPickedSubjects((cur) =>
                    cur.includes(name) ? cur.filter((item) => item !== name) : [...cur, name],
                  )
                }
              />
              {name}
            </label>
          ))}
        </div>
        <div className="chip-list">
          {visibleTags.map((tag) => (
            <label key={tag.id} className="chip">
              <input
                type="checkbox"
                checked={pickedTags.includes(tag.id)}
                onChange={() =>
                  setPickedTags((cur) =>
                    cur.includes(tag.id) ? cur.filter((item) => item !== tag.id) : [...cur, tag.id],
                  )
                }
              />
              {tag.subject} / {tag.name}
            </label>
          ))}
        </div>
        <p className="muted">不勾选学科或考点表示全部。每个逻辑页可能跨多张纸，实际分页由浏览器决定。</p>
        <div className="actions">
          <button type="button" disabled={creating} onClick={() => void create()}>
            {creating ? "创建中…" : "创建纸质本"}
          </button>
        </div>
      </div> : null}

      <h2>已有纸质本</h2>
      {!books.length ? <p className="empty">还没有纸质本。</p> : null}
      <div className="cards">
        {books.map((book) => (
          <article className="card" key={book.id}>
            <Link className="card-body" to={`/print/${book.id}`}>
              <div className="card-top">
                <strong>{book.title}</strong>
                <span className="muted">{book.page_count} 页</span>
              </div>
              <div className="meta">
                {book.subjects.length ? book.subjects.join("、") : "全部学科"}
                {book.tag_ids.length ? ` · ${book.tag_ids.length} 个考点` : ""}
              </div>
            </Link>
          </article>
        ))}
      </div>
    </section>
  );
}

function PrintBookView({ bookId }: { bookId: number }) {
  const { user } = useAuth();
  const readOnly = user?.role === "readonly";
  const navigate = useNavigate();
  const [book, setBook] = useState<PrintBookDetail | null>(null);
  const [plan, setPlan] = useState<PrintPlan | null>(null);
  const [selected, setSelected] = useState<number[]>([]);
  const [error, setError] = useState("");
  const [message, setMessage] = useState("");
  const [busy, setBusy] = useState("");

  async function load() {
    const detail = await api.getPrintBook(bookId);
    setBook(detail);
    try {
      setPlan(await api.planPrintBook(bookId));
    } catch (err) {
      setPlan(null);
      setError(err instanceof Error ? err.message : String(err));
    }
  }

  useEffect(() => {
    setError("");
    load().catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, [bookId]);

  function toggle(pageNo: number) {
    setSelected((cur) => (cur.includes(pageNo) ? cur.filter((item) => item !== pageNo) : [...cur, pageNo]));
  }

  function toggleSheet(sheet: number) {
    const pages = (book?.pages ?? []).filter((page) => page.sheet === sheet).map((page) => page.page_no);
    setSelected((cur) => {
      const allOn = pages.every((pageNo) => cur.includes(pageNo));
      return allOn ? cur.filter((pageNo) => !pages.includes(pageNo)) : [...new Set([...cur, ...pages])];
    });
  }

  async function apply() {
    if (readOnly || busy) return;
    setBusy("apply");
    setError("");
    setMessage("");
    try {
      const result = await api.applyPrintBook(bookId);
      await load();
      navigate(`/print/${bookId}/document?pages=${result.pages.join(",")}`);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy("");
    }
  }

  async function reprint() {
    if (!selected.length) return;
    setBusy("reprint");
    setError("");
    setMessage("");
    try {
      const result = await api.reprintPrintBook(bookId, selected);
      navigate(`/print/${bookId}/document?pages=${result.pages.join(",")}`);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy("");
    }
  }

  async function remove() {
    if (!book || readOnly || busy) return;
    if (!window.confirm(`确定删除纸质本「${book.title}」？归档内容也会删除。`)) return;
    setBusy("delete");
    try {
      await api.deletePrintBook(book.id);
      navigate("/print", { replace: true });
    } catch (err) {
      setBusy("");
      setError(err instanceof Error ? err.message : String(err));
    }
  }

  if (error && !book) return <p className="error">{error}</p>;
  if (!book) return <p className="muted">加载中…</p>;

  return (
    <article className="detail">
      <p className="crumb">
        <Link to="/print">纸质本</Link> / {book.title}
      </p>
      <div className="page-head">
        <h1>{book.title}</h1>
        <p className="muted">{book.page_count} 页</p>
      </div>
      <div className="meta">
        {book.subjects.length ? book.subjects.join("、") : "全部学科"}
        {book.tag_ids.length ? ` · ${book.tag_ids.length} 个考点` : " · 全部考点"}
      </div>
      {error ? <p className="error">{error}</p> : null}
      {message ? <p className="ok">{message}</p> : null}

      {plan ? (
        <div className="callout">
          {plan.first_generation
            ? "尚未生成归档。生成后可预览、打印或另存为 PDF。"
            : plan.cost === 0
              ? "归档与当前题库一致。"
              : `归档有变更，更新涉及的逻辑页：${plan.reprint_page_nos.join("、") || "无"}。`}
          {plan.moved_problem_ids.length ? (
            <div className="muted">将移动的题 id：{plan.moved_problem_ids.join("、")}</div>
          ) : null}
        </div>
      ) : null}

      <p className="muted">这里显示逻辑页码，每个逻辑页可能跨多张纸。实际分页由浏览器和打印设置决定。</p>
      <div className="actions">
        {book.pages.length ? <Link className="print-preview-link" to={`/print/${bookId}/document`}>预览全部归档页</Link> : null}
        {!readOnly ? <>
        <button type="button" disabled={Boolean(busy) || plan?.cost === 0} onClick={() => void apply()}>
          {busy === "apply" ? "生成中…" : plan?.first_generation ? "首次生成并预览" : "更新归档并预览"}
        </button>
        <button type="button" className="danger" disabled={Boolean(busy)} onClick={() => void remove()}>
          {busy === "delete" ? "删除中…" : "删除纸质本"}
        </button>
        </> : null}
      </div>

      <h2>归档逻辑页</h2>
      {!book.pages.length ? <p className="muted">{readOnly ? "主账户尚未生成归档页。" : "还没有归档页，先生成一次。"}</p> : null}
      <table className="table">
        <thead>
          <tr>
            <th></th>
            <th>页</th>
            <th>分组</th>
            <th>学科</th>
            <th>题目</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          {book.pages.map((page) => (
            <tr key={page.page_no}>
              <td>
                <input
                  type="checkbox"
                  checked={selected.includes(page.page_no)}
                  onChange={() => toggle(page.page_no)}
                />
              </td>
              <td>{page.page_no}</td>
              <td>
                <button type="button" className="ghost compact" onClick={() => toggleSheet(page.sheet)}>
                  {page.sheet}
                </button>
              </td>
              <td>{page.subject}</td>
              <td>{page.titles.join("、") || "（续页）"}</td>
              <td>{page.stale ? <span className="tag warn-tag">已变</span> : null}</td>
            </tr>
          ))}
        </tbody>
      </table>
      <div className="actions">
        <button type="button" disabled={!selected.length || Boolean(busy)} onClick={() => void reprint()}>
          {busy === "reprint" ? "准备中…" : `预览选中页（${selected.length}）`}
        </button>
      </div>
    </article>
  );
}

export function PrintDocumentPage() {
  const { id } = useParams();
  const [params] = useSearchParams();
  const bookId = Number(id);
  const pageSelection = params.get("pages") ?? "";
  const [document, setDocument] = useState<PrintDocument | null>(null);
  const [error, setError] = useState("");

  useEffect(() => {
    let cancelled = false;
    setDocument(null);
    setError("");
    const pages = [...new Set(pageSelection.split(",").map(Number).filter((page) => Number.isSafeInteger(page) && page > 0))];
    api.printDocument(bookId, pages.length ? pages : undefined)
      .then((data) => { if (!cancelled) setDocument(data); })
      .catch((err) => { if (!cancelled) setError(err instanceof Error ? err.message : String(err)); });
    return () => { cancelled = true; };
  }, [bookId, pageSelection]);

  useEffect(() => {
    if (!document) return;
    const previous = window.document.title;
    window.document.title = `${document.book.title} · 错题本`;
    return () => { window.document.title = previous; };
  }, [document]);

  return (
    <section className="print-document">
      <div className="print-controls">
        <p className="crumb"><Link to={`/print/${bookId}`}>返回纸质本</Link></p>
        <div className="page-head"><h1>{document?.book.title ?? "打印预览"}</h1></div>
        <p className="muted">预览显示保存的归档内容。每个逻辑页可能跨多张纸；打印时选择 A4，可在系统打印窗口另存为 PDF。</p>
        <button type="button" disabled={!document?.pages.length} onClick={() => window.print()}>打印 / 另存为 PDF</button>
        {error ? <p className="error" role="alert">{error}</p> : null}
        {!document && !error ? <p className="muted">正在加载归档内容…</p> : null}
        {document && !document.pages.length ? <p className="empty">还没有可打印的归档页。</p> : null}
      </div>
      {document?.pages.map((page) => (
        <section className="print-logical-page" key={page.page_no}>
          <div className="print-page-heading"><span>{document.book.title} · {page.subject}</span><span>逻辑页 {page.page_no}</span></div>
          {page.problems.map((problem) => (
            <article className="print-problem" key={problem.id}>
              <h1>{problem.title}</h1>
              <p className="meta">{problem.subject}{problem.source ? ` · ${problem.source}` : ""}</p>
              {problem.tags.length ? <p className="meta">考点：{problem.tags.join("、")}</p> : null}
              <h2>题干</h2>
              <MarkdownMath>{problem.stem_md}</MarkdownMath>
              {problem.diagrams.map((diagram) => <InlineSvg key={diagram.id} svg={diagram.svg} caption={diagram.caption} />)}
              <h2>解题思路</h2>
              <MarkdownMath>{problem.solution.approach_md}</MarkdownMath>
              <h2>参考答案</h2>
              <MarkdownMath>{problem.solution.answer_md}</MarkdownMath>
              {problem.mistake_note ? <><h2>错因</h2><MarkdownMath>{problem.mistake_note}</MarkdownMath></> : null}
            </article>
          ))}
        </section>
      ))}
    </section>
  );
}
