import { useState, type FormEvent } from "react";
import { useAuth } from "../lib/auth";
import { api, ApiError } from "../lib/api";
import { validatePassword } from "../lib/validation";
import { Banner, Button, PageHeader, TextField, useToast } from "../components/ui";
import { navigate } from "../lib/router";

export function LoginPage() {
  const { login, notice } = useAuth();
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (!username || !password) {
      setError("Enter your username and password.");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      await login(username.trim(), password);
      navigate("/");
    } catch (err) {
      setError(err instanceof ApiError ? err.message : "Sign-in failed.");
      setPassword("");
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="login-wrap">
      <section className="login-art" aria-hidden="true">
        <div className="rx">℞</div>
        <div>
          <h1>The front desk and the consulting room, on one record.</h1>
          <ul>
            <li>Register patients with duplicate checks and an internal patient ID</li>
            <li>Book slots, issue queue tokens, run the waiting room</li>
            <li>Structured OPD notes, e-prescriptions and investigation orders</li>
            <li>Bills, payments and a tamper-evident audit trail</li>
          </ul>
        </div>
        <p className="small" style={{ color: "#8d99b0", margin: 0 }}>
          Proof of concept. Not NABH- or ABDM-certified. ABDM features run on a mock adapter.
        </p>
      </section>
      <section className="login-form">
        <form className="login-card stack" onSubmit={submit} noValidate>
          <div>
            <h2>Sign in to ClinicFlow</h2>
            <p className="muted small">Use the account your clinic administrator created for you.</p>
          </div>
          {notice && <Banner kind="info">{notice}</Banner>}
          {error && <Banner kind="error">{error}</Banner>}
          <TextField label="Username" value={username} onChange={setUsername} autoComplete="username" autoFocus name="username" />
          <TextField label="Password" type="password" value={password} onChange={setPassword} autoComplete="current-password" name="password" />
          <Button variant="primary" type="submit" busy={busy}>Sign in</Button>
          <div className="demo-creds">
            Demo accounts (synthetic data only)
            <table>
              <tbody>
                <tr><td>Administrator</td><td><code>admin</code></td></tr>
                <tr><td>Doctor</td><td><code>dr.meera</code></td></tr>
                <tr><td>Receptionist</td><td><code>reception</code></td></tr>
              </tbody>
            </table>
            Password for all demo accounts: <code>Clinic-Demo-2026</code>
          </div>
        </form>
      </section>
    </div>
  );
}

export function ChangePasswordPage({ forced }: { forced?: boolean }) {
  const { user, refresh, logout } = useAuth();
  const toast = useToast();
  const [current, setCurrent] = useState("");
  const [next, setNext] = useState("");
  const [confirm, setConfirm] = useState("");
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [busy, setBusy] = useState(false);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    const errs: Record<string, string> = {};
    const policy = validatePassword(next, user?.username ?? "");
    if (!current) errs.currentPassword = "Enter your current password";
    if (policy) errs.newPassword = policy;
    if (next !== confirm) errs.confirm = "Passwords don't match";
    setErrors(errs);
    if (Object.keys(errs).length) return;
    setBusy(true);
    try {
      await api.post("/auth/change-password", { currentPassword: current, newPassword: next });
      toast("Password changed. Other sessions were signed out.");
      await refresh();
      navigate("/");
    } catch (err) {
      if (err instanceof ApiError) setErrors({ ...err.fields, form: err.message });
    } finally {
      setBusy(false);
    }
  };

  const form = (
    <form className="stack" onSubmit={submit} noValidate style={{ maxWidth: 380 }}>
      {forced && <Banner kind="warn">Your account has a temporary password. Choose a new password to continue.</Banner>}
      {errors.form && <Banner kind="error">{errors.form}</Banner>}
      <TextField label="Current password" type="password" value={current} onChange={setCurrent} error={errors.currentPassword} autoComplete="current-password" />
      <TextField label="New password" type="password" value={next} onChange={setNext} error={errors.newPassword ?? errors.password}
        hint="At least 10 characters, with letters and numbers" autoComplete="new-password" />
      <TextField label="Confirm new password" type="password" value={confirm} onChange={setConfirm} error={errors.confirm} autoComplete="new-password" />
      <div className="row">
        <Button variant="primary" type="submit" busy={busy}>Change password</Button>
        {forced && <Button type="button" variant="ghost" onClick={() => logout()}>Sign out</Button>}
      </div>
    </form>
  );
  if (forced)
    return (
      <div className="login-form" style={{ minHeight: "100vh" }}>
        <div className="login-card stack">
          <h2>Set a new password</h2>
          {form}
        </div>
      </div>
    );
  return (
    <>
      <PageHeader title="Change password" subtitle="Changing your password signs you out on other devices." />
      <div className="panel"><div className="panel-body">{form}</div></div>
    </>
  );
}
