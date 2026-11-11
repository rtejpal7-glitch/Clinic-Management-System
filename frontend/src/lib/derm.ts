// Dermatology module: API shapes and helpers. Codes mirror backend/src/derm/DermVocabulary.hpp;
// the labels are always fetched from /derm/vocabulary so the two cannot drift.
import { api } from "./api";

export interface VocabItem { code: string; label: string }
export interface DermVocabulary {
  problemTypes: VocabItem[]; symptoms: VocabItem[]; bodySites: VocabItem[]; lesionTypes: VocabItem[]; colours: VocabItem[];
  configurations: VocabItem[]; distributions: VocabItem[]; factors: VocabItem[]; severities: VocabItem[];
  durationUnits: string[]; pregnancyStatuses: string[]; treatmentResponses: string[]; outcomes: string[];
}

let vocabCache: Promise<DermVocabulary> | null = null;
export function dermVocabulary(): Promise<DermVocabulary> {
  if (!vocabCache) vocabCache = api.get<DermVocabulary>("/derm/vocabulary").catch((e) => { vocabCache = null; throw e; });
  return vocabCache;
}

export interface DermAssessment {
  exists?: boolean;
  canEdit?: boolean;
  problemType?: string | null;
  durationValue?: number | null;
  durationUnit?: string | null;
  severity?: string | null;
  symptoms: string[];
  spreading?: boolean | null;
  recurring?: boolean | null;
  previousEpisodes?: number | null;
  aggravatingFactors: string[];
  relievingFactors: string[];
  bodySites: string[];
  lesionTypes: string[];
  colours: string[];
  configuration?: string | null;
  distribution?: string | null;
  extentBsaPercent?: number | null;
  findingsNotes?: string | null;
  otherConditions?: string | null;
  previousDermTreatments?: string | null;
  previousTreatmentResponse?: string | null;
  familyHistory?: string | null;
  pregnancyStatus?: string | null;
  updatedAt?: string;
}
export const emptyAssessment = (): DermAssessment => ({
  symptoms: [], aggravatingFactors: [], relievingFactors: [], bodySites: [], lesionTypes: [], colours: [],
});

export interface SafetyAlert {
  level: "CONTRAINDICATED" | "WARNING" | "INFO";
  code: string;
  message: string;
  items: number[];
  reviewedContent: boolean;
  requiresOverride: boolean;
  reference?: string;
}

export interface CatalogueProduct {
  medicationId: string; genericName: string; brandName?: string | null; strength: string; dosageForm: string; route: string;
  reviewStatus: string; highRisk?: boolean; commonDosage?: string | null; defaultDose?: string | null;
  defaultFrequency?: string | null; defaultDurationDays?: number | null;
}
export interface Suggestion { generic: string; dose?: string | null; frequency?: string | null; durationDays?: number | null; route?: string }

export interface PanelA {
  status: "OK" | "INSUFFICIENT_DATA" | "NO_APPROVED_CONTENT" | "NO_MATCH";
  message?: string;
  missing: string[];
  redFlags: { code: string; title: string; guidance: string; reference?: string }[];
  conditions: {
    code: string; name: string; icd10: string; summary: string; score: number; strength: string;
    matched: { finding: string; weight: number }[]; notRecorded: { finding: string; weight: number }[];
    observations: string[]; references: { citation: string }[];
  }[];
  questions: { text: string; condition: string }[];
  treatments: { conditionCode: string; condition: string; text: string; line: string; reference: string;
    medicines: { generic: string; products: CatalogueProduct[]; patientAlerts: SafetyAlert[] }[] }[];
  referrals: { condition: string; text: string }[];
  recordedFindings: string[];
  itemsAwaitingReview: number;
  suggestions: Suggestion[];
  disclaimer: string;
  snapshotId: string;
  hasAssessment?: boolean;
}

export interface PatternMed {
  generic: string; name: string; cases: number; percent: number; doctors?: number;
  usualRegimen: { route: string; frequency: string; durationDays?: number; cases: number };
  outcomes: { recorded: number; shown: boolean; note?: string } & Record<string, { count: number; percent: number } | number | boolean | string | undefined>;
  products: CatalogueProduct[];
  patientAlerts: SafetyAlert[];
}
export interface PatternPanel {
  source: "MY_PATTERNS" | "NETWORK";
  status: "OK" | "INSUFFICIENT_DATA" | "NOT_PARTICIPATING";
  message?: string;
  caseCount?: number | null;
  doctorCount?: number;
  clinicCount?: number;
  minimumCases?: number;
  casesWithOutcome?: number;
  period?: { from: string; to: string };
  diagnosis?: { code: string; display: string };
  comparison?: { tier: string; description: string; severity?: string; ageBand?: string };
  thresholds?: Record<string, number>;
  medications: PatternMed[];
  otherMedicinesGrouped?: { note: string };
  suggestions: Suggestion[];
  disclaimer: string;
  snapshotId: string;
}

export interface Photo {
  id: string; consultationId: string; bodySite?: string | null; caption?: string | null; contentType: string; sizeBytes: number;
  consentScope: string; status: "ACTIVE" | "ENTERED_IN_ERROR"; statusReason?: string | null; capturedBy: string; capturedAt: string;
}

export interface DecisionRecord {
  decisions: { source: string; generic?: string; medicineName?: string; decision: string; suggested?: Suggestion | null;
    finalItem?: { name: string; dose: string; frequency: string; route: string; durationDays?: number | null } | null; decidedBy: string; decidedAt: string }[];
  overrides: { reason: string; alerts: SafetyAlert[]; by: string; at: string }[];
  outcomes: { outcome: string; adverseEffects?: string | null; notes?: string | null; recordedBy: string; recordedAt: string }[];
  panelsViewed: { source: string; status: string; views: number; lastViewedAt: string }[];
}

export const SOURCE_LABEL: Record<string, string> = {
  CLINICAL: "A · Clinical knowledge",
  MY_PATTERNS: "B · My prescribing patterns",
  NETWORK: "C · ClinicFlow network",
  DOCTOR: "Added by doctor",
  MANUAL: "Added by doctor",
};

export const labelFor = (items: VocabItem[] | undefined, code?: string | null) =>
  (code && items?.find((i) => i.code === code)?.label) || code || "";

/** Resize an image in the browser before upload (keeps uploads small; server still validates). */
export async function resizeImage(file: File, maxSide = 1600, quality = 0.85): Promise<string> {
  const url = URL.createObjectURL(file);
  try {
    const img = await new Promise<HTMLImageElement>((resolve, reject) => {
      const i = new Image();
      i.onload = () => resolve(i);
      i.onerror = () => reject(new Error("This file could not be read as an image"));
      i.src = url;
    });
    const scale = Math.min(1, maxSide / Math.max(img.width, img.height));
    const canvas = document.createElement("canvas");
    canvas.width = Math.round(img.width * scale);
    canvas.height = Math.round(img.height * scale);
    canvas.getContext("2d")!.drawImage(img, 0, 0, canvas.width, canvas.height);
    return canvas.toDataURL("image/jpeg", quality);
  } finally {
    URL.revokeObjectURL(url);
  }
}
