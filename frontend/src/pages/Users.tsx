import { Fragment, useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { Permission, Role, UserRow } from "../lib/types";
import { dateTime, humanize } from "../lib/format";
import { validatePassword } from "../lib/validation";
import { Banner, Button, Empty, Loading, Modal, PageHeader, StatusBadge, Tabs, TextField, useAsync, useToast } from "../components/ui";
import { IconPlus } from "../components/icons";

export function UsersPage() {
  const { can } = useAuth();
  const [tab, setTab] = useState<"users" | "roles">("users");
  return (
    <>
      <PageHeader title="Users & roles" subtitle="Access is denied unless a role grants the permission. Every change here is written to the audit log." />
      <Tabs tabs={[{ id: "users", label: "Users" }, ...(can("role.manage") ? [{ id: "roles" as const, label: "Roles & permissions" }] : [])]} value={tab} onChange={setTab} />
      {tab === "users" ? <UsersTab /> : <RolesTab />}
    </>
  );
}

function UsersTab() {
  const { user: me } = useAuth();
  const toast = useToast();
  const users = useAsync(() => api.get<UserRow[]>("/users"), []);
  const roles = useAsync(() => api.get<Role[]>("/roles"), []);
  const [editing, setEditing] = useState<UserRow | "new" | null>(null);
  const [resetting, setResetting] = useState<UserRow | null>(null);
  if (users.loading) return <Loading />;
  return (
    <>
      <div className="row" style={{ justifyContent: "flex-end", marginBottom: 12 }}><Button variant="primary" onClick={() => setEditing("new")}><IconPlus /> Add user</Button></div>
      <div className="table-wrap">
        <table className="data">
          <thead><tr><th>Name</th><th>Username</th><th>Roles</th><th>Status</th><th>Last sign-in</th><th className="right">Actions</th></tr></thead>
          <tbody>
            {users.data?.map((u) => (
              <tr key={u.id}>
                <td><div className="cell-main">{u.fullName}</div>{u.doctorId && <div className="cell-sub">Doctor profile linked</div>}</td>
                <td>{u.username}</td>
                <td>{u.roles.map((r) => humanize(r)).join(", ")}</td>
                <td>{!u.isActive ? <StatusBadge status="INACTIVE" label="Deactivated" /> : u.lockedUntil ? <span className="badge b-red">Locked</span> : <span className="badge b-green">Active</span>}</td>
                <td className="small nowrap">{dateTime(u.lastLoginAt)}</td>
                <td className="right nowrap">
                  <Button size="sm" variant="ghost" onClick={() => setEditing(u)}>Edit</Button>
                  <Button size="sm" variant="ghost" onClick={() => setResetting(u)}>Reset password</Button>
                  {u.id !== me?.id && (
                    <Button size="sm" variant="ghost" onClick={async () => {
                      try { await api.put(`/users/${u.id}`, { fullName: u.fullName, email: u.email, phone: u.phone, isActive: !u.isActive }); toast(u.isActive ? "User deactivated" : "User reactivated"); users.reload(); }
                      catch (e) { toast((e as ApiError).message, "error"); }
                    }}>{u.isActive ? "Deactivate" : "Reactivate"}</Button>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {editing && <UserModal user={editing === "new" ? null : editing} roles={roles.data ?? []} onClose={() => setEditing(null)} onSaved={() => { setEditing(null); users.reload(); }} />}
      {resetting && <ResetModal user={resetting} onClose={() => setResetting(null)} onDone={() => { setResetting(null); users.reload(); }} />}
    </>
  );
}

function UserModal({ user, roles, onClose, onSaved }: { user: UserRow | null; roles: Role[]; onClose: () => void; onSaved: () => void }) {
  const toast = useToast();
  const [f, setF] = useState({ username: user?.username ?? "", fullName: user?.fullName ?? "", email: user?.email ?? "", phone: user?.phone ?? "", password: "", roles: user?.roles ?? ["RECEPTIONIST"] });
  const [err, setErr] = useState<ApiError | null>(null);
  const pwErr = !user && f.password ? validatePassword(f.password, f.username) : null;
  const save = async () => {
    try {
      if (user) await api.put(`/users/${user.id}`, { fullName: f.fullName, email: f.email, phone: f.phone, roles: f.roles });
      else await api.post("/users", f);
      toast(user ? "User updated" : `User ${f.username} created; they must change the password at first sign-in`);
      onSaved();
    } catch (e) { setErr(e as ApiError); }
  };
  return (
    <Modal title={user ? `Edit ${user.fullName}` : "Add user"} onClose={onClose} footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" onClick={save} disabled={!!pwErr || !f.roles.length}>{user ? "Save changes" : "Create user"}</Button></>}>
      {err && <Banner kind="error">{err.message}</Banner>}
      <div className="grid g2">
        <TextField label="Full name" required value={f.fullName} onChange={(v) => setF({ ...f, fullName: v })} error={err?.fields.fullName} className="span-all" />
        <TextField label="Username" required value={f.username} onChange={(v) => setF({ ...f, username: v.trim() })} disabled={!!user} error={err?.fields.username} />
        {!user && <TextField label="Temporary password" required type="password" value={f.password} onChange={(v) => setF({ ...f, password: v })} error={pwErr ?? err?.fields.password} hint="The user changes it at first sign-in" autoComplete="new-password" />}
        <TextField label="Email" value={f.email} onChange={(v) => setF({ ...f, email: v })} error={err?.fields.email} />
        <TextField label="Phone" value={f.phone} onChange={(v) => setF({ ...f, phone: v })} />
        <fieldset className="fieldset span-all" style={{ marginBottom: 0 }}>
          <legend className="small" style={{ fontWeight: 600, color: "var(--ink-2)" }}>Roles</legend>
          <div className="stack" style={{ gap: 6 }}>
            {roles.map((r) => (
              <label key={r.id} className="check">
                <input type="checkbox" checked={f.roles.includes(r.code)} onChange={(e) => setF({ ...f, roles: e.target.checked ? [...f.roles, r.code] : f.roles.filter((x) => x !== r.code) })} />
                <span><strong>{r.name}</strong> <span className="muted">{r.description}</span></span>
              </label>
            ))}
          </div>
        </fieldset>
      </div>
      <p className="small muted" style={{ marginTop: 12 }}>Doctors also need a doctor profile (registration number, fee, working hours) under Clinic settings.</p>
    </Modal>
  );
}

function ResetModal({ user, onClose, onDone }: { user: UserRow; onClose: () => void; onDone: () => void }) {
  const toast = useToast();
  const [pw, setPw] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const policy = pw ? validatePassword(pw, user.username) : "Enter a temporary password";
  return (
    <Modal title={`Reset password for ${user.fullName}`} onClose={onClose}
      footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" disabled={!!policy} onClick={async () => {
        try { await api.post(`/users/${user.id}/reset-password`, { newPassword: pw }); toast("Password reset. The user's sessions were signed out."); onDone(); }
        catch (e) { setErr((e as ApiError).message); }
      }}>Reset password</Button></>}>
      {err && <Banner kind="error">{err}</Banner>}
      <p className="small">The user will be signed out everywhere, any lockout is cleared, and they must choose a new password at next sign-in.</p>
      <TextField label="Temporary password" type="password" value={pw} onChange={setPw} error={pw ? policy ?? undefined : undefined} autoComplete="new-password" />
    </Modal>
  );
}

function RolesTab() {
  const toast = useToast();
  const roles = useAsync(() => api.get<Role[]>("/roles"), []);
  const perms = useAsync(() => api.get<Permission[]>("/permissions"), []);
  const [draft, setDraft] = useState<Record<string, string[]>>({});
  const [adding, setAdding] = useState(false);
  if (roles.loading || perms.loading) return <Loading />;
  if (!roles.data || !perms.data) return <Empty title="Could not load roles" />;
  const modules = [...new Set(perms.data.map((p) => p.module))];
  const current = (r: Role) => draft[r.id] ?? r.permissions;
  const toggle = (r: Role, code: string) => {
    const cur = current(r);
    setDraft({ ...draft, [r.id]: cur.includes(code) ? cur.filter((c) => c !== code) : [...cur, code] });
  };
  const changed = roles.data.filter((r) => draft[r.id] && JSON.stringify([...draft[r.id]].sort()) !== JSON.stringify([...r.permissions].sort()));
  const save = async () => {
    try {
      for (const r of changed) await api.put(`/roles/${r.id}`, { permissions: draft[r.id] });
      toast("Permissions updated");
      setDraft({});
      roles.reload();
    } catch (e) { toast((e as ApiError).message, "error"); }
  };
  return (
    <>
      <div className="row" style={{ justifyContent: "space-between", marginBottom: 12 }}>
        <p className="small muted" style={{ margin: 0 }}>Changes apply on the user's next request. At least one active user must keep user management.</p>
        <div className="row">
          <Button onClick={() => setAdding(true)}><IconPlus /> New role</Button>
          <Button variant="primary" disabled={!changed.length} onClick={save}>Save {changed.length ? `${changed.length} role${changed.length > 1 ? "s" : ""}` : "changes"}</Button>
        </div>
      </div>
      <div className="table-wrap">
        <table className="data">
          <thead><tr><th>Permission</th>{roles.data.map((r) => <th key={r.id} style={{ textAlign: "center" }}>{r.name}<div className="cell-sub">{r.userCount} users</div></th>)}</tr></thead>
          <tbody>
            {modules.map((m) => (
              <Fragment key={m}>
                <tr><td colSpan={roles.data!.length + 1} style={{ background: "var(--paper)", fontWeight: 650 }}>{humanize(m)}</td></tr>
                {perms.data!.filter((p) => p.module === m).map((p) => (
                  <tr key={p.code}>
                    <td><div>{p.description}</div><div className="cell-sub">{p.code}</div></td>
                    {roles.data!.map((r) => (
                      <td key={r.id} style={{ textAlign: "center" }}>
                        <input type="checkbox" aria-label={`${r.name}: ${p.description}`} checked={current(r).includes(p.code)} onChange={() => toggle(r, p.code)} style={{ accentColor: "var(--blue)", width: 16, height: 16 }} />
                      </td>
                    ))}
                  </tr>
                ))}
              </Fragment>
            ))}
          </tbody>
        </table>
      </div>
      {adding && <NewRoleModal onClose={() => setAdding(false)} onDone={() => { setAdding(false); roles.reload(); }} />}
    </>
  );
}

function NewRoleModal({ onClose, onDone }: { onClose: () => void; onDone: () => void }) {
  const [code, setCode] = useState("");
  const [name, setName] = useState("");
  const [desc, setDesc] = useState("");
  const [err, setErr] = useState<ApiError | null>(null);
  return (
    <Modal title="New role" onClose={onClose} footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" onClick={async () => {
      try { await api.post("/roles", { code, name, description: desc, permissions: [] }); onDone(); } catch (e) { setErr(e as ApiError); }
    }}>Create role</Button></>}>
      {err && <Banner kind="error">{err.message}</Banner>}
      <div className="stack">
        <TextField label="Name" value={name} onChange={(v) => { setName(v); setCode(v.toUpperCase().replace(/[^A-Z0-9]+/g, "_").replace(/^_|_$/g, "")); }} placeholder="e.g. Nurse" />
        <TextField label="Code" value={code} onChange={(v) => setCode(v.toUpperCase())} error={err?.fields.code} hint="Uppercase letters, numbers and underscores" />
        <TextField label="Description" value={desc} onChange={setDesc} />
      </div>
      <p className="small muted" style={{ marginTop: 10 }}>New roles start with no permissions. Grant them in the permissions table.</p>
    </Modal>
  );
}
