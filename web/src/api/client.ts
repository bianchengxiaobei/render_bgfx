import type {
  CatalogNode,
  Command,
  CommandResultData,
  CompileJob,
  CompileResult,
  Diagnostic,
  DocumentData,
  Envelope,
  JsonValue,
  SessionData,
  TargetInfo,
} from './types';

const TOKEN_KEY = 'hbg.sg.token';
const DEFAULT_TIMEOUT_MS = 5000;

export class ApiError extends Error {
  status: number;
  code: string;
  revision?: number;
  details?: JsonValue;

  constructor(status: number, code: string, message: string, revision?: number, details?: JsonValue) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
    this.revision = revision;
    this.details = details;
  }
}

function readStoredToken(): string {
  try {
    return window.localStorage.getItem(TOKEN_KEY) ?? '';
  } catch {
    return '';
  }
}

export function getToken(): string {
  if (typeof window === 'undefined') {
    return '';
  }
  const queryToken = new URL(window.location.href).searchParams.get('token') ?? '';
  if (queryToken) {
    setToken(queryToken);
    return queryToken;
  }
  return readStoredToken();
}

export function setToken(token: string): void {
  if (typeof window === 'undefined') {
    return;
  }
  try {
    if (token) {
      window.localStorage.setItem(TOKEN_KEY, token);
    } else {
      window.localStorage.removeItem(TOKEN_KEY);
    }
  } catch {
    // Storage can be disabled; the query token still works for page resources.
  }
}

export function apiUrl(
  path: string,
  params?: Record<string, string | number | boolean | undefined | null>,
): string {
  const base = typeof window === 'undefined' ? 'http://127.0.0.1' : window.location.origin;
  const url = new URL(path, base);
  for (const [key, value] of Object.entries(params ?? {})) {
    if (value !== undefined && value !== null && value !== '') {
      url.searchParams.set(key, String(value));
    }
  }
  if (typeof window === 'undefined') {
    return `${url.pathname}${url.search}`;
  }
  return `${url.pathname}${url.search}`;
}

export function imageUrl(
  path: string,
  params?: Record<string, string | number | boolean | undefined | null>,
): string {
  const token = getToken();
  return apiUrl(path, { ...params, token: token || undefined });
}

function headersFor(body: unknown): HeadersInit {
  const token = getToken();
  const headers: Record<string, string> = {};
  if (token) {
    headers['X-SG-Token'] = token;
  }
  if (body !== undefined) {
    headers['Content-Type'] = 'application/json';
  }
  return headers;
}

function envelopeOrThrow<T>(status: number, payload: unknown): Envelope<T> {
  const body = payload as Partial<Envelope<T>> | null;
  if (!body || typeof body !== 'object' || typeof body.ok !== 'boolean') {
    throw new ApiError(status, 'SG_INVALID_RESPONSE', '服务器返回了无效响应');
  }
  if (!body.ok) {
    throw new ApiError(
      status,
      body.error?.code ?? 'SG_INTERNAL',
      body.error?.message ?? '请求失败',
      body.revision,
      body.error?.details,
    );
  }
  return body as Envelope<T>;
}

async function requestJson<T>(
  path: string,
  init: RequestInit = {},
  retry = true,
): Promise<Envelope<T>> {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), DEFAULT_TIMEOUT_MS);
  const method = (init.method ?? 'GET').toUpperCase();
  try {
    const response = await fetch(apiUrl(path), {
      ...init,
      headers: { ...headersFor(init.body), ...(init.headers ?? {}) },
      signal: controller.signal,
    });
    const text = await response.text();
    let payload: unknown = null;
    try {
      payload = text ? JSON.parse(text) : null;
    } catch {
      throw new ApiError(response.status, 'SG_INVALID_RESPONSE', '服务器返回了非 JSON 响应');
    }
    const envelope = envelopeOrThrow<T>(response.status, payload);
    if (!response.ok) {
      throw new ApiError(response.status, 'SG_HTTP_ERROR', `HTTP ${response.status}`);
    }
    return envelope;
  } catch (error) {
    const isAbort = error instanceof DOMException && error.name === 'AbortError';
    if (isAbort) {
      throw new ApiError(0, 'SG_TIMEOUT', `请求超时（${DEFAULT_TIMEOUT_MS / 1000}s）`);
    }
    if (error instanceof ApiError) {
      if (retry && method === 'GET' && error.status >= 500) {
        return requestJson<T>(path, init, false);
      }
      throw error;
    }
    if (retry && method === 'GET') {
      return requestJson<T>(path, init, false);
    }
    throw new ApiError(0, 'SG_NETWORK', error instanceof Error ? error.message : '网络错误');
  } finally {
    clearTimeout(timeout);
  }
}

function encodeBody(body: unknown): string {
  return JSON.stringify(body);
}

export const api = {
  session(): Promise<Envelope<SessionData>> {
    return requestJson<SessionData>('/api/session');
  },

  document(revision?: number, retry = true): Promise<Envelope<DocumentData>> {
    return requestJson<DocumentData>(
      apiUrl('/api/document', revision ? { rev: revision } : undefined),
      {},
      retry,
    );
  },

  catalog(query = '', limit = 500): Promise<Envelope<{ nodes: CatalogNode[] }>> {
    return requestJson<{ nodes: CatalogNode[] }>(
      apiUrl('/api/catalog/nodes', { q: query, limit }),
    );
  },

  targets(): Promise<Envelope<{ targets: TargetInfo[] }>> {
    return requestJson<{ targets: TargetInfo[] }>('/api/catalog/targets');
  },

  diagnostics(): Promise<Envelope<{ diagnostics: Diagnostic[] }>> {
    return requestJson<{ diagnostics: Diagnostic[] }>('/api/diagnostics');
  },

  commands(baseRevision: number, commands: Command[]): Promise<Envelope<CommandResultData>> {
    return requestJson<CommandResultData>(
      '/api/commands',
      {
        method: 'POST',
        body: encodeBody({ baseRevision, commands }),
      },
      false,
    );
  },

  undo(): Promise<Envelope<{ affected: number[] }>> {
    return requestJson<{ affected: number[] }>('/api/undo', { method: 'POST' }, false);
  },

  redo(): Promise<Envelope<{ affected: number[] }>> {
    return requestJson<{ affected: number[] }>('/api/redo', { method: 'POST' }, false);
  },

  save(): Promise<Envelope<Record<string, never>>> {
    return requestJson<Record<string, never>>('/api/save', { method: 'POST' }, false);
  },

  saveAs(path: string): Promise<Envelope<Record<string, never>>> {
    return requestJson<Record<string, never>>(
      '/api/save-as',
      { method: 'POST', body: encodeBody({ path }) },
      false,
    );
  },

  open(path: string): Promise<Envelope<Record<string, never>>> {
    return requestJson<Record<string, never>>(
      '/api/open',
      { method: 'POST', body: encodeBody({ path }) },
      false,
    );
  },

  compile(payload: {
    mode: 'preview' | 'bake';
    targets: string[];
    variants: string;
    backends: string[];
    includeHlsl: boolean;
  }): Promise<Envelope<CompileJob>> {
    return requestJson<CompileJob>(
      '/api/compile',
      { method: 'POST', body: encodeBody(payload) },
      false,
    );
  },

  compileJob(jobId: string): Promise<Envelope<CompileResult>> {
    return requestJson<CompileResult>(`/api/compile/${encodeURIComponent(jobId)}`);
  },

  async nodePreview(
    nodeId: number,
    revision: number,
    size = 128,
    mesh?: string,
  ): Promise<'pending' | Blob> {
    const response = await fetch(
      apiUrl(`/api/preview/node/${nodeId}.png`, { rev: revision, size, mesh }),
      { headers: headersFor(undefined) },
    );
    if (response.status === 202) {
      return 'pending';
    }
    if (!response.ok) {
      const text = await response.text();
      let body: { error?: { code?: string; message?: string } } = {};
      try {
        body = JSON.parse(text);
      } catch {
        // The image endpoints can fail before JSON is available.
      }
      throw new ApiError(
        response.status,
        body.error?.code ?? 'SG_PREVIEW_FAILED',
        body.error?.message ?? `节点预览失败（HTTP ${response.status}）`,
      );
    }
    return response.blob();
  },
};

export interface EditorEvents {
  onRevision?: (revision: number) => void;
  onCompile?: (data: { jobId: string; state: string; revision: number }) => void;
  onDiagnostics?: (data: { revision: number; count: number }) => void;
  onFile?: (data: { path: string; kind: string }) => void;
}

export function connectEvents(events: EditorEvents): EventSource {
  const token = getToken();
  const source = new EventSource(apiUrl('/api/events', { token: token || undefined }));
  source.addEventListener('revision', (event) => {
    try {
      events.onRevision?.(JSON.parse((event as MessageEvent<string>).data).revision);
    } catch {
      // Ignore malformed notifications; the client refreshes snapshots.
    }
  });
  source.addEventListener('compile', (event) => {
    try {
      events.onCompile?.(JSON.parse((event as MessageEvent<string>).data));
    } catch {
      // Ignore malformed notifications.
    }
  });
  source.addEventListener('diagnostics', (event) => {
    try {
      events.onDiagnostics?.(JSON.parse((event as MessageEvent<string>).data));
    } catch {
      // Ignore malformed notifications.
    }
  });
  source.addEventListener('file', (event) => {
    try {
      events.onFile?.(JSON.parse((event as MessageEvent<string>).data));
    } catch {
      // Ignore malformed notifications.
    }
  });
  return source;
}
