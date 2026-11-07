// Small inline icon set (stroke icons, 24px grid) — no external dependency.
import type { SVGProps } from "react";

const base = (d: React.ReactNode) => (props: SVGProps<SVGSVGElement>) => (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={1.8} strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" {...props}>
    {d}
  </svg>
);

export const IconHome = base(<path d="M3 11.5 12 4l9 7.5M5.5 10v9.5h13V10" />);
export const IconUsers = base(<><circle cx="9" cy="8" r="3.2" /><path d="M3 19.5c.8-3.3 3.2-5 6-5s5.2 1.7 6 5M16 5.2a3 3 0 0 1 0 5.6M18 14.6c1.5.6 2.6 2.2 3 4.9" /></>);
export const IconUserPlus = base(<><circle cx="9" cy="8" r="3.2" /><path d="M3 19.5c.8-3.3 3.2-5 6-5s5.2 1.7 6 5M18 8v6M15 11h6" /></>);
export const IconCalendar = base(<><rect x="3.5" y="5" width="17" height="15" rx="2" /><path d="M3.5 9.5h17M8 3v4M16 3v4" /></>);
export const IconClock = base(<><circle cx="12" cy="12" r="8.5" /><path d="M12 7.5V12l3 2" /></>);
export const IconQueue = base(<><path d="M4 6h10M4 12h10M4 18h10" /><circle cx="19" cy="6" r="1.4" /><circle cx="19" cy="12" r="1.4" /><circle cx="19" cy="18" r="1.4" /></>);
export const IconStethoscope = base(<><path d="M6 3v6a4 4 0 0 0 8 0V3" /><path d="M10 13v2.5a4.5 4.5 0 0 0 9 0V13" /><circle cx="19" cy="11" r="2" /></>);
export const IconReceipt = base(<><path d="M6 3h12v18l-3-2-3 2-3-2-3 2z" /><path d="M9 8h6M9 12h6M9 16h3" /></>);
export const IconShield = base(<><path d="M12 3 4.5 6v5.5c0 4.5 3.2 8 7.5 9.5 4.3-1.5 7.5-5 7.5-9.5V6z" /><path d="m9 12 2 2 4-4" /></>);
export const IconSettings = base(<><circle cx="12" cy="12" r="3" /><path d="M19.4 15a1.6 1.6 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.6 1.6 0 0 0-1.8-.3 1.6 1.6 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.6 1.6 0 0 0-1-1.5 1.6 1.6 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.6 1.6 0 0 0 .3-1.8 1.6 1.6 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.6 1.6 0 0 0 1.5-1 1.6 1.6 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.6 1.6 0 0 0 1.8.3H9a1.6 1.6 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.6 1.6 0 0 0 1 1.5 1.6 1.6 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.6 1.6 0 0 0-.3 1.8V9a1.6 1.6 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.6 1.6 0 0 0-1.5 1z" /></>);
export const IconLog = base(<><path d="M7 3h8l4 4v14H7z" /><path d="M15 3v4h4M10 12h6M10 16h6" /></>);
export const IconSearch = base(<><circle cx="11" cy="11" r="6.5" /><path d="m20 20-4.2-4.2" /></>);
export const IconPlus = base(<path d="M12 5v14M5 12h14" />);
export const IconX = base(<path d="M6 6l12 12M18 6 6 18" />);
export const IconAlert = base(<><path d="M12 3.5 2.5 20h19z" /><path d="M12 10v4.5M12 17.5v.01" /></>);
export const IconInfo = base(<><circle cx="12" cy="12" r="8.5" /><path d="M12 11v5M12 8v.01" /></>);
export const IconLock = base(<><rect x="5" y="10.5" width="14" height="10" rx="2" /><path d="M8 10.5V7.5a4 4 0 0 1 8 0v3" /></>);
export const IconPrinter = base(<><path d="M7 9V3.5h10V9M7 17H5a1.5 1.5 0 0 1-1.5-1.5v-5A1.5 1.5 0 0 1 5 9h14a1.5 1.5 0 0 1 1.5 1.5v5A1.5 1.5 0 0 1 19 17h-2" /><path d="M7 14h10v6.5H7z" /></>);
export const IconLink = base(<><path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1" /><path d="M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1" /></>);
export const IconMenu = base(<path d="M4 6h16M4 12h16M4 18h16" />);
export const IconLogout = base(<><path d="M15 4h3.5A1.5 1.5 0 0 1 20 5.5v13a1.5 1.5 0 0 1-1.5 1.5H15" /><path d="M10 16l4-4-4-4M14 12H4" /></>);
export const IconTrash = base(<><path d="M4.5 7h15M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3" /></>);
export const IconCheck = base(<path d="m5 12.5 4.5 4.5L19 7.5" />);
export const IconDoc = base(<><path d="M7 3h7l5 5v13H7z" /><path d="M14 3v5h5" /></>);
export const IconLogo = () => (
  <svg viewBox="0 0 32 32" width="18" height="18" aria-hidden="true">
    <path d="M13 6h6v7h7v6h-7v7h-6v-7H6v-6h7z" fill="#F5F7F6" />
  </svg>
);
