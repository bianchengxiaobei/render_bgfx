import type {
  CatalogNode,
  CatalogSlot,
  Graph,
  GraphEdge,
  GraphNode,
  JsonValue,
  SlotState,
} from '../api/types';

export const TYPE_COLORS: Record<string, string> = {
  float: '#9ccc65',
  integer: '#9ccc65',
  vector2: '#68b6ef',
  vector3: '#68b6ef',
  vector4: '#68b6ef',
  color: '#f2c14e',
  boolean: '#ef7d7d',
  dynamic: '#b8b8b8',
  dynamicvector: '#b8b8b8',
  dynamicmatrix: '#b8b8b8',
  matrix2: '#c792ea',
  matrix3: '#c792ea',
  matrix4: '#c792ea',
  texture2d: '#e8934a',
  texture2darray: '#e8934a',
  texture3d: '#e8934a',
  cubemap: '#e8934a',
  gradient: '#e58ac7',
  string: '#c6c6c6',
};

export const TYPE_LABELS: Record<string, string> = {
  float: 'Float',
  integer: 'Int',
  vector2: 'Vector2',
  vector3: 'Vector3',
  vector4: 'Vector4',
  color: 'Color',
  boolean: 'Bool',
  matrix2: 'Matrix2',
  matrix3: 'Matrix3',
  matrix4: 'Matrix4',
  texture2d: 'Texture2D',
  texture2darray: 'Texture2DArray',
  texture3d: 'Texture3D',
  cubemap: 'Cubemap',
  gradient: 'Gradient',
  string: 'String',
  dynamicvector: 'Dynamic Vector',
  dynamicmatrix: 'Dynamic Matrix',
  dynamic: 'Dynamic',
};

export function typeColor(type: string): string {
  return TYPE_COLORS[type.toLowerCase()] ?? TYPE_COLORS.dynamic;
}

export function typeLabel(type: string): string {
  return TYPE_LABELS[type.toLowerCase()] ?? type;
}

export function isDynamic(type: string): boolean {
  const normalized = type.toLowerCase();
  return normalized === 'dynamic' || normalized === 'dynamicvector' || normalized === 'dynamicmatrix';
}

export function isVectorType(type: string): boolean {
  const normalized = type.toLowerCase();
  return normalized === 'vector2' || normalized === 'vector3' || normalized === 'vector4' ||
    normalized === 'color';
}

export function slotConcreteType(slot: CatalogSlot, state?: SlotState): string {
  if (state?.concreteType) {
    return state.concreteType.toLowerCase();
  }
  const normalized = slot.type.toLowerCase();
  if (normalized === 'dynamicvector') {
    return 'float';
  }
  if (normalized === 'dynamicmatrix') {
    return 'matrix2';
  }
  if (normalized === 'dynamic') {
    return 'vector4';
  }
  return normalized;
}

export type GradientValue = {
  keys: Array<{ t: number; color: number[] }>;
};

export function isGradientValue(value: JsonValue | undefined): value is GradientValue {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    return false;
  }
  const keys = (value as { keys?: JsonValue }).keys;
  return Array.isArray(keys) && keys.every((key) => {
    if (!key || typeof key !== 'object' || Array.isArray(key)) {
      return false;
    }
    const item = key as { t?: JsonValue; color?: JsonValue };
    return typeof item.t === 'number' &&
      Array.isArray(item.color) &&
      item.color.length === 4 &&
      item.color.every((channel) => typeof channel === 'number');
  });
}

export function defaultGradientValue(): GradientValue {
  return {
    keys: [
      { t: 0, color: [1, 1, 1, 1] },
      { t: 1, color: [0, 0, 0, 1] },
    ],
  };
}

export function gradientCss(value: JsonValue | undefined): string {
  const gradient = isGradientValue(value) ? value : defaultGradientValue();
  const stops = [...gradient.keys]
    .sort((left, right) => left.t - right.t)
    .map((key) => {
      const [red = 1, green = 1, blue = 1, alpha = 1] = key.color;
      const byte = (channel: number): number =>
        Math.max(0, Math.min(255, Math.round(channel * 255)));
      return `rgba(${byte(red)}, ${byte(green)}, ${byte(blue)}, ${Math.max(0, Math.min(1, alpha))}) ${Math.max(0, Math.min(1, key.t)) * 100}%`;
    });
  return `linear-gradient(90deg, ${stops.join(', ')})`;
}

export function defaultValueForType(type: string): JsonValue {
  switch (type.toLowerCase()) {
    case 'boolean':
      return false;
    case 'integer':
      return 0;
    case 'float':
      return 0;
    case 'vector2':
      return [0, 0];
    case 'vector3':
      return [0, 0, 0];
    case 'vector4':
      return [0, 0, 0, 0];
    case 'color':
      return [1, 1, 1, 1];
    case 'gradient':
      return defaultGradientValue();
    case 'texture2d':
    case 'texture2darray':
    case 'texture3d':
    case 'cubemap':
      return '';
    default:
      return '';
  }
}

export function propertyDefaultForType(type: string): JsonValue | undefined {
  switch (type.toLowerCase()) {
    case 'texture2d':
    case 'texture2darray':
    case 'texture3d':
    case 'cubemap':
      return undefined;
    default:
      return defaultValueForType(type);
  }
}

export function effectiveSlotValue(slot: CatalogSlot, state?: SlotState): JsonValue {
  if (state?.value !== undefined) {
    return state.value;
  }
  if (slot.default !== undefined) {
    return slot.default;
  }
  return defaultValueForType(slotConcreteType(slot, state));
}

export function isSlotConnected(node: GraphNode | undefined, slotId: number): boolean {
  return node?.slots?.[String(slotId)]?.mode === 'connected';
}

export function incomingEdge(graph: Graph | null, nodeId: number, slotId: number): GraphEdge | undefined {
  return graph?.edges.find((edge) => edge.to.node === nodeId && edge.to.slot === slotId);
}

export function catalogByType(catalog: CatalogNode[]): Record<string, CatalogNode> {
  const map: Record<string, CatalogNode> = {};
  for (const node of catalog) {
    map[node.type] = node;
  }
  return map;
}

export function nodeDefinition(
  catalog: Record<string, CatalogNode>,
  node: GraphNode,
): CatalogNode | undefined {
  return catalog[node.type];
}

export function nodeTitle(catalog: Record<string, CatalogNode>, node: GraphNode): string {
  return catalog[node.type]?.title || node.type;
}

export function slotSpec(
  catalog: Record<string, CatalogNode>,
  node: GraphNode,
  slotId: number,
): CatalogSlot | undefined {
  return catalog[node.type]?.slots.find((slot) => slot.id === slotId);
}

export function visibleSlots(definition: CatalogNode | undefined, direction: 'in' | 'out'): CatalogSlot[] {
  return (definition?.slots ?? []).filter((slot) => slot.direction === direction && !slot.hidden);
}

export function family(type: string): 'scalar' | 'vector' | 'matrix' | 'texture' | 'bool' | 'other' {
  const normalized = type.toLowerCase();
  if (normalized === 'float' || normalized === 'integer') {
    return 'scalar';
  }
  if (normalized === 'vector2' || normalized === 'vector3' || normalized === 'vector4' ||
      normalized === 'color') {
    return 'vector';
  }
  if (normalized.startsWith('matrix')) {
    return 'matrix';
  }
  if (normalized.startsWith('texture') || normalized === 'cubemap') {
    return 'texture';
  }
  if (normalized === 'boolean') {
    return 'bool';
  }
  return 'other';
}

export function connectionCompatible(
  source: CatalogSlot,
  target: CatalogSlot,
  sourceState?: SlotState,
  targetState?: SlotState,
): boolean {
  if (source.direction !== 'out' || target.direction !== 'in') {
    return false;
  }
  if (!source.allowConnection || !target.allowConnection) {
    return false;
  }
  if (isDynamic(source.type) || isDynamic(target.type)) {
    return true;
  }
  const sourceType = slotConcreteType(source, sourceState);
  const targetType = slotConcreteType(target, targetState);
  if (target.allowedConcreteTypes.length > 0 &&
      !target.allowedConcreteTypes.map((item) => item.toLowerCase()).includes(sourceType)) {
    return false;
  }
  if (sourceType === targetType) {
    return true;
  }
  if (family(sourceType) === 'vector' && family(targetType) === 'vector') {
    return true;
  }
  if (family(sourceType) === 'scalar' && family(targetType) === 'scalar') {
    return true;
  }
  return false;
}

export function edgeId(edge: GraphEdge): string {
  return `e-${edge.from.node}-${edge.from.slot}-${edge.to.node}-${edge.to.slot}`;
}

export function findNode(graph: Graph | null, id: number): GraphNode | undefined {
  return graph?.nodes.find((node) => node.id === id);
}

export function formatValue(value: JsonValue | undefined): string {
  if (value === undefined) {
    return '';
  }
  if (Array.isArray(value)) {
    return value.map((item) => typeof item === 'number' ? trimFloat(item) : String(item)).join(', ');
  }
  if (value && typeof value === 'object') {
    return JSON.stringify(value);
  }
  return String(value);
}

export function trimFloat(value: number): string {
  if (!Number.isFinite(value)) {
    return '0';
  }
  return Number(value.toFixed(4)).toString();
}

export function colorToHex(value: JsonValue | undefined): string {
  if (!Array.isArray(value) || value.length < 3) {
    return '#ffffff';
  }
  const channel = (index: number): string => {
    const raw = typeof value[index] === 'number' ? Number(value[index]) : 1;
    const byte = Math.max(0, Math.min(255, Math.round(raw * 255)));
    return byte.toString(16).padStart(2, '0');
  };
  return `#${channel(0)}${channel(1)}${channel(2)}`;
}

export function parseColorHex(hex: string, alpha = 1): JsonValue {
  const normalized = hex.replace('#', '');
  if (!/^[0-9a-fA-F]{6}$/.test(normalized)) {
    return [1, 1, 1, alpha];
  }
  return [0, 2, 4].map((offset) => parseInt(normalized.slice(offset, offset + 2), 16) / 255)
    .concat(alpha) as JsonValue;
}

export function parseEditedValue(text: string, type: string): JsonValue | null {
  const normalized = type.toLowerCase();
  if (normalized === 'boolean') {
    if (text === 'true' || text === '1') {
      return true;
    }
    if (text === 'false' || text === '0') {
      return false;
    }
    return null;
  }
  if (normalized === 'integer') {
    const value = Number(text);
    return Number.isInteger(value) ? value : null;
  }
  if (normalized === 'float') {
    const value = Number(text);
    return Number.isFinite(value) ? value : null;
  }
  if (normalized === 'gradient') {
    try {
      const parsed: unknown = JSON.parse(text);
      return isGradientValue(parsed as JsonValue) ? parsed as JsonValue : null;
    } catch {
      return null;
    }
  }
  if (isVectorType(normalized)) {
    const trimmed = text.trim();
    if (normalized === 'color' && trimmed.startsWith('#')) {
      return parseColorHex(trimmed);
    }
    const parts = trimmed.replace(/[\[\]()]/g, '').split(/[\s,]+/).filter(Boolean);
    if (parts.length < 2 || parts.length > 4) {
      return null;
    }
    const numbers = parts.map(Number);
    if (numbers.some((number) => !Number.isFinite(number))) {
      return null;
    }
    return numbers;
  }
  return text;
}

export function commandValueForSlot(
  slot: CatalogSlot,
  state: SlotState | undefined,
  value: JsonValue,
): { concreteType?: string } {
  const result: { concreteType?: string } = {};
  if (isDynamic(slot.type) && state?.concreteType) {
    result.concreteType = state.concreteType;
  }
  return result;
}


export function withFallbackLayout(graph: Graph): Graph {
  if (graph.nodes.length < 2) {
    return graph;
  }
  const first = graph.nodes[0].position;
  if (!graph.nodes.every((node) => node.position[0] === first[0] && node.position[1] === first[1])) {
    return graph;
  }

  const depth = new Map<number, number>();
  const visiting = new Set<number>();
  const depthOf = (id: number): number => {
    const cached = depth.get(id);
    if (cached !== undefined) {
      return cached;
    }
    if (visiting.has(id)) {
      return 0;
    }
    visiting.add(id);
    let result = 0;
    for (const edge of graph.edges) {
      if (edge.to.node === id) {
        result = Math.max(result, depthOf(edge.from.node) + 1);
      }
    }
    visiting.delete(id);
    depth.set(id, result);
    return result;
  };
  for (const node of graph.nodes) {
    depthOf(node.id);
  }

  const rows = new Map<number, number>();
  return {
    ...graph,
    nodes: graph.nodes.map((node) => {
      const column = depth.get(node.id) ?? 0;
      const row = rows.get(column) ?? 0;
      rows.set(column, row + 1);
      return {
        ...node,
        position: [80 + column * 290, 80 + row * 150] as [number, number],
      };
    }),
  };
}
