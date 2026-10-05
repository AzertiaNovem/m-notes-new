import type { NoteDiffHunk, NoteVersionDiff, NoteVersionRef } from "@mistakebook/shared";
import type { ReactNode } from "react";
import { InlineMath } from "./MarkdownMath.tsx";

function formatChangedAt(value: string | null): string {
  if (!value) return "空";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleString("zh-CN", { hour12: false });
}

function sideLabel(ref: NoteVersionRef): ReactNode {
  if (ref.source === "empty") return "（无上一版）";
  if (ref.source === "current") return "当前正文";
  return <>{ref.title ? <InlineMath>{ref.title}</InlineMath> : "（空）"}{" · "}{formatChangedAt(ref.changed_at)}</>;
}

type SplitRow = {
  left: string | null;
  right: string | null;
  leftKind: "equal" | "remove" | "blank";
  rightKind: "equal" | "add" | "blank";
};

function flattenHunks(hunks: NoteDiffHunk[]): SplitRow[] {
  const rows: SplitRow[] = [];
  for (const hunk of hunks) {
    if (hunk.kind === "equal") {
      for (const line of hunk.left) {
        rows.push({ left: line, right: line, leftKind: "equal", rightKind: "equal" });
      }
      continue;
    }
    if (hunk.kind === "remove") {
      for (const line of hunk.left) {
        rows.push({ left: line, right: null, leftKind: "remove", rightKind: "blank" });
      }
      continue;
    }
    if (hunk.kind === "add") {
      for (const line of hunk.right) {
        rows.push({ left: null, right: line, leftKind: "blank", rightKind: "add" });
      }
      continue;
    }
    const n = Math.max(hunk.left.length, hunk.right.length);
    for (let i = 0; i < n; i++) {
      const left = hunk.left[i] ?? null;
      const right = hunk.right[i] ?? null;
      rows.push({
        left,
        right,
        leftKind: left == null ? "blank" : "remove",
        rightKind: right == null ? "blank" : "add",
      });
    }
  }
  return rows;
}

export function NoteSplitDiff({ diff }: { diff: NoteVersionDiff }) {
  const rows = flattenHunks(diff.hunks);
  const unchanged = !diff.title_changed && !diff.parent_changed && !diff.sort_changed && rows.length === 0;
  return (
    <div className="note-diff">
      <div className="diff-heads">
        <div>
          <span className="muted">旧</span>
          <strong>{sideLabel(diff.from)}</strong>
        </div>
        <div>
          <span className="muted">新</span>
          <strong>{sideLabel(diff.to)}</strong>
        </div>
      </div>
      {diff.title_changed || diff.parent_changed || diff.sort_changed ? (
        <ul className="diff-meta">
          {diff.title_changed ? (
            <li>
              标题：<del>{diff.from.title ? <InlineMath>{diff.from.title}</InlineMath> : "（空）"}</del> → <ins>{diff.to.title ? <InlineMath>{diff.to.title}</InlineMath> : "（空）"}</ins>
            </li>
          ) : null}
          {diff.parent_changed ? (
            <li>
              父节点：{diff.from.parent_id ?? "根"} → {diff.to.parent_id ?? "根"}
            </li>
          ) : null}
          {diff.sort_changed ? (
            <li>
              排序：{diff.from.sort_order} → {diff.to.sort_order}
            </li>
          ) : null}
        </ul>
      ) : null}
      {unchanged ? (
        <p className="muted">这两版正文相同。</p>
      ) : (
        <div className="diff-split" role="table" aria-label="版本对比">
          {rows.map((row, index) => (
            <div className="diff-pair" key={index} role="row">
              <pre className={`diff-cell left ${row.leftKind}`} role="cell">
                {row.left ?? ""}
              </pre>
              <pre className={`diff-cell right ${row.rightKind}`} role="cell">
                {row.right ?? ""}
              </pre>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
