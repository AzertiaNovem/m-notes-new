import { type FormEvent, useState } from "react";
import { Link, Navigate, useSearchParams } from "react-router-dom";
import { useAuth } from "../auth.tsx";

export function safeNext(raw: string | null): string {
  if (!raw || !raw.startsWith("/") || raw.startsWith("//") || /[\\\u0000-\u001f\u007f]/.test(raw)) return "/";
  try {
    const target = new URL(raw, window.location.origin);
    if (target.origin !== window.location.origin || /^\/(login|register)\/?$/.test(target.pathname)) return "/";
    return target.pathname + target.search + target.hash;
  } catch {
    return "/";
  }
}

export function LoginPage({ registerMode = false }: { registerMode?: boolean }) {
  const { user, loading, login, register } = useAuth();
  const [params] = useSearchParams();
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const next = safeNext(params.get("next"));

  if (!loading && user) return <Navigate to={next} replace />;

  async function submit(event: FormEvent) {
    event.preventDefault();
    if (busy) return;
    setError("");
    if (registerMode && password !== confirmPassword) {
      setError("两次输入的密码不一致");
      return;
    }
    setBusy(true);
    try {
      await (registerMode ? register : login)(username.trim(), password);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy(false);
    }
  }

  return (
    <main className="login-page">
      <h1>错题本</h1>
      <p className="muted">{registerMode ? "创建自己的错题本，独立保存题目、笔记和学习记录。" : "登录你的错题本，继续整理与复习。"}</p>
      <form className="form login-form" onSubmit={submit}>
        <label>用户名
          <input autoComplete="username" value={username} onChange={(e) => setUsername(e.target.value)} maxLength={64} required />
        </label>
        <label>{registerMode ? "密码（至少 8 位）" : "密码"}
          <input type="password" autoComplete={registerMode ? "new-password" : "current-password"} value={password} onChange={(e) => setPassword(e.target.value)} minLength={registerMode ? 8 : undefined} maxLength={256} required />
        </label>
        {registerMode ? <label>确认密码
          <input type="password" autoComplete="new-password" value={confirmPassword} onChange={(e) => setConfirmPassword(e.target.value)} minLength={8} maxLength={256} required />
        </label> : null}
        {error ? <p className="error" role="alert">{error}</p> : null}
        <button type="submit" disabled={busy || loading}>{busy ? (registerMode ? "注册中…" : "登录中…") : (registerMode ? "注册并登录" : "登录")}</button>
      </form>
      <p className="muted">{registerMode ? "已有账户？" : "还没有账户？"} <Link to={`${registerMode ? "/login" : "/register"}?next=${encodeURIComponent(next)}`}>{registerMode ? "去登录" : "注册账户"}</Link></p>
    </main>
  );
}
