import type {
  DetailReport,
  ReportDataset,
  ReportDiff,
  ReportIndex,
  ReportSummary,
} from './types';

const ROOTS: Record<ReportDataset, string> = {
  reports: '/shader_analysis/reports',
  reference: '/shader_analysis/reference',
};

export function datasetRoot(dataset: ReportDataset): string {
  return ROOTS[dataset];
}

function safeRelativePath(value: string): string {
  const normalized = value.replaceAll('\\', '/').replace(/^\/+/, '');
  if (!normalized || normalized.split('/').some((part) => part === '..')) {
    throw new Error(`invalid report path: ${value}`);
  }
  return normalized;
}

function encodePath(value: string): string {
  return safeRelativePath(value)
    .split('/')
    .map((part) => encodeURIComponent(part))
    .join('/');
}

export function summaryUrl(dataset: ReportDataset, summaryPath: string): string {
  return `${datasetRoot(dataset)}/${encodePath(summaryPath)}`;
}

export function detailUrl(
  dataset: ReportDataset,
  summaryPath: string,
  variantId: string,
  detailPath?: string | null,
): string {
  const root = datasetRoot(dataset);
  const rootRelative = root.replace(/^\/+/, '');
  if (detailPath) {
    const normalized = detailPath.replaceAll('\\', '/');
    if (normalized.startsWith(`${rootRelative}/`)) {
      return `${root}/${encodePath(normalized.slice(rootRelative.length + 1))}`;
    }
    if (normalized.startsWith('/shader_analysis/')) {
      return normalized;
    }
  }
  const base = safeRelativePath(summaryPath).replace(/\.json$/i, '');
  return `${root}/${encodePath(base)}/${encodeURIComponent(variantId)}.json`;
}

export function sourceUrl(sourcePath: string): string {
  return `/${encodePath(sourcePath)}`;
}

export function detailRelativePath(summaryPath: string, variantId: string): string {
  return `${safeRelativePath(summaryPath).replace(/\.json$/i, '')}/${variantId}.json`;
}

export function safeServePath(value: string): string {
  const normalized = value.trim().replaceAll('\\', '/');
  const relative = normalized.replace(/^\/+/, '');
  if (
    !relative ||
    relative
      .split('/')
      .some((part) => part === '..' || part === '.' || part === '')
  ) {
    throw new Error(`invalid serve path: ${value}`);
  }
  return `/${encodePath(relative)}`;
}

export function summaryUrlAt(root: string, summaryPath: string): string {
  return `${safeServePath(root)}/${encodePath(summaryPath)}`;
}

export function detailUrlAt(
  root: string,
  summaryPath: string,
  variantId: string,
): string {
  const base = safeRelativePath(summaryPath).replace(/\.json$/i, '');
  return `${safeServePath(root)}/${encodePath(base)}/${encodeURIComponent(variantId)}.json`;
}

export function summaryPathFor(sourcePath: string, backend: string): string {
  const normalized = sourcePath.replaceAll('\\', '/').replace(/^\/+/, '');
  const relative = normalized.replace(/^shaders\//, '');
  return `${backend}/${relative.replace(/\.sc$/i, '')}.json`;
}

export function loadDiff(path: string, signal?: AbortSignal): Promise<ReportDiff> {
  return fetchJson<ReportDiff>(safeServePath(path), signal);
}

export function loadSummaryAt(
  root: string,
  summaryPath: string,
  signal?: AbortSignal,
): Promise<ReportSummary> {
  return fetchJson<ReportSummary>(summaryUrlAt(root, summaryPath), signal);
}

export async function loadDetailAt(
  root: string,
  summaryPath: string,
  variantId: string,
  signal?: AbortSignal,
): Promise<DetailReport> {
  const regular = detailUrlAt(root, summaryPath, variantId);
  const debug = regular.replace(/\.json$/i, '-debug.json');
  try {
    return await fetchJson<DetailReport>(regular, signal);
  } catch (firstError) {
    if (signal?.aborted) {
      throw firstError;
    }
    try {
      return await fetchJson<DetailReport>(debug, signal);
    } catch (secondError) {
      if (signal?.aborted) {
        throw secondError;
      }
      throw firstError;
    }
  }
}

export async function loadSourceText(
  sourcePath: string,
  signal?: AbortSignal,
): Promise<string> {
  const response = await fetch(sourceUrl(sourcePath), {
    signal,
    headers: { Accept: 'text/plain' },
    cache: 'no-store',
  });
  if (!response.ok) {
    throw new Error(`${response.status} ${response.statusText} for ${sourcePath}`);
  }
  return response.text();
}

export function reportLabel(dataset: ReportDataset): string {
  return dataset === 'reference' ? 'Reference' : 'Reports';
}

export async function fetchJson<T>(url: string, signal?: AbortSignal): Promise<T> {
  const response = await fetch(url, {
    signal,
    headers: { Accept: 'application/json' },
    cache: 'no-store',
  });
  if (!response.ok) {
    throw new Error(`${response.status} ${response.statusText} for ${url}`);
  }
  return (await response.json()) as T;
}

export function loadIndex(
  dataset: ReportDataset,
  signal?: AbortSignal,
): Promise<ReportIndex> {
  return fetchJson<ReportIndex>(`${datasetRoot(dataset)}/index.json`, signal);
}

export function loadSummary(
  dataset: ReportDataset,
  summaryPath: string,
  signal?: AbortSignal,
): Promise<ReportSummary> {
  return fetchJson<ReportSummary>(summaryUrl(dataset, summaryPath), signal);
}

export function loadDetail(
  dataset: ReportDataset,
  summaryPath: string,
  variantId: string,
  signal?: AbortSignal,
  detailPath?: string | null,
): Promise<DetailReport> {
  return fetchJson<DetailReport>(
    detailUrl(dataset, summaryPath, variantId, detailPath),
    signal,
  );
}
