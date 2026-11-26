import type { VariantStatus } from '../types';

export function formatMetric(value: number | null | undefined): string {
  return value == null ? 'n/a' : value.toLocaleString('en-US');
}

export function formatBytes(value: number | null | undefined): string {
  if (value == null) {
    return 'n/a';
  }
  if (value < 1024) {
    return `${value} B`;
  }
  if (value < 1024 * 1024) {
    return `${(value / 1024).toFixed(1)} KB`;
  }
  if (value < 1024 * 1024 * 1024) {
    return `${(value / (1024 * 1024)).toFixed(1)} MB`;
  }
  return `${(value / (1024 * 1024 * 1024)).toFixed(2)} GB`;
}

export function formatDuration(value: number | null | undefined): string {
  if (value == null) {
    return 'n/a';
  }
  return value < 1000 ? `${value} ms` : `${(value / 1000).toFixed(2)} s`;
}

export function formatDate(value: string | null | undefined): string {
  if (!value) {
    return 'n/a';
  }
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
}

export function stageLabel(stage: string): string {
  switch (stage) {
    case 'vertex':
      return 'Vertex';
    case 'fragment':
      return 'Fragment';
    case 'compute':
      return 'Compute';
    default:
      return stage || 'Unknown';
  }
}

export function backendLabel(backend: string): string {
  switch (backend) {
    case 'dxbc':
      return 'DXBC · D3D11';
    case 'spirv':
      return 'SPIR-V · Vulkan';
    default:
      return backend || 'Unknown';
  }
}

export function statusClass(status: VariantStatus): string {
  return `status status-${status}`;
}

export function shadercVersion(version: string | null | undefined): string {
  if (!version) {
    return 'n/a';
  }
  return version.match(/version\s+([0-9][^\s,]*)/i)?.[1] ?? version;
}

export function fileName(path: string): string {
  const parts = path.replaceAll('\\', '/').split('/');
  return parts[parts.length - 1] || path;
}

export function shortHash(value: string, length = 12): string {
  return value ? value.slice(0, length) : '';
}

export async function copyText(value: string): Promise<void> {
  await navigator.clipboard.writeText(value);
}

export function downloadText(name: string, text: string, mime = 'text/plain'): void {
  const blob = new Blob([text], { type: `${mime};charset=utf-8` });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = name;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 0);
}

export function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
