-- ClinicFlow V007: reference data — permissions, system roles, starter catalogues.
-- Catalogue content is starter configuration for demonstration. Codes must be
-- reviewed by the clinic against the authoritative sources (WHO ICD-10, LOINC,
-- CDSCO / national formulary) before clinical use.

INSERT INTO permissions (code, module, description) VALUES
  ('dashboard.read',       'dashboard',   'View operational dashboard'),
  ('patient.read',         'patients',    'Search and view patient demographics'),
  ('patient.create',       'patients',    'Register new patients'),
  ('patient.update',       'patients',    'Update patient demographics'),
  ('patient.history.read', 'patients',    'View demographic change history'),
  ('appointment.read',     'scheduling',  'View appointments and queue'),
  ('appointment.manage',   'scheduling',  'Book, reschedule, cancel appointments and check patients in'),
  ('queue.manage',         'scheduling',  'Call, skip and progress queue tokens'),
  ('schedule.read',        'scheduling',  'View doctor schedules and availability'),
  ('schedule.manage',      'scheduling',  'Configure doctor working hours and slots'),
  ('doctor.manage',        'scheduling',  'Create and edit doctor profiles'),
  ('clinical.read',        'clinical',    'View consultations, vitals, diagnoses, prescriptions'),
  ('clinical.write',       'clinical',    'Create and edit draft consultations and allergies'),
  ('clinical.finalize',    'clinical',    'Finalize consultations and add addenda'),
  ('prescription.write',   'clinical',    'Create prescriptions and investigation orders (CPOE)'),
  ('catalogue.read',       'catalogue',   'Search medicine, investigation, diagnosis and service catalogues'),
  ('catalogue.manage',     'catalogue',   'Maintain catalogues'),
  ('billing.read',         'billing',     'View bills and payments'),
  ('billing.manage',       'billing',     'Create bills and record payments'),
  ('billing.refund',       'billing',     'Record refunds and cancel bills'),
  ('report.billing',       'billing',     'View billing reports and revenue'),
  ('user.manage',          'admin',       'Manage user accounts'),
  ('role.manage',          'admin',       'Manage roles and permission mapping'),
  ('settings.manage',      'admin',       'Configure clinic settings'),
  ('audit.read',           'admin',       'View and verify audit logs'),
  ('abdm.read',            'abdm',        'View ABHA links, consents and ABDM transactions'),
  ('abdm.link',            'abdm',        'Initiate ABHA linking and verification'),
  ('abdm.consent',         'abdm',        'Create, track and revoke ABDM consent requests'),
  ('fhir.export',          'abdm',        'Export records as FHIR resources / share via ABDM');

INSERT INTO roles (code, name, description, is_system) VALUES
  ('ADMIN',        'Administrator', 'Clinic administration, users, configuration, reports and audit', true),
  ('DOCTOR',       'Doctor',        'Clinical documentation, prescriptions and orders', true),
  ('RECEPTIONIST', 'Receptionist',  'Registration, appointments, queue and billing', true);

INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM roles r JOIN permissions p ON p.code = ANY (CASE r.code
  WHEN 'ADMIN' THEN ARRAY[
    'dashboard.read','patient.read','patient.create','patient.update','patient.history.read',
    'appointment.read','appointment.manage','queue.manage','schedule.read','schedule.manage','doctor.manage',
    'catalogue.read','catalogue.manage','billing.read','billing.manage','billing.refund','report.billing',
    'user.manage','role.manage','settings.manage','audit.read','abdm.read']
  WHEN 'DOCTOR' THEN ARRAY[
    'dashboard.read','patient.read','patient.create','patient.update','patient.history.read',
    'appointment.read','queue.manage','schedule.read',
    'clinical.read','clinical.write','clinical.finalize','prescription.write','catalogue.read',
    'abdm.read','abdm.link','abdm.consent','fhir.export']
  WHEN 'RECEPTIONIST' THEN ARRAY[
    'dashboard.read','patient.read','patient.create','patient.update',
    'appointment.read','appointment.manage','queue.manage','schedule.read','catalogue.read',
    'billing.read','billing.manage','abdm.read','abdm.link','abdm.consent']
  END);

-- Starter medicine catalogue (generic names). drug_schedule left for the clinic to configure.
INSERT INTO medication_catalogue (generic_name, strength, dosage_form, default_route) VALUES
  ('Paracetamol', '500 mg', 'TABLET', 'ORAL'),
  ('Paracetamol', '650 mg', 'TABLET', 'ORAL'),
  ('Paracetamol', '250 mg/5 ml', 'SYRUP', 'ORAL'),
  ('Ibuprofen', '400 mg', 'TABLET', 'ORAL'),
  ('Diclofenac Sodium', '50 mg', 'TABLET', 'ORAL'),
  ('Amoxicillin', '500 mg', 'CAPSULE', 'ORAL'),
  ('Amoxicillin + Clavulanic Acid', '625 mg', 'TABLET', 'ORAL'),
  ('Azithromycin', '500 mg', 'TABLET', 'ORAL'),
  ('Cefixime', '200 mg', 'TABLET', 'ORAL'),
  ('Doxycycline', '100 mg', 'CAPSULE', 'ORAL'),
  ('Nitrofurantoin', '100 mg', 'CAPSULE', 'ORAL'),
  ('Metronidazole', '400 mg', 'TABLET', 'ORAL'),
  ('Cetirizine', '10 mg', 'TABLET', 'ORAL'),
  ('Levocetirizine', '5 mg', 'TABLET', 'ORAL'),
  ('Montelukast', '10 mg', 'TABLET', 'ORAL'),
  ('Salbutamol', '100 mcg/dose', 'INHALER', 'INHALATION'),
  ('Budesonide', '200 mcg/dose', 'INHALER', 'INHALATION'),
  ('Pantoprazole', '40 mg', 'TABLET', 'ORAL'),
  ('Omeprazole', '20 mg', 'CAPSULE', 'ORAL'),
  ('Ondansetron', '4 mg', 'TABLET', 'ORAL'),
  ('Domperidone', '10 mg', 'TABLET', 'ORAL'),
  ('Oral Rehydration Salts', 'WHO formula', 'POWDER', 'ORAL'),
  ('Metformin', '500 mg', 'TABLET', 'ORAL'),
  ('Glimepiride', '1 mg', 'TABLET', 'ORAL'),
  ('Amlodipine', '5 mg', 'TABLET', 'ORAL'),
  ('Telmisartan', '40 mg', 'TABLET', 'ORAL'),
  ('Losartan', '50 mg', 'TABLET', 'ORAL'),
  ('Atenolol', '50 mg', 'TABLET', 'ORAL'),
  ('Atorvastatin', '10 mg', 'TABLET', 'ORAL'),
  ('Rosuvastatin', '10 mg', 'TABLET', 'ORAL'),
  ('Aspirin', '75 mg', 'TABLET', 'ORAL'),
  ('Levothyroxine', '50 mcg', 'TABLET', 'ORAL'),
  ('Vitamin D3 (Cholecalciferol)', '60000 IU', 'CAPSULE', 'ORAL'),
  ('Calcium Carbonate + Vitamin D3', '500 mg + 250 IU', 'TABLET', 'ORAL'),
  ('Ferrous Sulphate + Folic Acid', '100 mg + 0.5 mg', 'TABLET', 'ORAL'),
  ('Methylcobalamin', '1500 mcg', 'TABLET', 'ORAL'),
  ('Prednisolone', '10 mg', 'TABLET', 'ORAL'),
  ('Clotrimazole', '1% w/w', 'CREAM', 'TOPICAL'),
  ('Mupirocin', '2% w/w', 'OINTMENT', 'TOPICAL'),
  ('Ciprofloxacin', '0.3% w/v', 'EYE DROPS', 'OPHTHALMIC');

-- Starter investigation catalogue. LOINC codes provided for common lab tests;
-- radiology LOINC mapping intentionally left blank pending clinic review.
INSERT INTO investigation_catalogue (code, name, category, loinc_code, specimen) VALUES
  ('CBC',     'Complete Blood Count (CBC)',            'LAB', '58410-2', 'Blood'),
  ('HB',      'Haemoglobin',                           'LAB', '718-7',   'Blood'),
  ('ESR',     'Erythrocyte Sedimentation Rate (ESR)',  'LAB', '30341-2', 'Blood'),
  ('CRP',     'C-Reactive Protein (CRP)',              'LAB', '1988-5',  'Serum'),
  ('FBS',     'Fasting Blood Glucose',                 'LAB', '1558-6',  'Serum/Plasma'),
  ('RBS',     'Random Blood Glucose',                  'LAB', '2345-7',  'Serum/Plasma'),
  ('HBA1C',   'Glycated Haemoglobin (HbA1c)',          'LAB', '4548-4',  'Blood'),
  ('LIPID',   'Lipid Profile',                         'LAB', '57698-3', 'Serum/Plasma'),
  ('LFT',     'Liver Function Test (Hepatic panel)',   'LAB', '24325-3', 'Serum/Plasma'),
  ('CREAT',   'Serum Creatinine',                      'LAB', '2160-0',  'Serum/Plasma'),
  ('TSH',     'Thyroid Stimulating Hormone (TSH)',     'LAB', '3016-3',  'Serum/Plasma'),
  ('URINE-RE','Urine Routine & Microscopy',            'LAB', '24356-8', 'Urine'),
  ('VITB12',  'Vitamin B12',                           'LAB', '2132-9',  'Serum/Plasma'),
  ('VITD',    '25-Hydroxy Vitamin D',                  'LAB', '62292-8', 'Serum/Plasma'),
  ('ECG',     'Electrocardiogram (12-lead ECG)',       'OTHER', '11524-6', NULL),
  ('XR-CHEST','X-Ray Chest PA View',                   'RADIOLOGY', NULL, NULL),
  ('USG-ABD', 'Ultrasound Whole Abdomen',              'RADIOLOGY', NULL, NULL);

-- Starter ICD-10 (WHO) diagnosis list for quick coding in OPD
INSERT INTO diagnosis_codes (code_system, code, display) VALUES
  ('ICD-10', 'J06.9', 'Acute upper respiratory infection, unspecified'),
  ('ICD-10', 'J02.9', 'Acute pharyngitis, unspecified'),
  ('ICD-10', 'J45.9', 'Asthma, unspecified'),
  ('ICD-10', 'I10',   'Essential (primary) hypertension'),
  ('ICD-10', 'E11.9', 'Type 2 diabetes mellitus without complications'),
  ('ICD-10', 'E78.5', 'Hyperlipidaemia, unspecified'),
  ('ICD-10', 'E03.9', 'Hypothyroidism, unspecified'),
  ('ICD-10', 'D64.9', 'Anaemia, unspecified'),
  ('ICD-10', 'A09',   'Other gastroenteritis and colitis of infectious and unspecified origin'),
  ('ICD-10', 'K29.7', 'Gastritis, unspecified'),
  ('ICD-10', 'K21.9', 'Gastro-oesophageal reflux disease without oesophagitis'),
  ('ICD-10', 'N39.0', 'Urinary tract infection, site not specified'),
  ('ICD-10', 'M54.5', 'Low back pain'),
  ('ICD-10', 'R50.9', 'Fever, unspecified'),
  ('ICD-10', 'R51',   'Headache'),
  ('ICD-10', 'A90',   'Dengue fever [classical dengue]'),
  ('ICD-10', 'L30.9', 'Dermatitis, unspecified'),
  ('ICD-10', 'H10.9', 'Conjunctivitis, unspecified'),
  ('ICD-10', 'F41.1', 'Generalized anxiety disorder'),
  ('ICD-10', 'Z00.0', 'General medical examination');

INSERT INTO service_catalogue (code, name, category, price_paise, tax_rate_bp) VALUES
  ('CONSULT-NEW',  'Consultation — new patient',  'CONSULTATION', 50000, 0),
  ('CONSULT-FU',   'Consultation — follow-up',    'CONSULTATION', 30000, 0),
  ('PROC-DRESS',   'Wound dressing',              'PROCEDURE',    20000, 0),
  ('PROC-NEB',     'Nebulization',                'PROCEDURE',    25000, 0),
  ('PROC-INJ',     'Injection administration',    'PROCEDURE',    10000, 0),
  ('INV-ECG',      'ECG (in-clinic)',             'INVESTIGATION',30000, 0),
  ('OTH-CERT',     'Medical certificate',         'OTHER',        20000, 1800);
