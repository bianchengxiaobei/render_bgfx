import { create } from 'zustand';
import { ApiError, api, connectEvents, getToken, setToken as setStoredToken } from '../api/client';
import type {
  CatalogNode,
  Command,
  CommandResultData,
  CompileResult,
  Diagnostic,
  DocumentData,
  Graph,
  GraphLayout,
  JsonValue,
  SessionData,
} from '../api/types';
import { catalogByType, withFallbackLayout } from '../graph/model';

export type LoadState = 'idle' | 'loading' | 'ready' | 'error';
export type ToastKind = 'info' | 'success' | 'error';

export interface Toast {
  id: number;
  kind: ToastKind;
  text: string;
}

export interface Selection {
  nodes: number[];
  edges: string[];
  property?: string;
  keyword?: string;
}

export interface CompileStatus {
  state: 'idle' | 'queued' | 'running' | 'succeeded' | 'failed';
  message: string;
  jobId?: string;
}

export interface EditorState {
  token: string;
  status: LoadState;
  error: string | null;
  session: SessionData | null;
  document: DocumentData | null;
  graph: Graph | null;
  layout: GraphLayout | null;
  revision: number;
  dirty: boolean;
  diagnostics: Diagnostic[];
  catalog: CatalogNode[];
  catalogMap: Record<string, CatalogNode>;
  selection: Selection;
  inFlight: boolean;
  pending: number;
  toast: Toast | null;
  compile: CompileStatus;
  previewEpoch: number;
  showPreviews: boolean;
  searchOpen: boolean;
  searchPosition: { x: number; y: number };
  focusNodeId: number | null;
  events: EventSource | null;
  initialize: () => Promise<void>;
  connectSSE: () => void;
  disconnectSSE: () => void;
  applyDocument: (data: DocumentData, session?: SessionData | null) => void;
  refreshDocument: () => Promise<DocumentData | null>;
  refreshDiagnostics: () => Promise<void>;
  runCommands: (
    commands: Command[],
    options?: { refresh?: boolean; selectNode?: number },
  ) => Promise<CommandResultData | null>;
  addNode: (
    type: string,
    position: { x: number; y: number },
    settings?: Record<string, JsonValue>,
  ) => Promise<number | null>;
  connect: (from: { node: number; slot: number }, to: { node: number; slot: number }) => Promise<void>;
  disconnectTo: (node: number, slot: number) => Promise<void>;
  removeNodes: (ids: number[]) => Promise<void>;
  removeEdge: (from: { node: number; slot: number }, to: { node: number; slot: number }) => Promise<void>;
  setNodeSetting: (node: number, name: string, value: JsonValue) => Promise<void>;
  setSlotValue: (
    node: number,
    slot: number,
    value: JsonValue,
    concreteType?: string,
  ) => Promise<void>;
  setSlotDefault: (node: number, slot: number) => Promise<void>;
  setSlotConcreteType: (node: number, slot: number, concreteType: string) => Promise<void>;
  addProperty: (name: string, type: string, category?: string) => Promise<void>;
  removeProperty: (refName: string) => Promise<void>;
  renameProperty: (refName: string, newName: string) => Promise<void>;
  setProperty: (refName: string, field: string, value: JsonValue) => Promise<void>;
  addKeyword: (name: string, type: 'boolean' | 'enum') => Promise<void>;
  removeKeyword: (refName: string) => Promise<void>;
  setTargetSetting: (name: string, value: JsonValue) => Promise<void>;
  moveNodesLocal: (nodes: number[], delta: { x: number; y: number }) => void;
  setLocalNodePositions: (positions: Record<number, { x: number; y: number }>) => void;
  commitMoves: (nodes: number[], delta: { x: number; y: number }) => Promise<void>;
  undo: () => Promise<void>;
  redo: () => Promise<void>;
  save: () => Promise<void>;
  saveAs: (path: string) => Promise<void>;
  open: (path: string) => Promise<void>;
  runCompile: () => Promise<void>;
  setSelection: (selection: Selection) => void;
  setToken: (token: string) => void;
  setShowPreviews: (show: boolean) => void;
  openSearch: (position: { x: number; y: number }) => void;
  closeSearch: () => void;
  setFocusNode: (id: number | null) => void;
  notify: (kind: ToastKind, text: string) => void;
  clearToast: () => void;
}

let commandChain: Promise<unknown> = Promise.resolve();
let refreshPromise: Promise<DocumentData | null> | null = null;
let toastCounter = 0;

function enqueue<T>(task: () => Promise<T>): Promise<T> {
  const next = commandChain.then(task, task);
  commandChain = next.then(
    () => undefined,
    () => undefined,
  );
  return next;
}

function errorText(error: unknown): string {
  if (error instanceof ApiError) {
    return error.message || error.code;
  }
  return error instanceof Error ? error.message : String(error);
}

function isStale(error: unknown): boolean {
  return error instanceof ApiError && error.code === 'SG_STALE_REVISION';
}

function sameNodeSet(left: number[], right: number[]): boolean {
  if (left.length !== right.length) {
    return false;
  }
  const set = new Set(left);
  return right.every((id) => set.has(id));
}

export const useEditorStore = create<EditorState>((set, get) => ({
  token: getToken(),
  status: 'idle',
  error: null,
  session: null,
  document: null,
  graph: null,
  layout: null,
  revision: 0,
  dirty: false,
  diagnostics: [],
  catalog: [],
  catalogMap: {},
  selection: { nodes: [], edges: [] },
  inFlight: false,
  pending: 0,
  toast: null,
  compile: { state: 'idle', message: '未编译' },
  previewEpoch: 0,
  showPreviews: true,
  searchOpen: false,
  searchPosition: { x: 0, y: 0 },
  focusNodeId: null,
  events: null,

  async initialize() {
    const token = getToken();
    set({ token, status: 'loading', error: null });
    if (!token) {
      set({ status: 'error', error: '缺少编辑器 token，请从宿主启动日志的 URL 打开页面。' });
      return;
    }
    try {
      const [session, document, catalog] = await Promise.all([
        api.session(),
        api.document(),
        api.catalog('', 500),
      ]);
      const catalogNodes = catalog.data.nodes ?? [];
      set({
        session: session.data,
        catalog: catalogNodes,
        catalogMap: catalogByType(catalogNodes),
        status: 'ready',
        error: null,
      });
      get().applyDocument({ ...document.data, revision: document.revision }, session.data);
      get().connectSSE();
    } catch (error) {
      set({ status: 'error', error: errorText(error) });
    }
  },

  connectSSE() {
    get().disconnectSSE();
    const source = connectEvents({
      onRevision: (revision) => {
        if (revision !== get().revision && !get().inFlight) {
          void get().refreshDocument();
        }
      },
      onCompile: (data) => {
        const current = get().compile;
        if (data.revision !== get().revision || !current.jobId || current.jobId !== data.jobId) {
          return;
        }
        if (data.state === 'succeeded') {
          set({
            compile: { state: 'succeeded', message: '编译成功', jobId: data.jobId },
            previewEpoch: get().previewEpoch + 1,
          });
          void get().refreshDocument();
        } else if (data.state === 'failed') {
          set({ compile: { state: 'failed', message: '编译失败', jobId: data.jobId } });
        } else {
          set({ compile: { state: data.state === 'queued' ? 'queued' : 'running', message: '编译中', jobId: data.jobId } });
        }
      },
      onDiagnostics: () => {
        void get().refreshDiagnostics();
      },
      onFile: (data) => {
        get().notify('info', `文件已更新：${data.path}`);
      },
    });
    set({ events: source });
  },

  disconnectSSE() {
    get().events?.close();
    set({ events: null });
  },

  applyDocument(data, session = get().session) {
    const graph = withFallbackLayout(data.graph);
    set((state) => {
      const nodeIds = new Set(graph.nodes.map((node) => node.id));
      const edgeIds = new Set(graph.edges.map((edge) => `e-${edge.from.node}-${edge.from.slot}-${edge.to.node}-${edge.to.slot}`));
      const nodes = state.selection.nodes.filter((id) => nodeIds.has(id));
      const edges = state.selection.edges.filter((id) => edgeIds.has(id));
      const property = state.selection.property &&
        graph.properties.some((item) => item.refName === state.selection.property)
        ? state.selection.property
        : undefined;
      const keyword = state.selection.keyword &&
        graph.keywords.some((item) => item.refName === state.selection.keyword)
        ? state.selection.keyword
        : undefined;
      const edgeSet = new Set(state.selection.edges);
      const sameEdges = edges.length === state.selection.edges.length &&
        edges.every((id) => edgeSet.has(id));
      const selection = sameNodeSet(nodes, state.selection.nodes) && sameEdges &&
        property === state.selection.property && keyword === state.selection.keyword
        ? state.selection
        : { nodes, edges, property, keyword };
      return {
        session,
        document: data,
        graph,
        layout: data.layout,
        revision: data.revision ?? data.layout?.revision ?? state.revision,
        dirty: data.dirty,
        diagnostics: data.diagnostics ?? [],
        selection,
        status: 'ready',
        error: null,
      };
    });
  },

  async refreshDocument() {
    if (refreshPromise) {
      return refreshPromise;
    }
    refreshPromise = (async () => {
      try {
        const [document, session] = await Promise.all([api.document(), api.session()]);
        get().applyDocument({ ...document.data, revision: document.revision }, session.data);
        return document.data;
      } catch (error) {
        get().notify('error', `刷新文档失败：${errorText(error)}`);
        return null;
      } finally {
        refreshPromise = null;
      }
    })();
    return refreshPromise;
  },

  async refreshDiagnostics() {
    try {
      const response = await api.diagnostics();
      set({ diagnostics: response.data.diagnostics ?? [] });
    } catch (error) {
      get().notify('error', `刷新诊断失败：${errorText(error)}`);
    }
  },

  async runCommands(commands, options = {}) {
    if (commands.length === 0) {
      return null;
    }
    return enqueue(async () => {
      set({ inFlight: true, pending: get().pending + 1 });
      try {
        const baseRevision = get().revision;
        const response = await api.commands(baseRevision, commands);
        set({
          revision: response.revision ?? get().revision,
          diagnostics: response.data.diagnostics ?? get().diagnostics,
          dirty: response.data.dirty ?? true,
        });
        if (options.refresh !== false) {
          set({ compile: { state: 'idle', message: '未编译' } });
          await get().refreshDocument();
        }
        if (options.selectNode !== undefined) {
          set({ selection: { nodes: [options.selectNode], edges: [] } });
        }
        return response.data;
      } catch (error) {
        if (isStale(error)) {
          get().notify('error', '文档已被其他客户端更新，已重新加载。');
          await get().refreshDocument();
        } else {
          get().notify('error', errorText(error));
        }
        return null;
      } finally {
        set({ inFlight: false, pending: Math.max(0, get().pending - 1) });
      }
    });
  },

  async addNode(type, position, settings) {
    const definition = get().catalogMap[type];
    const command: Command = {
      op: 'addNode',
      type,
      position: [position.x, position.y],
    };
    if (settings && Object.keys(settings).length > 0) {
      command.settings = settings;
    }
    if (definition?.blockNode) {
      command.id = undefined;
    }
    const result = await get().runCommands([command], { refresh: true });
    const id = typeof result?.id === 'number' ? result.id : null;
    if (id !== null) {
      set({ selection: { nodes: [id], edges: [] } });
    }
    return id;
  },

  async connect(from, to) {
    await get().runCommands([{ op: 'connect', from, to }], { refresh: true });
  },

  async disconnectTo(node, slot) {
    await get().runCommands([{ op: 'disconnect', to: { node, slot } }], { refresh: true });
  },

  async removeNodes(ids) {
    if (ids.length === 0) {
      return;
    }
    await get().runCommands([{ op: 'removeNodes', ids }], { refresh: true });
  },

  async removeEdge(from, to) {
    await get().runCommands([{ op: 'disconnect', from, to }], { refresh: true });
  },

  async setNodeSetting(node, name, value) {
    await get().runCommands([{ op: 'setNodeSetting', node, name, value }], { refresh: true });
  },

  async setSlotValue(node, slot, value, concreteType) {
    const command: Command = { op: 'setSlotValue', node, slot, mode: 'literal', value };
    if (concreteType) {
      command.concreteType = concreteType;
    }
    await get().runCommands([command], { refresh: true });
  },

  async setSlotDefault(node, slot) {
    await get().runCommands([{ op: 'setSlotValue', node, slot, mode: 'default' }], { refresh: true });
  },

  async setSlotConcreteType(node, slot, concreteType) {
    await get().runCommands([{ op: 'setSlotConcreteType', node, slot, concreteType }], { refresh: true });
  },

  async addProperty(name, type, category) {
    await get().runCommands([{ op: 'addProperty', name, type, category: category ?? 'Default' }], { refresh: true });
  },

  async removeProperty(refName) {
    await get().runCommands([{ op: 'removeProperty', refName }], { refresh: true });
  },

  async renameProperty(refName, newName) {
    await get().runCommands([{ op: 'renameProperty', refName, newName }], { refresh: true });
  },

  async setProperty(refName, field, value) {
    await get().runCommands([{ op: 'setProperty', refName, field, value }], { refresh: true });
  },

  async addKeyword(name, type) {
    await get().runCommands([{ op: 'addKeyword', name, type }], { refresh: true });
  },

  async removeKeyword(refName) {
    await get().runCommands([{ op: 'removeKeyword', refName }], { refresh: true });
  },

  async setTargetSetting(name, value) {
    await get().runCommands([{ op: 'setTargetSetting', name, value }], { refresh: true });
  },

  moveNodesLocal(nodes, delta) {
    if (nodes.length === 0 || (!delta.x && !delta.y)) {
      return;
    }
    set((state) => {
      if (!state.graph) {
        return state;
      }
      const ids = new Set(nodes);
      return {
        graph: {
          ...state.graph,
          nodes: state.graph.nodes.map((node) => ids.has(node.id)
            ? { ...node, position: [node.position[0] + delta.x, node.position[1] + delta.y] }
            : node),
        },
        layout: state.layout
          ? {
              ...state.layout,
              nodes: state.layout.nodes.map((node) => ids.has(node.id)
                ? { ...node, position: [node.position[0] + delta.x, node.position[1] + delta.y] }
                : node),
            }
          : state.layout,
        dirty: true,
      };
    });
  },

  setLocalNodePositions(positions) {
    set((state) => {
      if (!state.graph) {
        return state;
      }
      const changed = state.graph.nodes.some((node) => {
        const next = positions[node.id];
        return next !== undefined && (next.x !== node.position[0] || next.y !== node.position[1]);
      });
      if (!changed) {
        return state;
      }
      return {
        graph: {
          ...state.graph,
          nodes: state.graph.nodes.map((node) => {
            const next = positions[node.id];
            return next ? { ...node, position: [next.x, next.y] } : node;
          }),
        },
        layout: state.layout
          ? {
              ...state.layout,
              nodes: state.layout.nodes.map((node) => {
                const next = positions[node.id];
                return next ? { ...node, position: [next.x, next.y] } : node;
              }),
            }
          : state.layout,
      };
    });
  },

  async commitMoves(nodes, delta) {
    if (nodes.length === 0 || (!delta.x && !delta.y)) {
      return;
    }
    const result = await get().runCommands(
      [{ op: 'moveNodes', nodes, delta: [delta.x, delta.y] }],
      { refresh: false },
    );
    if (result === null) {
      await get().refreshDocument();
    }
  },

  async undo() {
    try {
      await api.undo();
      set({ compile: { state: 'idle', message: '未编译' } });
      await get().refreshDocument();
      get().notify('info', '已撤销');
    } catch (error) {
      get().notify('error', errorText(error));
    }
  },

  async redo() {
    try {
      await api.redo();
      set({ compile: { state: 'idle', message: '未编译' } });
      await get().refreshDocument();
      get().notify('info', '已重做');
    } catch (error) {
      get().notify('error', errorText(error));
    }
  },

  async save() {
    try {
      await api.save();
      set({ dirty: false });
      get().notify('success', '已保存');
      await get().refreshDocument();
    } catch (error) {
      get().notify('error', `保存失败：${errorText(error)}`);
    }
  },

  async saveAs(path) {
    try {
      await api.saveAs(path);
      get().notify('success', `已保存到 ${path}`);
      await get().refreshDocument();
    } catch (error) {
      get().notify('error', `另存为失败：${errorText(error)}`);
    }
  },

  async open(path) {
    try {
      await api.open(path);
      set({ selection: { nodes: [], edges: [] }, compile: { state: 'idle', message: '未编译' } });
      await get().refreshDocument();
      get().notify('success', `已打开 ${path}`);
    } catch (error) {
      get().notify('error', `打开失败：${errorText(error)}`);
    }
  },

  async runCompile() {
    const graph = get().graph;
    if (!graph) {
      return;
    }
    set({ compile: { state: 'queued', message: '提交编译…' } });
    try {
      const response = await api.compile({
        mode: 'preview',
        targets: [graph.target.id],
        variants: 'used',
        backends: [],
        includeHlsl: false,
      });
      const jobId = response.data.jobId;
      set({ compile: { state: 'running', message: '编译中…', jobId } });
      const deadline = Date.now() + 60000;
      while (Date.now() < deadline) {
        await new Promise((resolve) => setTimeout(resolve, 300));
        const job = await api.compileJob(jobId);
        const result: CompileResult = job.data;
        if (result.state === 'succeeded') {
          set((state) => ({
            compile: { state: 'succeeded', message: '编译成功', jobId },
            diagnostics: result.diagnostics ?? state.diagnostics,
            previewEpoch: state.previewEpoch + 1,
          }));
          await get().refreshDocument();
          return;
        }
        if (result.state === 'failed' || result.state === 'cancelled' || result.state === 'stale') {
          set({
            compile: { state: 'failed', message: result.state === 'stale' ? '编译已过期' : '编译失败', jobId },
            diagnostics: result.diagnostics ?? get().diagnostics,
          });
          get().notify('error', result.state === 'stale' ? '文档已修改，编译结果已过期。' : '编译失败，请查看诊断。');
          return;
        }
      }
      set({ compile: { state: 'failed', message: '编译超时' } });
      get().notify('error', '编译超时。');
    } catch (error) {
      set({ compile: { state: 'failed', message: errorText(error) } });
      get().notify('error', `编译失败：${errorText(error)}`);
    }
  },

  setSelection(selection) {
    set({ selection });
  },

  setToken(token) {
    setStoredToken(token);
    set({ token });
  },

  setShowPreviews(show) {
    set({ showPreviews: show });
  },

  openSearch(position) {
    set({ searchOpen: true, searchPosition: position });
  },

  closeSearch() {
    set({ searchOpen: false });
  },

  setFocusNode(id) {
    set({ focusNodeId: id });
  },

  notify(kind, text) {
    const id = ++toastCounter;
    set({ toast: { id, kind, text } });
    setTimeout(() => {
      if (get().toast?.id === id) {
        set({ toast: null });
      }
    }, 4200);
  },

  clearToast() {
    set({ toast: null });
  },
}));
