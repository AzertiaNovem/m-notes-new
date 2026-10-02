import { createContext, useContext, useEffect, useState, type ReactNode } from "react";
import { api, getToken, setToken, type PublicUser } from "./api.ts";

type AuthContextValue = {
  user: PublicUser | null;
  loading: boolean;
  login: (username: string, password: string) => Promise<void>;
  register: (username: string, password: string) => Promise<void>;
  logout: () => Promise<void>;
  refresh: () => Promise<void>;
};

const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<PublicUser | null>(null);
  const [loading, setLoading] = useState(true);

  async function refresh() {
    if (!getToken()) {
      setUser(null);
      setLoading(false);
      return;
    }
    try {
      const me = await api.me();
      setUser(me.user);
    } catch {
      setToken(null);
      setUser(null);
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    void refresh();
  }, []);

  return (
    <AuthContext.Provider
      value={{
        user,
        loading,
        login: async (username, password) => {
          const res = await api.login(username, password);
          setToken(res.token);
          setUser(res.user);
        },
        register: async (username, password) => {
          const res = await api.register(username, password);
          setToken(res.token);
          setUser(res.user);
        },
        logout: async () => {
          try {
            await api.logout();
          } catch {
            // ignore
          }
          setToken(null);
          setUser(null);
        },
        refresh,
      }}
    >
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth(): AuthContextValue {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error("useAuth must be used within AuthProvider");
  return ctx;
}
