import type { ReactNode } from "react";
import { Navigate, Route, Routes, useLocation } from "react-router-dom";
import { Layout } from "./components/Layout.tsx";
import { useAuth } from "./auth.tsx";
import { AccountPage } from "./pages/Account.tsx";
import { EntryPage } from "./pages/Entry.tsx";
import { GraphPage } from "./pages/Graph.tsx";
import { LoginPage } from "./pages/Login.tsx";
import { NotesPage } from "./pages/Notes.tsx";
import { ProblemDetailPage } from "./pages/ProblemDetail.tsx";
import { ProblemsPage } from "./pages/Problems.tsx";
import { SearchPage } from "./pages/Search.tsx";
import { PrintDocumentPage, PrintPage } from "./pages/Print.tsx";
import { TagsPage } from "./pages/Tags.tsx";

function Guard({ children }: { children: ReactNode }) {
  const { user, loading } = useAuth();
  const location = useLocation();
  if (loading) return <p className="muted">加载中…</p>;
  if (!user) {
    const next = encodeURIComponent(location.pathname + location.search);
    return <Navigate to={`/login?next=${next}`} replace />;
  }
  return <>{children}</>;
}

export function App() {
  return (
    <Routes>
      <Route path="/login" element={<LoginPage key="login" />} />
      <Route path="/register" element={<LoginPage key="register" registerMode />} />
      <Route
        path="*"
        element={
          <Guard>
            <Layout>
              <Routes>
                <Route path="/" element={<ProblemsPage />} />
                <Route path="/problems/:id" element={<ProblemDetailPage />} />
                <Route path="/notes" element={<NotesPage />} />
                <Route path="/notes/:id" element={<NotesPage />} />
                <Route path="/graph" element={<GraphPage />} />
                <Route path="/search" element={<SearchPage />} />
                <Route path="/tags" element={<TagsPage />} />
                <Route path="/print" element={<PrintPage />} />
                <Route path="/print/:id" element={<PrintPage />} />
                <Route path="/print/:id/document" element={<PrintDocumentPage />} />
                <Route path="/new" element={<EntryPage />} />
                <Route path="/account" element={<AccountPage />} />
                <Route path="*" element={<Navigate to="/" replace />} />
              </Routes>
            </Layout>
          </Guard>
        }
      />
    </Routes>
  );
}
