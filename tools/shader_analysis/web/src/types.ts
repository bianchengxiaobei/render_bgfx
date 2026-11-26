export type ReportDataset = 'reports' | 'reference';
export type VariantStatus = 'ok' | 'failed' | 'skipped';

export interface ShadercInfo {
  path: string;
  version: string;
}

export interface SourceInfo {
  path: string;
  sha256: string;
  language: string;
}

export interface LogEntry {
  message: string;
  stacktrace: string[];
}

export interface Metrics {
  instructionCount?: number | null;
  opcodeHistogram?: Record<string, number>;
  tempRegisterMax?: number | null;
  tempRegisterPeak?: number | null;
  inputCount?: number | null;
  outputCount?: number | null;
  cbufferSlotCount?: number | null;
  cbufferDwordsRead?: number | null;
  dynamicCBufferReadCount?: number | null;
  textureCount?: number | null;
  samplerCount?: number | null;
  uavCount?: number | null;
  structuredBufferCount?: number | null;
  sampleCount?: number | null;
  loadCount?: number | null;
  storeCount?: number | null;
  branchCount?: number | null;
  loopCount?: number | null;
  ssaValueCount?: number | null;
  maxLiveSsaValues?: number | null;
  imageCount?: number | null;
  uniformBufferCount?: number | null;
  storageBufferCount?: number | null;
  pushConstantCount?: number | null;
  uniformBufferBytes?: number | null;
  pushConstantBytes?: number | null;
  accessChainCount?: number | null;
  sourceLineCount?: number | null;
  debugInfo?: boolean | null;
  warningCount?: number | null;
  errorCount?: number | null;
  microCodeSize?: number | null;
  sgprCount?: number | null;
  sgprUsedCount?: number | null;
  userSgprCount?: number | null;
  ldsSize?: number | null;
  threadGroupWaves?: number | null;
  simdOccupancyCount?: number | null;
  simdOccupancyMax?: number | null;
  cuOccupancyCount?: number | null;
  cuOccupancyMax?: number | null;
}

export interface VariantLogs {
  warnings: LogEntry[];
  errors: LogEntry[];
}

export interface VariantSummary {
  variantId: string;
  name: string;
  defines: string[];
  canonicalDefines: string[];
  keywords: string[];
  status: VariantStatus;
  cacheHit: boolean;
  compileMs?: number | null;
  binaryBytes?: number | null;
  binaryPath?: string | null;
  disasmPath?: string | null;
  detailPath?: string | null;
  metrics?: Metrics | null;
  logs?: VariantLogs;
}

export interface ReportSummary {
  schemaVersion: number;
  toolVersion: string;
  generatedAt: string;
  backend: string;
  profile: string;
  optimization: number;
  debugInfo: boolean;
  shaderc: ShadercInfo;
  source: SourceInfo;
  stage: string;
  variantCount: number;
  variants: VariantSummary[];
}

export interface ReportIndexEntry {
  backend: string;
  source: string;
  stage: string;
  summaryPath: string;
  variantCount: number;
  okCount: number;
  failedCount: number;
  sourceSha256: string;
  generatedAt: string;
}

export interface ReportIndex {
  schemaVersion: number;
  generatedAt: string;
  entries: ReportIndexEntry[];
}

export interface RegisterRef {
  class: string;
  index: number;
  mask: string;
}

export interface ResourceRef {
  kind: string;
  name: string;
  index: number;
}

export interface CbufferRead {
  instruction: number;
  slot: number;
  offset: number;
  dwords: number;
  dynamic: boolean;
}

export interface Pressure {
  temp?: number | null;
  ssa?: number | null;
}

export interface BranchInfo {
  kind: string;
  targetIndex?: number | null;
  indent: number;
}

export interface Instruction {
  index: number;
  opcode: string;
  text: string;
  reads: RegisterRef[];
  writes: RegisterRef[];
  resourceRefs: ResourceRef[];
  cbufferRead?: CbufferRead | null;
  pressure: Pressure;
  branch: BranchInfo;
  sourceLine?: number | null;
}

export interface LivenessInterval {
  class: string;
  index: number;
  start: number;
  end: number;
}

export interface Resource {
  kind: string;
  name: string;
  binding: number;
  size?: number | null;
}

export interface DetailReport {
  schemaVersion: number;
  source: string;
  stage: string;
  backend: string;
  variantId: string;
  instructions: Instruction[];
  liveness: LivenessInterval[];
  resources: Resource[];
  cbufferReads: CbufferRead[];
}

export type DiffStatus = 'same' | 'changed' | 'added' | 'removed' | 'incomparable';

export interface DiffReportSide {
  root: string;
  generatedAt?: string | null;
}

export interface DiffEntry {
  backend: string;
  source: string;
  stage: string;
  variantId: string;
  canonicalDefines: string[];
  metricDeltas: Record<string, number>;
  status: DiffStatus;
}

export interface DiffVariantRef {
  backend: string;
  source: string;
  stage: string;
  variantId: string;
  canonicalDefines: string[];
  status: DiffStatus;
}

export interface ReportDiff {
  schemaVersion: number;
  current: DiffReportSide;
  reference: DiffReportSide;
  entries: DiffEntry[];
  addedVariants: DiffVariantRef[];
  removedVariants: DiffVariantRef[];
  sourceHashChanged: boolean;
  toolchainChanged: boolean;
}