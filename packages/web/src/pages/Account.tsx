import { type FormEvent, useEffect, useState } from "react";
import { api, setToken, type PublicUser } from "../api.ts";
import { useAuth } from "../auth.tsx";

export function AccountPage() {
  const { user, refresh } = useAuth();
  const readOnly = user?.role === "readonly";
  const [username, setUsername] = useState(user?.username ?? "");
  const [usernamePassword, setUsernamePassword] = useState("");
  const [current, setCurrent] = useState("");
  const [next, setNext] = useState("");
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");
  const [users, setUsers] = useState<PublicUser[]>([]);
  const [limit, setLimit] = useState(5);
  const [loadingUsers, setLoadingUsers] = useState(!readOnly);
  const [newName, setNewName] = useState("");
  const [newPass, setNewPass] = useState("");
  const [busy, setBusy] = useState("");

  async function loadUsers() {
    if (readOnly) return;
    setLoadingUsers(true);
    try {
      const data = await api.subaccounts();
      setUsers(data.items);
      setLimit(Math.min(5, data.limit));
    } finally {
      setLoadingUsers(false);
    }
  }

  useEffect(() => {
    if (user?.username) setUsername(user.username);
  }, [user?.username]);

  useEffect(() => {
    void loadUsers().catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, [user?.id, readOnly]);

  async function perform(action: string, run: () => Promise<void>) {
    if (busy) return;
    setBusy(action);
    setError("");
    setMessage("");
    try {
      await run();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy("");
    }
  }

  function changeUsername(event: FormEvent) {
    event.preventDefault();
    void perform("username", async () => {
      const res = await api.changeUsername(username.trim(), usernamePassword);
      setUsername(res.user.username);
      setUsernamePassword("");
      await refresh();
      setMessage("用户名已更新");
    });
  }

  function changePassword(event: FormEvent) {
    event.preventDefault();
    void perform("password", async () => {
      const res = await api.changePassword(current, next);
      setToken(res.token);
      setCurrent("");
      setNext("");
      await refresh();
      setMessage("密码已更新");
    });
  }

  function addUser(event: FormEvent) {
    event.preventDefault();
    if (readOnly || users.length >= limit) return;
    void perform("create", async () => {
      await api.createSubaccount(newName.trim(), newPass);
      setNewName("");
      setNewPass("");
      await loadUsers();
      setMessage("只读子账户已创建，可使用独立用户名和密码登录");
    });
  }

  function deleteUser(item: PublicUser) {
    if (!window.confirm(`确定删除只读子账户「${item.username}」？该账户将立即失去访问权限，你的数据会保留。`)) return;
    void perform(`delete-${item.id}`, async () => {
      await api.deleteSubaccount(item.id);
      await loadUsers();
      setMessage("只读子账户已删除");
    });
  }

  return (
    <section>
      <div className="page-head">
        <h1>设置</h1>
        <p className="muted">{user?.username} · {readOnly ? "只读子账户" : "主账户"}</p>
      </div>
      {readOnly ? <p className="callout">你可以查看和导出所属主账户的学习数据，并修改自己的登录信息。</p> : <p className="muted">你的学习数据独立保存，只有你和你创建的只读子账户可以访问。</p>}
      {error ? <p className="error" role="alert">{error}</p> : null}
      {message ? <p className="ok" role="status">{message}</p> : null}
      <div className="settings-section">
        <h2>导出全部数据</h2>
        <p className="muted">下载 JSON 文件，保留题目、解答、笔记、历史版本、考点、关联和纸质本数据。导出不包含密码或登录令牌。</p>
        <button type="button" disabled={Boolean(busy)} onClick={() => void perform("export", async () => {
          await api.exportData();
          setMessage("JSON 数据已导出");
        })}>
          {busy === "export" ? "导出中…" : "导出全部数据（JSON）"}
        </button>
      </div>
      {!readOnly ? (
        <div className="settings-section">
          <div className="page-head">
            <h2>只读子账户</h2>
            <span className="muted">{loadingUsers ? "加载中…" : `${users.length} / ${limit}`}</span>
          </div>
          <p className="muted">最多创建 5 个。子账户可以阅读、搜索和导出你的数据，不能新增、修改或删除学习内容，也不能管理其他账户。</p>
          {!loadingUsers && !users.length ? <p className="empty">还没有只读子账户。</p> : null}
          <ul className="dup-list">
            {users.map((item) => (
              <li key={item.id}>
                <span>{item.username}<span className="tag account-role">只读</span></span>
                <button type="button" className="danger" disabled={Boolean(busy)} onClick={() => deleteUser(item)}>
                  {busy === `delete-${item.id}` ? "删除中…" : "删除"}
                </button>
              </li>
            ))}
          </ul>
          {users.length >= limit ? <p className="callout">已达到 {limit} 个子账户的上限。删除一个后可创建新的子账户。</p> : (
            <form className="form" onSubmit={addUser}>
              <label>子账户用户名
                <input value={newName} onChange={(e) => setNewName(e.target.value)} maxLength={64} autoComplete="off" required />
              </label>
              <label>初始密码（至少 8 位）
                <input type="password" value={newPass} onChange={(e) => setNewPass(e.target.value)} minLength={8} maxLength={256} autoComplete="new-password" required />
              </label>
              <button type="submit" disabled={Boolean(busy) || loadingUsers}>{busy === "create" ? "创建中…" : "创建只读子账户"}</button>
            </form>
          )}
        </div>
      ) : null}
      <form className="form settings-section" onSubmit={changeUsername}>
        <h2>修改用户名</h2>
        <label>新用户名
          <input autoComplete="username" value={username} onChange={(e) => setUsername(e.target.value)} required maxLength={64} placeholder="中文、字母或数字" />
        </label>
        <label>当前密码
          <input type="password" autoComplete="current-password" value={usernamePassword} onChange={(e) => setUsernamePassword(e.target.value)} required />
        </label>
        <button type="submit" disabled={Boolean(busy)}>{busy === "username" ? "保存中…" : "保存用户名"}</button>
      </form>
      <form className="form settings-section" onSubmit={changePassword}>
        <h2>修改密码</h2>
        <label>当前密码
          <input type="password" autoComplete="current-password" value={current} onChange={(e) => setCurrent(e.target.value)} required />
        </label>
        <label>新密码（至少 8 位）
          <input type="password" autoComplete="new-password" value={next} onChange={(e) => setNext(e.target.value)} minLength={8} maxLength={256} required />
        </label>
        <button type="submit" disabled={Boolean(busy)}>{busy === "password" ? "保存中…" : "保存密码"}</button>
      </form>
    </section>
  );
}
