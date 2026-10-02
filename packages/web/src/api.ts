const TOKEN_KEY = "mistakebook_native_token";

export function apiBase(): string {
  const raw = import.meta.env.VITE_API_BASE_URL as string | undefined;
  return (raw ?? "").replace(/\/$/, "");
}

export function getToken(): string | null {
  try {
    return localStorage.getItem(TOKEN_KEY);
  } catch {
    return null;
  }
}

export function setToken(token: string | null): void {
  try {
    if (token) localStorage.setItem(TOKEN_KEY, token);
    else localStorage.removeItem(TOKEN_KEY);
  } catch {
    // private mode
  }
}

function url(path: string): string {
  if (path.startsWith("http")) return path;
  return `${apiBase()}${path}`;
}

export class ApiError extends Error {
  readonly status: number;
  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const headers = new Headers(init?.headers);
  if (!headers.has("Content-Type") && init?.body) headers.set("Content-Type", "application/json");
  const token = getToken();
  if (token) headers.set("Authorization", `Bearer ${token}`);
  let res: Response;
  try {
    res = await fetch(url(path), { ...init, headers });
  } catch {
    throw new ApiError("网络不可用，请检查连接", 0);
  }
  if (res.status === 204) return undefined as T;
  const body = await res.json().catch(() => ({}));
  if (res.status === 401) {
    setToken(null);
    if (!["/login", "/register"].includes(window.location.pathname)) {
      const next = encodeURIComponent(window.location.pathname + window.location.search);
      window.location.assign(`/login?next=${next}`);
    }
    throw new ApiError(typeof body.detail === "string" ? body.detail : "请先登录", 401);
  }
  if (!res.ok) {
    const detail = (body as { detail?: unknown }).detail;
    throw new ApiError(typeof detail === "string" ? detail : res.statusText, res.status);
  }
  return body as T;
}

export type PublicUser = {
  id: number;
  username: string;
  role: "admin" | "user" | "readonly";
  owner_id: number | null;
  created_at: string;
};

export type PrintDocument = {
  book: import("@mistakebook/shared").PrintBookSummary;
  pages: Array<{
    page_no: number;
    sheet: number;
    subject: string;
    problems: import("@mistakebook/shared").ProblemDetail[];
  }>;
  pagination: "logical";
};

export const api = {
  register: (username: string, password: string) =>
    request<{ user: PublicUser; token: string; expires_in: number }>("/api/v1/auth/register", {
      method: "POST",
      body: JSON.stringify({ username, password }),
    }),
  login: (username: string, password: string) =>
    request<{ user: PublicUser; token: string; expires_in: number }>("/api/v1/auth/login", {
      method: "POST",
      body: JSON.stringify({ username, password }),
    }),
  logout: () => request<{ ok: boolean }>("/api/v1/auth/logout", { method: "POST" }),
  me: () => request<{ user: PublicUser }>("/api/v1/auth/me"),
  changeUsername: (username: string, current_password: string) =>
    request<{ user: PublicUser }>("/api/v1/auth/username", {
      method: "POST",
      body: JSON.stringify({ username, current_password }),
    }),
  changePassword: (current_password: string, new_password: string) =>
    request<{ ok: boolean; token: string }>("/api/v1/auth/password", {
      method: "POST",
      body: JSON.stringify({ current_password, new_password }),
    }),
  subaccounts: () => request<{ items: PublicUser[]; limit: number }>("/api/v1/subaccounts"),
  createSubaccount: (username: string, password: string) =>
    request<PublicUser>("/api/v1/subaccounts", {
      method: "POST",
      body: JSON.stringify({ username, password }),
    }),
  deleteSubaccount: (id: number) => request<void>(`/api/v1/subaccounts/${id}`, { method: "DELETE" }),
  exportData: async () => {
    const data = await request<unknown>("/api/v1/export");
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: "application/json;charset=utf-8" });
    const href = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = href;
    anchor.download = `错题本-全部数据-${new Date().toISOString().slice(0, 10)}.json`;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    window.setTimeout(() => URL.revokeObjectURL(href), 1000);
  },
  subjects: () => request<{ items: Array<{ name: string }> }>("/api/v1/subjects"),
  taxonomy: () =>
    request<import("@mistakebook/shared").TaxonomyResponse>("/api/v1/taxonomy"),
  tags: (subject?: string) =>
    request<{ items: import("@mistakebook/shared").TagRef[] }>(
      `/api/v1/tags${subject ? `?subject=${encodeURIComponent(subject)}` : ""}`,
    ),
  duplicates: () =>
    request<import("@mistakebook/shared").DuplicatesResponse>("/api/v1/tags/duplicates"),
  suggestTags: (subject: string, names: string[]) =>
    request<import("@mistakebook/shared").SuggestTagsResponse>("/api/v1/tags/suggest", {
      method: "POST",
      body: JSON.stringify({ subject, names }),
    }),
  mergeTags: (source_tag_id: number, target_tag_id: number) =>
    request<import("@mistakebook/shared").MergeTagsResponse>("/api/v1/tags/merge", {
      method: "POST",
      body: JSON.stringify({ source_tag_id, target_tag_id }),
    }),
  listProblems: (params: Record<string, string | number | undefined>) => {
    const qs = new URLSearchParams();
    for (const [key, value] of Object.entries(params)) {
      if (value != null && value !== "") qs.set(key, String(value));
    }
    const suffix = qs.size ? `?${qs}` : "";
    return request<import("@mistakebook/shared").ProblemListResponse>(`/api/v1/problems${suffix}`);
  },
  getProblem: (id: number) =>
    request<import("@mistakebook/shared").ProblemDetail>(`/api/v1/problems/${id}`),
  listProblemLogs: (id: number) =>
    request<import("@mistakebook/shared").ChangeLogListResponse>(`/api/v1/problems/${id}/logs`),
  createProblem: (payload: import("@mistakebook/shared").ProblemCreate) =>
    request<import("@mistakebook/shared").ProblemDetail>("/api/v1/problems", {
      method: "POST",
      body: JSON.stringify(payload),
    }),
  updateProblem: (id: number, payload: import("@mistakebook/shared").ProblemUpdate) =>
    request<import("@mistakebook/shared").ProblemDetail>(`/api/v1/problems/${id}`, {
      method: "PATCH",
      body: JSON.stringify(payload),
    }),
  deleteProblem: (id: number) =>
    request<void>(`/api/v1/problems/${id}`, { method: "DELETE" }),
  search: (payload: {
    query: string;
    mode?: string;
    subject?: string;
    tag?: string;
    target?: string;
  }) =>
    request<import("@mistakebook/shared").SearchResponse>("/api/v1/search", {
      method: "POST",
      body: JSON.stringify(payload),
    }),
  listNoteToc: () =>
    request<{ items: import("@mistakebook/shared").NoteTocNode[] }>("/api/v1/notes/toc"),
  getNoteNode: (id: number) =>
    request<import("@mistakebook/shared").NoteNodeDetail>(`/api/v1/notes/${id}`),
  listNoteLogs: (id: number) =>
    request<import("@mistakebook/shared").ChangeLogListResponse>(`/api/v1/notes/${id}/logs`),
  listNoteVersions: (id: number) =>
    request<import("@mistakebook/shared").NoteVersionListResponse>(`/api/v1/notes/${id}/versions`),
  getNoteVersion: (id: number, versionId: number) =>
    request<import("@mistakebook/shared").NoteVersion>(`/api/v1/notes/${id}/versions/${versionId}`),
  diffNoteVersion: (id: number, versionId: number, against?: "previous" | "current" | number) => {
    const qs = against == null ? "" : `?against=${encodeURIComponent(String(against))}`;
    return request<import("@mistakebook/shared").NoteVersionDiff>(
      `/api/v1/notes/${id}/versions/${versionId}/diff${qs}`,
    );
  },
  diffNoteVersions: (id: number, from: number, to: number | "current") =>
    request<import("@mistakebook/shared").NoteVersionDiff>(
      `/api/v1/notes/${id}/diff?from=${from}&to=${encodeURIComponent(String(to))}`,
    ),
  getNoteGraph: (includeProblems = true) =>
    request<import("@mistakebook/shared").NoteGraph>(
      `/api/v1/notes/graph${includeProblems ? "" : "?problems=0"}`,
    ),
  listNoteLinks: (kind?: "note" | "problem", id?: number) => {
    const qs = new URLSearchParams();
    if (kind) qs.set("kind", kind);
    if (id != null) qs.set("id", String(id));
    const suffix = qs.size ? `?${qs}` : "";
    return request<{ items: import("@mistakebook/shared").NoteLink[] }>(`/api/v1/notes/links${suffix}`);
  },
  listPrintBooks: () =>
    request<{ items: import("@mistakebook/shared").PrintBookSummary[] }>("/api/v1/print/books"),
  createPrintBook: (payload: import("@mistakebook/shared").PrintBookCreate) =>
    request<import("@mistakebook/shared").PrintBookDetail>("/api/v1/print/books", {
      method: "POST",
      body: JSON.stringify(payload),
    }),
  getPrintBook: (id: number) =>
    request<import("@mistakebook/shared").PrintBookDetail>(`/api/v1/print/books/${id}`),
  deletePrintBook: (id: number) =>
    request<void>(`/api/v1/print/books/${id}`, { method: "DELETE" }),
  planPrintBook: (id: number) =>
    request<import("@mistakebook/shared").PrintPlan>(`/api/v1/print/books/${id}/plan`),
  printDocument: (id: number, pages?: number[]) =>
    request<PrintDocument>(`/api/v1/print/books/${id}/document${pages?.length ? `?page_nos=${pages.join(",")}` : ""}`),
  applyPrintBook: (id: number) => request<{ kind: string; pages: number[] }>(`/api/v1/print/books/${id}/apply`, { method: "POST" }),
  reprintPrintBook: (id: number, page_nos: number[]) =>
    request<{ kind: string; pages: number[] }>(`/api/v1/print/books/${id}/reprint`, {
      method: "POST",
      body: JSON.stringify({ page_nos }),
    }),
};
