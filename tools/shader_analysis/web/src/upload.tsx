import { createContext, useCallback, useContext, useMemo, useState } from 'react';
import type { ReactNode } from 'react';
import type { DetailReport, ReportSummary } from './types';

const MAX_UPLOAD_BYTES = 64 * 1024 * 1024;

interface UploadContextValue {
  detail: DetailReport | null;
  summary: ReportSummary | null;
  fileName: string;
  sourceText: string | null;
  sourceName: string;
  error: string | null;
  loading: boolean;
  loadFile: (file: File) => Promise<boolean>;
  loadSource: (file: File) => Promise<boolean>;
  clear: () => void;
}

const UploadContext = createContext<UploadContextValue | null>(null);

function isReportSummary(value: unknown): value is ReportSummary {
  if (!value || typeof value !== 'object') {
    return false;
  }
  const record = value as Record<string, unknown>;
  const source = record.source as Record<string, unknown> | null | undefined;
  return (
    typeof record.schemaVersion === 'number' &&
    typeof record.backend === 'string' &&
    typeof record.stage === 'string' &&
    typeof record.profile === 'string' &&
    typeof source?.path === 'string' &&
    Array.isArray(record.variants)
  );
}

function isDetailReport(value: unknown): value is DetailReport {
  if (!value || typeof value !== 'object') {
    return false;
  }
  const record = value as Record<string, unknown>;
  return (
    typeof record.schemaVersion === 'number' &&
    typeof record.source === 'string' &&
    typeof record.stage === 'string' &&
    typeof record.backend === 'string' &&
    typeof record.variantId === 'string' &&
    Array.isArray(record.instructions) &&
    Array.isArray(record.liveness) &&
    Array.isArray(record.resources) &&
    Array.isArray(record.cbufferReads)
  );
}

export function UploadProvider({ children }: { children: ReactNode }) {
  const [detail, setDetail] = useState<DetailReport | null>(null);
  const [summary, setSummary] = useState<ReportSummary | null>(null);
  const [fileName, setFileName] = useState('');
  const [sourceText, setSourceText] = useState<string | null>(null);
  const [sourceName, setSourceName] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  const loadFile = useCallback(async (file: File): Promise<boolean> => {
    setError(null);
    setLoading(true);
    try {
      if (file.size > MAX_UPLOAD_BYTES) {
        throw new Error('File is larger than 64 MB.');
      }
      let parsed: unknown;
      try {
        parsed = JSON.parse(await file.text());
      } catch {
        throw new Error('File is not valid JSON.');
      }
      if (isDetailReport(parsed)) {
        setDetail(parsed);
        setSummary(null);
        setFileName(file.name);
        setSourceText(null);
        setSourceName('');
        return true;
      }
      if (isReportSummary(parsed)) {
        setSummary(parsed);
        setDetail(null);
        setFileName(file.name);
        setSourceText(null);
        setSourceName('');
        return true;
      }
      throw new Error(
        'Expected a variant detail JSON (instructions, liveness, resources, cbufferReads) or a report summary JSON (source, variants).',
      );
    } catch (loadError) {
      setError(
        loadError instanceof Error ? loadError.message : String(loadError),
      );
      return false;
    } finally {
      setLoading(false);
    }
  }, []);

  const loadSource = useCallback(
    async (file: File): Promise<boolean> => {
      setError(null);
      if (!detail) {
        setError('Load a variant detail JSON before adding a shader source file.');
        return false;
      }
      if (file.size > MAX_UPLOAD_BYTES) {
        setError('Source file is larger than 64 MB.');
        return false;
      }
      const expected = detail.source.split(/[\\/]/).pop() ?? detail.source;
      if (file.name.toLowerCase() !== expected.toLowerCase()) {
        setError(`Source file name must match ${expected}.`);
        return false;
      }
      try {
        const text = await file.text();
        if (!text.trim()) {
          throw new Error('Source file is empty.');
        }
        setSourceText(text);
        setSourceName(file.name);
        return true;
      } catch (loadError) {
        setError(
          loadError instanceof Error ? loadError.message : String(loadError),
        );
        return false;
      }
    },
    [detail],
  );

  const clear = useCallback(() => {
    setDetail(null);
    setSummary(null);
    setFileName('');
    setSourceText(null);
    setSourceName('');
    setError(null);
  }, []);

  const value = useMemo<UploadContextValue>(
    () => ({
      detail,
      summary,
      fileName,
      sourceText,
      sourceName,
      error,
      loading,
      loadFile,
      loadSource,
      clear,
    }),
    [
      clear,
      detail,
      error,
      fileName,
      loadFile,
      loadSource,
      loading,
      sourceName,
      sourceText,
      summary,
    ],
  );

  return <UploadContext.Provider value={value}>{children}</UploadContext.Provider>;
}

export function useUpload(): UploadContextValue {
  const value = useContext(UploadContext);
  if (!value) {
    throw new Error('useUpload must be used inside UploadProvider.');
  }
  return value;
}
