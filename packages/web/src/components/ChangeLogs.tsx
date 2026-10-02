import type { ChangeLogEntry } from "@mistakebook/shared";

function formatChangedAt(value: string): string {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleString("zh-CN", { hour12: false });
}

export function ChangeLogs({ items }: { items: ChangeLogEntry[] }) {
  return (
    <section className="change-logs">
      <h2>更新记录</h2>
      {!items.length ? (
        <p className="muted">还没有更新记录。</p>
      ) : (
        <ol>
          {items.map((log) => (
            <li key={log.id}>
              <div className="change-log-meta">
                <span className="tag">{log.editor_tool}</span>
                <time dateTime={log.changed_at}>{formatChangedAt(log.changed_at)}</time>
              </div>
              <p>{log.change_summary}</p>
            </li>
          ))}
        </ol>
      )}
    </section>
  );
}
