import { NavLink } from "react-router-dom";
import type { ReactNode } from "react";
import { useAuth } from "../auth.tsx";

export function Layout({ children }: { children: ReactNode }) {
  const { user, logout } = useAuth();
  return (
    <div className="shell">
      <header className="topbar">
        <div className="brand">错题本</div>
        <div className="top-actions">
          <NavLink className="who" to="/account">
            {user?.username}{user?.role === "readonly" ? " · 只读" : ""}
          </NavLink>
          <button type="button" className="ghost" onClick={() => void logout()}>
            退出
          </button>
        </div>
      </header>
      <nav className="nav">
        <NavLink to="/" end>
          题库
        </NavLink>
        <NavLink to="/notes">笔记</NavLink>
        <NavLink to="/graph">图谱</NavLink>
        <NavLink to="/search">搜索</NavLink>
        <NavLink to="/tags">考点</NavLink>
        <NavLink to="/print">纸质本</NavLink>
        {user?.role !== "readonly" ? <NavLink to="/new">录入</NavLink> : null}
        <NavLink to="/account">设置</NavLink>
      </nav>
      <main className="main">{children}</main>
    </div>
  );
}
