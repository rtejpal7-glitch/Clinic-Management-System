// API client. The bearer token lives in sessionStorage (cleared when the tab
// closes) — see docs/SECURITY.md for the trade-off versus HttpOnly cookies.

const TOKEN_KEY = "cf.session";

export class ApiError extends Error {
  status: number;
  code: string;
  fields: Record<string, string>;
  data: any;
  constructor(status: number, code: string, message: string, fields: Record<string, string> = {}, data: any = null) {
    super(message);
    this.status = status;
    this.code = code;
    this.fields = fields;
    this.data = data;
  }
}

let token: string | null = null;
try {
  token = sessionStorage.getItem(TOKEN_KEY);
} catch {
  token = null;
}

export function setToken(t: string | null) {
  token = t;
  try {
    if (t) sessionStorage.setItem(TOKEN_KEY, t);
    else sessionStorage.removeItem(TOKEN_KEY);
  } catch {
    /* storage unavailable: keep in memory only */
  }
}
export const hasToken = () => !!token;

type Listener = (e: ApiError) => void;
const authListeners = new Set<Listener>();
export function onAuthError(l: Listener) {
  authListeners.add(l);
  return () => {
    authListeners.delete(l);
  };
}

function query(params?: Record<string, unknown>) {
  if (!params) return "";
  const q = new URLSearchParams();
  for (const [k, v] of Object.entries(params)) if (v !== undefined && v !== null && v !== "") q.set(k, String(v));
  const s = q.toString();
  return s ? `?${s}` : "";
}

async function request<T>(method: string, path: string, body?: unknown, params?: Record<string, unknown>): Promise<T> {
  const headers: Record<string, string> = { Accept: "application/json" };
  if (body !== undefined) headers["Content-Type"] = "application/json";
  if (token) headers.Authorization = `Bearer ${token}`;
  let res: Response;
  try {
    res = await fetch(`/api/v1${path}${query(params)}`, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
  } catch {
    throw new ApiError(0, "NETWORK", "Cannot reach the server. Check your connection and try again.");
  }
  const text = await res.text();
  let json: any = null;
  try {
    json = text ? JSON.parse(text) : null;
  } catch {
    json = null;
  }
  if (!res.ok) {
    const e = json?.error ?? {};
    const err = new ApiError(res.status, e.code ?? "HTTP_" + res.status, e.message ?? `Request failed (${res.status})`, e.fields ?? {}, e.data ?? null);
    if (res.status === 401 || err.code === "PASSWORD_CHANGE_REQUIRED") authListeners.forEach((l) => l(err));
    throw err;
  }
  return json as T;
}

export const api = {
  get: <T>(path: string, params?: Record<string, unknown>) => request<T>("GET", path, undefined, params),
  post: <T>(path: string, body: unknown = {}) => request<T>("POST", path, body),
  put: <T>(path: string, body: unknown = {}) => request<T>("PUT", path, body),
};
