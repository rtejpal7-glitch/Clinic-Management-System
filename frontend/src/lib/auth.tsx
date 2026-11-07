import { createContext, useCallback, useContext, useEffect, useRef, useState, type ReactNode } from "react";
import { api, hasToken, onAuthError, setToken } from "./api";
import type { CurrentUser } from "./types";

interface AuthState {
  user: CurrentUser | null;
  loading: boolean;
  notice: string | null;
  login: (username: string, password: string) => Promise<CurrentUser>;
  logout: (reason?: string) => Promise<void>;
  refresh: () => Promise<void>;
  can: (perm: string) => boolean;
}

const Ctx = createContext<AuthState | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<CurrentUser | null>(null);
  const [loading, setLoading] = useState(hasToken());
  const [notice, setNotice] = useState<string | null>(null);
  const idleTimer = useRef<number | undefined>(undefined);

  const refresh = useCallback(async () => {
    if (!hasToken()) {
      setUser(null);
      setLoading(false);
      return;
    }
    try {
      setUser(await api.get<CurrentUser>("/auth/me"));
    } catch {
      setToken(null);
      setUser(null);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    refresh();
  }, [refresh]);

  useEffect(
    () =>
      onAuthError((e) => {
        if (e.code === "PASSWORD_CHANGE_REQUIRED") {
          refresh();
          return;
        }
        setToken(null);
        setUser(null);
        setNotice("Your session ended. Sign in again to continue.");
      }),
    [refresh],
  );

  const logout = useCallback(async (reason?: string) => {
    try {
      await api.post("/auth/logout");
    } catch {
      /* already invalid */
    }
    setToken(null);
    setUser(null);
    setNotice(reason ?? null);
  }, []);

  // Client-side idle logout mirrors the server-side idle timeout (shared workstations).
  useEffect(() => {
    if (!user) return;
    const ms = (user.sessionIdleMinutes || 30) * 60000;
    const reset = () => {
      window.clearTimeout(idleTimer.current);
      idleTimer.current = window.setTimeout(() => logout("You were signed out after a period of inactivity."), ms);
    };
    const events = ["mousedown", "keydown", "touchstart"];
    events.forEach((ev) => window.addEventListener(ev, reset));
    reset();
    return () => {
      window.clearTimeout(idleTimer.current);
      events.forEach((ev) => window.removeEventListener(ev, reset));
    };
  }, [user, logout]);

  const login = useCallback(async (username: string, password: string) => {
    const res = await api.post<{ token: string; user: CurrentUser }>("/auth/login", { username, password });
    setToken(res.token);
    setUser(res.user);
    setNotice(null);
    return res.user;
  }, []);

  const can = useCallback((perm: string) => !!user?.permissions.includes(perm), [user]);

  return <Ctx.Provider value={{ user, loading, notice, login, logout, refresh, can }}>{children}</Ctx.Provider>;
}

export function useAuth() {
  const c = useContext(Ctx);
  if (!c) throw new Error("useAuth outside AuthProvider");
  return c;
}
