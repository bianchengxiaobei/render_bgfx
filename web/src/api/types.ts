export type JsonPrimitive = string | number | boolean | null;
export type JsonValue = JsonPrimitive | JsonValue[] | { [key: string]: JsonValue };

export type Severity = 'error' | 'warning' | 'info';

export interface Diagnostic {
  severity: Severity;
  code: string;
  message: string;
  nodeId?: number;
  slotId?: number;
  pass?: string;
  variant?: string;
}

export interface Capabilities {
  compile: boolean;
  preview: boolean;
  nodePreview: boolean;
  undo?: boolean;
  redo?: boolean;
}

export interface TargetInfo {
  id: string;
  available: boolean;
  reason?: string;
}

export interface SessionData {
  protocolVersion: number;
  server: string;
  document: {
    path: string;
    kind: 'graph' | 'subgraph';
    dirty: boolean;
    revision: number;
  };
  port: number;
  capabilities: Capabilities;
  targets: TargetInfo[];
}

export interface CatalogSlot {
  id: number;
  name: string;
  displayName: string;
  direction: 'in' | 'out';
  type: string;
  required: boolean;
  hidden: boolean;
  allowConnection: boolean;
  supportsLiteral: boolean;
  requiresLiteral: boolean;
  canPropagateLiteral: boolean;
  allowedConcreteTypes: string[];
  default?: JsonValue;
}

export interface CatalogNode {
  type: string;
  title: string;
  category: string;
  synonyms: string[];
  targets: string[];
  settings: string[];
  hasPreview: boolean;
  blockNode: boolean;
  allowInMainGraph: boolean;
  allowInSubGraph: boolean;
  isTargetSpecific: boolean;
  defaultPrecision: string;
  slots: CatalogSlot[];
}

export interface SlotState {
  mode: 'default' | 'literal' | 'connected';
  value?: JsonValue;
  concreteType?: string;
}

export interface GraphNode {
  id: number;
  type: string;
  typeVersion: number;
  position: [number, number];
  settings: Record<string, JsonValue>;
  slots: Record<string, SlotState>;
  unknownData?: string;
}

export interface GraphEndpoint {
  node: number;
  slot: number;
}

export interface GraphEdge {
  from: GraphEndpoint;
  to: GraphEndpoint;
}

export interface Property {
  name: string;
  refName: string;
  type: string;
  precision: 'inherit' | 'single' | 'half';
  exposed: boolean;
  default: JsonValue;
  category?: string;
  floatMode?: string;
  attributes?: string[];
  sampler?: { filter: string; wrap: string };
  defaultType?: string;
}

export interface KeywordEntry {
  id: number;
  displayName: string;
  referenceName: string;
}

export interface Keyword {
  name: string;
  refName: string;
  type: 'boolean' | 'enum';
  definition: string;
  scope: string;
  default: number;
  category?: string;
  entries?: KeywordEntry[];
}

export interface Category {
  name: string;
  children: string[];
}

export interface GraphGroup {
  id: number;
  title: string;
  position: [number, number];
  size: [number, number];
  color: [number, number, number, number];
  nodes: number[];
}

export interface GraphNote {
  id: number;
  title: string;
  position: [number, number];
  size: [number, number];
  text: string;
  theme: string;
}

export interface GraphTarget {
  id: string;
  subTarget: string;
  settings: Record<string, JsonValue>;
}

export interface Graph {
  schemaVersion: number;
  kind: 'graph' | 'subgraph';
  name: string;
  generatorVersion?: string;
  target: GraphTarget;
  properties: Property[];
  keywords: Keyword[];
  categories: Category[];
  nodes: GraphNode[];
  edges: GraphEdge[];
  groups: GraphGroup[];
  notes: GraphNote[];
  outputs?: { name: string; type: string; slot: number }[];
}

export interface LayoutNode {
  id: number;
  position: [number, number];
  collapsed: boolean;
}

export interface LayoutGroup {
  id: number;
  position: [number, number];
  size: [number, number];
  collapsed: boolean;
}

export interface LayoutNote {
  id: number;
  position: [number, number];
  size: [number, number];
}

export interface GraphLayout {
  schemaVersion: number;
  revision: number;
  view: { pan: [number, number]; zoom: number };
  nodes: LayoutNode[];
  groups: LayoutGroup[];
  notes: LayoutNote[];
}

export interface DocumentData {
  revision?: number;
  path: string;
  kind: 'graph' | 'subgraph';
  dirty: boolean;
  graph: Graph;
  layout: GraphLayout;
  targets: TargetInfo[];
  capabilities: Capabilities;
  diagnostics: Diagnostic[];
}

export interface Envelope<T> {
  ok: boolean;
  revision?: number;
  data: T;
  error?: { code: string; message: string; details?: JsonValue };
}

export interface CommandResultData {
  applied: number;
  dirty: boolean;
  affected: number[];
  diagnostics: Diagnostic[];
  id?: number;
  idMap?: Record<string, number>;
  [key: string]: unknown;
}

export interface CompileJob {
  jobId: string;
  revision: number;
}

export interface CompileResult {
  state: 'queued' | 'running' | 'succeeded' | 'failed' | 'cancelled' | 'stale';
  revision: number;
  diagnostics: Diagnostic[];
  artifacts?: { path: string; hash: string }[];
  stats?: {
    passes?: number;
    variants?: number;
    backends?: number;
    milliseconds?: number;
  };
  error?: string;
}

export type Command = { op: string } & Record<string, unknown>;

export interface NodePreviewPending {
  node: number;
  slot: number;
  state: string;
}

export type PreviewChannel = 'rgb' | 'r' | 'g' | 'b' | 'a';
export type PreviewMesh = 'sphere' | 'plane' | 'cube' | 'quad';
export type PreviewBackground = 'checker' | 'black' | 'gray' | 'sky';

export interface PreviewOptions {
  mesh: PreviewMesh;
  size: number;
  background: PreviewBackground;
  channel: PreviewChannel;
  yaw: number;
  pitch: number;
  distance: number;
}
