import { useEffect, useState, type ReactNode } from "react";
import { useAuth } from "./lib/auth";
import { match, navigate, useRoute } from "./lib/router";
import { api } from "./lib/api";
import type { Settings } from "./lib/types";
import { Loading, MockFlag, Empty } from "./components/ui";
import {
  IconCalendar, IconClock, IconHome, IconLog, IconLogo, IconLogout, IconMenu, IconQueue, IconReceipt, IconSettings, IconShield,
  IconStethoscope, IconUserPlus, IconUsers,
} from "./components/icons";
import { LoginPage, ChangePasswordPage } from "./pages/Login";
import { DashboardPage } from "./pages/Dashboard";
import { PatientDirectoryPage } from "./pages/PatientDirectory";
import { PatientFormPage } from "./pages/PatientForm";
import { PatientProfilePage } from "./pages/PatientProfile";
import { AppointmentsPage } from "./pages/Appointments";
import { DoctorSchedulePage } from "./pages/DoctorSchedule";
import { QueuePage } from "./pages/Queue";
import { ConsultationPage } from "./pages/Consultation";
import { ConsultationPrintPage } from "./pages/ConsultationPrint";
import { BillingPage, BillDetailPage, NewBillPage } from "./pages/Billing";
import { UsersPage } from "./pages/Users";
import { AuditPage } from "./pages/Audit";
import { SettingsPage } from "./pages/Settings";
import { PatientSearchBox } from "./pages/PatientSearchBox";
import { InsightsPage } from "./pages/Insights";
import { ClinicalContentPage } from "./pages/ClinicalContent";
import { ClinicsPage } from "./pages/Clinics";

interface NavItem { to: string; label: string; icon: ReactNode; perm?: string; match: string[] }
const NAV: { title: string; items: NavItem[] }[] = [
  { title: "Today", items: [
    { to: "/", label: "Dashboard", icon: <IconHome />, perm: "dashboard.read", match: [""] },
    { to: "/queue", label: "Waiting queue", icon: <IconQueue />, perm: "appointment.read", match: ["queue"] },
    { to: "/appointments", label: "Appointments", icon: <IconCalendar />, perm: "appointment.read", match: ["appointments"] },
    { to: "/insights", label: "Practice insights", icon: <IconStethoscope />, perm: "insights.read", match: ["insights"] },
  ] },
  { title: "Patients", items: [
    { to: "/patients", label: "Patient directory", icon: <IconUsers />, perm: "patient.read", match: ["patients"] },
    { to: "/patients/new", label: "Register patient", icon: <IconUserPlus />, perm: "patient.create", match: ["patients/new"] },
  ] },
  { title: "Clinic", items: [
    { to: "/schedule", label: "Doctor schedule", icon: <IconClock />, perm: "schedule.read", match: ["schedule"] },
    { to: "/consultations", label: "My consultations", icon: <IconStethoscope />, perm: "clinical.write", match: ["consultations"] },
    { to: "/billing", label: "Billing", icon: <IconReceipt />, perm: "billing.read", match: ["billing", "bills"] },
  ] },
  { title: "Administration", items: [
    { to: "/users", label: "Users & roles", icon: <IconShield />, perm: "user.manage", match: ["users"] },
    { to: "/audit", label: "Audit log", icon: <IconLog />, perm: "audit.read", match: ["audit"] },
    { to: "/settings", label: "Clinic settings", icon: <IconSettings />, perm: "settings.manage", match: ["settings"] },
    { to: "/clinics", label: "Clinic & network", icon: <IconUsers />, perm: "settings.manage", match: ["clinics"] },
    { to: "/clinical-content", label: "Clinical content", icon: <IconShield />, perm: "cds.review|catalogue.manage", match: ["clinical-content"] },
  ] },
];

function isActive(item: NavItem, path: string) {
  const p = path.replace(/^\//, "");
  if (item.to === "/patients/new") return p === "patients/new";
  if (item.to === "/patients") return p.startsWith("patients") && p !== "patients/new";
  return item.match.some((m) => (m === "" ? p === "" : p === m || p.startsWith(m + "/")));
}

export function App() {
  const { user, loading } = useAuth();
  if (loading) return <Loading label="Starting ClinicFlow" />;
  if (!user) return <LoginPage />;
  if (user.mustChangePassword) return <ChangePasswordPage forced />;
  return <Shell />;
}

function Shell() {
  const { user, logout, can } = useAuth();
  const route = useRoute();
  const [settings, setSettings] = useState<Settings | null>(null);
  const [open, setOpen] = useState(false);
  useEffect(() => {
    api.get<Settings>("/settings").then(setSettings).catch(() => {});
  }, []);
  useEffect(() => setOpen(false), [route.path]);

  const roleLabel = user!.roles.map((r) => r.charAt(0) + r.slice(1).toLowerCase()).join(", ");
  return (
    <div className="shell">
      <aside className={`sidebar ${open ? "open" : ""}`} aria-label="Main navigation">
        <div className="brand">
          <div className="brand-mark"><IconLogo /></div>
          <div>
            <div className="brand-name">ClinicFlow</div>
            <div className="brand-clinic">{settings?.clinicName ?? " "}</div>
          </div>
        </div>
        {NAV.map((g) => {
          const items = g.items.filter((i) => !i.perm || i.perm.split("|").some((p) => can(p)));
          if (!items.length) return null;
          return (
            <nav className="nav-group" key={g.title} aria-label={g.title}>
              <div className="nav-group-title">{g.title}</div>
              {items.map((i) => (
                <a key={i.to} href={"#" + i.to} className={`nav-link ${isActive(i, route.path) ? "active" : ""}`} aria-current={isActive(i, route.path) ? "page" : undefined}>
                  {i.icon}
                  {i.label}
                </a>
              ))}
            </nav>
          );
        })}
        <div className="sidebar-foot">
          <div className="user-chip">
            <strong>{user!.fullName}</strong>
            <span>{roleLabel}</span>
          </div>
          <a href="#/account" className="nav-link">Change password</a>
          <button className="nav-link btn-ghost" style={{ border: 0, background: "none", width: "100%", cursor: "pointer", font: "inherit" }} onClick={() => logout()}>
            <IconLogout /> Sign out
          </button>
        </div>
      </aside>
      <div className="main">
        <header className="topbar">
          <button className="icon-btn menu-btn" onClick={() => setOpen((o) => !o)} aria-label="Open navigation"><IconMenu /></button>
          {can("patient.read") && <div className="search"><PatientSearchBox /></div>}
          <div className="topbar-right">
            <MockFlag>ABDM: mock mode</MockFlag>
          </div>
        </header>
        <main className="content" id="main">
          <Routes path={route.path} query={route.query} />
        </main>
      </div>
    </div>
  );
}

function Routes({ path, query }: { path: string; query: URLSearchParams }) {
  const { can, user } = useAuth();
  const guard = (perm: string, node: ReactNode) =>
    can(perm) ? node : <Empty title="You don't have access to this page">Ask an administrator if you need this permission.</Empty>;
  let m: Record<string, string> | null;
  if (path === "/" || path === "") {
    if (!can("dashboard.read")) return <Empty title="Welcome to ClinicFlow" />;
    return <DashboardPage />;
  }
  if (path === "/account") return <ChangePasswordPage />;
  if (path === "/patients") return guard("patient.read", <PatientDirectoryPage initialQuery={query.get("q") ?? ""} />);
  if (path === "/patients/new") return guard("patient.create", <PatientFormPage />);
  if ((m = match("/patients/:id/edit", path))) return guard("patient.update", <PatientFormPage patientId={m.id} />);
  if ((m = match("/patients/:id", path))) return guard("patient.read", <PatientProfilePage id={m.id} tab={query.get("tab") ?? "overview"} />);
  if (path === "/appointments") return guard("appointment.read", <AppointmentsPage />);
  if (path === "/schedule") return guard("schedule.read", <DoctorSchedulePage />);
  if (path === "/queue") return guard("appointment.read", <QueuePage />);
  if (path === "/consultations") {
    if (user?.doctorId) {
      navigate("/queue");
      return null;
    }
    return <Empty title="No doctor profile">Consultations are recorded by users with a doctor profile.</Empty>;
  }
  if ((m = match("/consultations/:id/print", path))) return guard("clinical.read", <ConsultationPrintPage id={m.id} kind={query.get("doc") === "summary" ? "summary" : "rx"} />);
  if ((m = match("/consultations/:id", path))) return guard("clinical.read", <ConsultationPage id={m.id} />);
  if (path === "/billing") return guard("billing.read", <BillingPage />);
  if (path === "/bills/new") return guard("billing.manage", <NewBillPage patientId={query.get("patientId")} appointmentId={query.get("appointmentId")} />);
  if ((m = match("/bills/:id", path))) return guard("billing.read", <BillDetailPage id={m.id} print={query.get("print") === "1"} />);
  if (path === "/users") return guard("user.manage", <UsersPage />);
  if (path === "/audit") return guard("audit.read", <AuditPage />);
  if (path === "/settings") return guard("settings.manage", <SettingsPage />);
  if (path === "/insights") return can("insights.read") || can("network.read") ? <InsightsPage tab={query.get("tab") ?? undefined} /> : guard("insights.read", null);
  if (path === "/clinics") return guard("settings.manage", <ClinicsPage />);
  if (path === "/clinical-content") return can("cds.review") || can("catalogue.manage") ? <ClinicalContentPage /> : guard("cds.review", null);
  return <Empty title="Page not found"><a href="#/">Go to the dashboard</a></Empty>;
}
