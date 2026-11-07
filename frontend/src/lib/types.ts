// API response shapes (mirror backend DTOs in backend/src/dto).

export interface CurrentUser {
  id: string;
  username: string;
  fullName: string;
  roles: string[];
  permissions: string[];
  doctorId?: string;
  mustChangePassword: boolean;
  sessionIdleMinutes: number;
}

export interface Patient {
  id: string;
  patientNumber: string;
  fullName: string;
  dateOfBirth: string;
  dobIsEstimated?: boolean;
  ageYears: number;
  gender: string;
  mobile: string;
  email?: string;
  addressLine?: string;
  city?: string;
  district?: string;
  state?: string;
  pincode?: string;
  emergencyContactName?: string;
  emergencyContactRelation?: string;
  emergencyContactPhone?: string;
  bloodGroup?: string;
  maritalStatus?: string;
  occupation?: string;
  preferredLanguage?: string;
  isActive: boolean;
  abhaLinked: boolean;
  version: number;
  createdAt: string;
  updatedAt: string;
}

export interface Page<T> {
  items: T[];
  total: number;
  page: number;
  size: number;
}

export interface Doctor {
  id: string;
  userId: string;
  displayName: string;
  qualification?: string;
  specialization?: string;
  registrationNumber?: string;
  registrationCouncil?: string;
  hprId?: string;
  consultationFeePaise: number;
  isActive: boolean;
}

export interface ScheduleBlock {
  id?: string;
  weekday: number;
  startTime: string;
  endTime: string;
  slotMinutes: number;
}

export interface Slot {
  startAt: string;
  endAt: string;
  available: boolean;
  reason?: string;
}

export interface Appointment {
  id: string;
  patientId: string;
  patientNumber: string;
  patientName: string;
  patientMobile: string;
  patientGender: string;
  patientAgeYears: number;
  doctorId: string;
  doctorName: string;
  appointmentType: "SCHEDULED" | "WALK_IN" | "FOLLOW_UP";
  startAt: string;
  endAt: string;
  status: string;
  reason?: string;
  notes?: string;
  followUpOf?: string;
  cancellationReason?: string;
  tokenNumber?: number;
  tokenStatus?: string;
  consultationId?: string;
  billId?: string;
  billStatus?: string;
  version: number;
}

export interface QueueToken {
  id: string;
  appointmentId: string;
  doctorId: string;
  doctorName: string;
  tokenNumber: number;
  displayToken: string;
  status: string;
  patientId: string;
  patientName: string;
  patientNumber: string;
  appointmentType: string;
  appointmentStatus: string;
  scheduledAt: string;
  issuedAt: string;
  calledAt?: string;
  waitingMinutes?: number;
  consultationId?: string;
}

export interface Vitals {
  bpSystolic?: number | null;
  bpDiastolic?: number | null;
  pulse?: number | null;
  temperatureC?: number | null;
  respiratoryRate?: number | null;
  spo2?: number | null;
  heightCm?: number | null;
  weightKg?: number | null;
  bmi?: number;
  recordedAt?: string;
}

export interface Diagnosis {
  id?: string;
  codeSystem: string;
  code?: string;
  display: string;
  diagnosisType?: string;
  notes?: string;
}

export interface RxItem {
  id?: string;
  medicationId?: string;
  medicineName: string;
  strength?: string;
  dosageForm?: string;
  dose: string;
  route: string;
  frequency: string;
  durationDays?: number | null;
  quantity?: number | null;
  instructions?: string;
}

export interface InvestigationOrder {
  id?: string;
  investigationId?: string;
  name: string;
  category: string;
  loincCode?: string;
  priority?: string;
  clinicalNotes?: string;
  status?: string;
}

export interface Consultation {
  id: string;
  appointmentId?: string;
  patientId: string;
  patientName: string;
  patientNumber: string;
  doctorId: string;
  doctorName: string;
  doctorQualification?: string;
  doctorRegistration?: string;
  status: "DRAFT" | "FINALIZED";
  chiefComplaint?: string;
  historyPresentIllness?: string;
  pastMedicalHistory?: string;
  currentMedications?: string;
  examinationNotes?: string;
  treatmentPlan?: string;
  followUpInstructions?: string;
  followUpDate?: string;
  startedAt: string;
  finalizedAt?: string;
  version: number;
  vitals?: Vitals;
  diagnoses: Diagnosis[];
  addenda: { id: string; authorName: string; reason: string; content: string; createdAt: string }[];
  prescription: { id?: string; status?: string; advice?: string; items: RxItem[]; investigations: InvestigationOrder[] };
  canEdit: boolean;
}

export interface TimelineEntry {
  consultationId: string;
  startedAt: string;
  status: string;
  doctorName: string;
  chiefComplaint?: string;
  diagnoses: string[];
  medicines: string[];
  investigations: string[];
  vitals?: Vitals;
  followUpDate?: string;
}

export interface Allergy {
  id: string;
  substance: string;
  category: string;
  reaction?: string;
  severity: string;
  status: string;
  recordedBy?: string;
  createdAt: string;
}

export interface Medication {
  id: string;
  genericName: string;
  brandName?: string;
  strength: string;
  dosageForm: string;
  defaultRoute: string;
  drugSchedule?: string;
  isActive: boolean;
}

export interface InvestigationItem {
  id: string;
  code: string;
  name: string;
  category: string;
  loincCode?: string;
  specimen?: string;
}

export interface Service {
  id: string;
  code: string;
  name: string;
  category: string;
  pricePaise: number;
  taxRateBp: number;
  isActive: boolean;
}

export interface BillItem {
  id: string;
  serviceId?: string;
  description: string;
  quantity: number;
  unitPricePaise: number;
  taxRateBp: number;
  lineSubtotalPaise: number;
  lineDiscountPaise: number;
  lineTaxPaise: number;
  lineTotalPaise: number;
}

export interface Payment {
  id: string;
  paymentType: "PAYMENT" | "REFUND";
  amountPaise: number;
  method: string;
  reference?: string;
  notes?: string;
  receivedBy?: string;
  receivedAt: string;
}

export interface Bill {
  id: string;
  invoiceNumber: string;
  patientId: string;
  patientName: string;
  patientNumber: string;
  patientMobile: string;
  appointmentId?: string;
  consultationId?: string;
  status: string;
  subtotalPaise: number;
  discountPaise: number;
  taxPaise: number;
  totalPaise: number;
  paidPaise: number;
  refundedPaise: number;
  balancePaise: number;
  discountReason?: string;
  notes?: string;
  cancelledReason?: string;
  createdBy?: string;
  createdAt: string;
  items?: BillItem[];
  payments?: Payment[];
}

export interface Settings {
  clinicName: string;
  address?: string;
  city?: string;
  state?: string;
  pincode?: string;
  phone?: string;
  email?: string;
  registrationNumber?: string;
  gstin?: string;
  hfrId?: string;
  timezone: string;
  patientIdPrefix: string;
  invoicePrefix: string;
  defaultSlotMinutes: number;
  sessionIdleMinutes: number;
  duplicateRules: {
    threshold: number;
    nameSimilarityMin: number;
    weightMobile: number;
    weightDateOfBirth: number;
    weightName: number;
    weightGender: number;
  };
  updatedAt?: string;
}

export interface UserRow {
  id: string;
  username: string;
  fullName: string;
  email?: string;
  phone?: string;
  isActive: boolean;
  roles: string[];
  doctorId?: string;
  lastLoginAt?: string;
  lockedUntil?: string;
}

export interface Role {
  id: string;
  code: string;
  name: string;
  description?: string;
  isSystem: boolean;
  userCount: number;
  permissions: string[];
}

export interface Permission {
  code: string;
  module: string;
  description: string;
}

export interface AuditLog {
  id: number;
  occurredAt: string;
  userId?: string;
  username?: string;
  roles?: string;
  action: string;
  entityType?: string;
  entityId?: string;
  patientId?: string;
  outcome: string;
  ipAddress?: string;
  details?: string;
  hash: string;
}

export interface Identifier {
  id: string;
  idType: string;
  valueMasked: string;
  verificationStatus: string;
  verificationMethod?: string;
  verifiedAt?: string;
  isMock: boolean;
  unlinkedAt?: string;
  createdAt: string;
}

export interface AbdmTxn {
  id: string;
  requestId: string;
  txnType: string;
  status: string;
  attemptCount: number;
  maxAttempts: number;
  gatewayTxnId?: string;
  errorCode?: string;
  errorMessage?: string;
  responseSummary?: string;
  isMock: boolean;
  createdAt: string;
  nextRetryAt?: string;
}

export interface Consent {
  id: string;
  patientId: string;
  direction: string;
  purposeCode: string;
  hiTypes: string[];
  dateFrom: string;
  dateTo: string;
  expiresAt: string;
  status: string;
  statusReason?: string;
  consentRequestRef?: string;
  consentArtefactRef?: string;
  requestedBy?: string;
  requestedAt: string;
  isMock: boolean;
}

export interface Dashboard {
  date: string;
  totalPatients: number;
  newPatientsToday: number;
  appointmentsToday: number;
  waitingNow: number;
  inConsultationNow: number;
  consultationsCompletedToday: number;
  pendingPaymentsCount?: number;
  pendingPaymentsPaise?: number;
  revenueTodayPaise?: number;
  revenueLast7DaysPaise?: number[];
  last7Days?: string[];
  appointmentsByStatus: Record<string, number>;
}
