import { useEffect, useMemo, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { loadSummary, reportLabel, sourceUrl } from '../api';
import type { ReportDataset, ReportSummary, VariantSummary } from '../types';
import { summaryToCsv } from '../utils/csv';
import { matchesKeywordFilter } from '../utils/filters';
import {
  backendLabel,
  copyText,
  downloadText,
  errorMessage,
  fileName,
  formatBytes,
  formatDate,
  formatDuration,
  formatMetric,
  shortHash,
  stageLabel,
  statusClass,
} from '../utils/format';

type SortKey =
  | 'variantId'
  | 'name'
  | 'status'
  | 'instructionCount'
  | 'tempRegisterMax'
  | 'tempRegisterPeak'
  | 'ssaValueCount'
  | 'maxLiveSsaValues'
  | 'compileMs'
  | 'binaryBytes';

function datasetFrom(value: string | null): ReportDataset {
  return value === 'reference' ? 'reference' : 'reports';
}

function sortValue(variant: VariantSummary, key: SortKey): string | number | null {
  switch (key) {
    case 'variantId':
      return variant.variantId;
    case 'name':
      return variant.name;
    case 'status':
      return variant.status;
    case 'compileMs':
      return variant.compileMs ?? null;
    case 'binaryBytes':
      return variant.binaryBytes ?? null;
    case 'instructionCount':
      return variant.metrics?.instructionCount ?? null;
    case 'tempRegisterMax':
      return variant.metrics?.tempRegisterMax ?? null;
    case 'tempRegisterPeak':
      return variant.metrics?.tempRegisterPeak ?? null;
    case 'ssaValueCount':
      return variant.metrics?.ssaValueCount ?? null;
    case 'maxLiveSsaValues':
      return variant.metrics?.maxLiveSsaValues ?? null;
  }
}

function CompareHeader({
  label,
  sortKey,
  active,
  direction,
  onSort,
  numeric = true,
}: {
  label: string;
  sortKey: SortKey;
  active: boolean;
  direction: 'asc' | 'desc';
  onSort: (key: SortKey) => void;
  numeric?: boolean;
}) {
  return (
    <th className={numeric ? 'numeric' : ''}>
      <button className="sort-button" type="button" onClick={() => onSort(sortKey)}>
        {label}
        {active ? (direction === 'asc' ? ' ↑' : ' ↓') : ''}
      </button>
    </th>
  );
}

export default function ProgramPage() {
  const [searchParams, setSearchParams] = useSearchParams();
  const dataset = datasetFrom(searchParams.get('dataset'));
  const summaryPath = searchParams.get('summary') ?? '';
  const source = searchParams.get('source') ?? '';
  const query = searchParams.get('q') ?? '';
  const statusFilter = searchParams.get('status') ?? 'all';
  const keywordFilter = searchParams.get('defines') ?? '';
  const [sortKey, setSortKey] = useState<SortKey>('variantId');
  const [direction, setDirection] = useState<'asc' | 'desc'>('asc');
  const [summary, setSummary] = useState<ReportSummary | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [copied, setCopied] = useState<string | null>(null);

  useEffect(() => {
    if (!summaryPath) {
      setLoading(false);
      setError('No summary path was supplied.');
      return;
    }
    const controller = new AbortController();
    setLoading(true);
    setError(null);
    loadSummary(dataset, summaryPath, controller.signal)
      .then((data) => {
        setSummary(data);
        setLoading(false);
      })
      .catch((loadError: unknown) => {
        if (!controller.signal.aborted) {
          setError(errorMessage(loadError));
          setLoading(false);
        }
      });
    return () => controller.abort();
  }, [dataset, summaryPath]);

  function updateQuery(key: string, value: string) {
    const next = new URLSearchParams(searchParams);
    if (!value || value === 'all') {
      next.delete(key);
    } else {
      next.set(key, value);
    }
    setSearchParams(next, { replace: true });
  }

  function handleSort(key: SortKey) {
    if (sortKey === key) {
      setDirection((current) => (current === 'asc' ? 'desc' : 'asc'));
    } else {
      setSortKey(key);
      setDirection('asc');
    }
  }

  const variants = useMemo(() => {
    const needle = query.trim().toLowerCase();
    const filtered = (summary?.variants ?? []).filter((variant) => {
      if (statusFilter !== 'all' && variant.status !== statusFilter) {
        return false;
      }
      if (!matchesKeywordFilter(variant, keywordFilter)) {
        return false;
      }
      if (!needle) {
        return true;
      }
      return [variant.name, variant.variantId, ...variant.canonicalDefines]
        .join(' ')
        .toLowerCase()
        .includes(needle);
    });
    return [...filtered].sort((left, right) => {
      const leftValue = sortValue(left, sortKey);
      const rightValue = sortValue(right, sortKey);
      if (leftValue == null && rightValue == null) {
        return 0;
      }
      if (leftValue == null) {
        return 1;
      }
      if (rightValue == null) {
        return -1;
      }
      const result =
        typeof leftValue === 'number' && typeof rightValue === 'number'
          ? leftValue - rightValue
          : String(leftValue).localeCompare(String(rightValue));
      return direction === 'asc' ? result : -result;
    });
  }, [summary, query, keywordFilter, statusFilter, sortKey, direction]);

  const okCount = summary?.variants.filter((variant) => variant.status === 'ok').length ?? 0;
  const failedCount =
    summary?.variants.filter((variant) => variant.status === 'failed').length ?? 0;

  async function handleCopy(value: string, label: string) {
    try {
      await copyText(value);
      setCopied(label);
      window.setTimeout(() => setCopied(null), 1500);
    } catch {
      setCopied('Copy failed');
    }
  }

  function downloadCsv() {
    if (!summary) {
      return;
    }
    downloadText(
      `${summary.backend}-${summary.stage}-${fileName(summary.source.path)}.csv`,
      summaryToCsv(summary),
      'text/csv',
    );
  }

  if (!summaryPath) {
    return (
      <section className="panel empty-state">
        <h1>No program selected</h1>
        <Link className="button" to={`/?dataset=${dataset}`}>
          Back to reports
        </Link>
      </section>
    );
  }

  return (
    <section>
      <div className="breadcrumbs">
        <Link to={`/?dataset=${dataset}`}>{reportLabel(dataset)}</Link>
        <span>/</span>
        <span>{summaryPath || 'Program'}</span>
      </div>

      {loading && <div className="panel empty-state">Loading summary…</div>}
      {error && (
        <div className="panel error-panel">
          <h2>Summary unavailable</h2>
          <p>{error}</p>
        </div>
      )}

      {!loading && !error && summary && (
        <>
          <div className="page-heading">
            <div>
              <p className="eyebrow">
                {backendLabel(summary.backend)} · {stageLabel(summary.stage)}
              </p>
              <h1 className="program-title">{summary.source.path}</h1>
              <p className="muted">
                {summary.variants.length} variants · {summary.profile} · optimization{' '}
                {summary.optimization} · {summary.debugInfo ? 'debug info' : 'release'}
              </p>
            </div>
            <div className="heading-actions">
              <button className="button secondary" type="button" onClick={downloadCsv}>
                Download CSV
              </button>
              <button
                className="button ghost"
                type="button"
                onClick={() => handleCopy(summaryPath, 'Summary path copied')}
              >
                Copy summary path
              </button>
              <a
                className="button ghost"
                href={sourceUrl(summary.source.path)}
                target="_blank"
                rel="noreferrer"
              >
                Open source
              </a>
            </div>
          </div>

          <div className="stat-grid five">
            <div className="stat">
              <span>Variants</span>
              <strong>{formatMetric(summary.variantCount)}</strong>
            </div>
            <div className="stat">
              <span>OK</span>
              <strong className="ok-text">{formatMetric(okCount)}</strong>
            </div>
            <div className="stat">
              <span>Failed</span>
              <strong className={failedCount > 0 ? 'danger-text' : ''}>
                {formatMetric(failedCount)}
              </strong>
            </div>
            <div className="stat">
              <span>Source SHA-256</span>
              <strong className="mono small" title={summary.source.sha256}>
                {shortHash(summary.source.sha256)}
              </strong>
            </div>
            <div className="stat">
              <span>Generated</span>
              <strong className="stat-date">{formatDate(summary.generatedAt)}</strong>
            </div>
          </div>

          <div className="panel meta-strip">
            <span>
              <strong>Shaderc</strong> {summary.shaderc.version || 'n/a'}
            </span>
            <span title={summary.shaderc.path}>
              <strong>Compiler</strong> {fileName(summary.shaderc.path)}
            </span>
            <span>
              <strong>Source</strong> {summary.source.path}
            </span>
            <span>
              <strong>Tool</strong> {summary.toolVersion}
            </span>
            {(source || summaryPath) && (
              <span className="muted">Deep link: {source || summaryPath}</span>
            )}
            {copied && <span className="copy-toast">{copied}</span>}
          </div>

          {dataset === 'reference' && (
            <p className="reference-note">
              Reference baselines contain summary JSON only; assembly details are not
              copied into the reference folder.
            </p>
          )}

          <div className="filter-bar">
            <label className="field grow">
              <span>Filter variants</span>
              <input
                type="search"
                value={query}
                placeholder="variant name, id or define…"
                onChange={(event) => updateQuery('q', event.target.value)}
              />
            </label>
            <label className="field grow">
              <span>Define filter (A&amp;B|C)</span>
              <input
                type="search"
                value={keywordFilter}
                placeholder="ALPHA_CLIP&HAS_NORMAL_MAP|DOUBLE_SIDED"
                onChange={(event) => updateQuery('defines', event.target.value)}
              />
            </label>
            <label className="field">
              <span>Status</span>
              <select
                value={statusFilter}
                onChange={(event) => updateQuery('status', event.target.value)}
              >
                <option value="all">All statuses</option>
                <option value="ok">ok</option>
                <option value="failed">failed</option>
                <option value="skipped">skipped</option>
              </select>
            </label>
            {(query || keywordFilter || statusFilter !== 'all') && (
              <button
                className="button ghost filter-clear"
                type="button"
                onClick={() => {
                  const next = new URLSearchParams(searchParams);
                  next.delete('q');
                  next.delete('defines');
                  next.delete('status');
                  setSearchParams(next, { replace: true });
                }}
              >
                Clear
              </button>
            )}
          </div>

          <div className="panel table-panel wide-table">
            <table className="data-table">
              <thead>
                <tr>
                  <CompareHeader
                    label="Variant"
                    sortKey="variantId"
                    active={sortKey === 'variantId'}
                    direction={direction}
                    onSort={handleSort}
                    numeric={false}
                  />
                  <th>Defines</th>
                  <CompareHeader
                    label="Status"
                    sortKey="status"
                    active={sortKey === 'status'}
                    direction={direction}
                    onSort={handleSort}
                    numeric={false}
                  />
                  <CompareHeader
                    label="Instr."
                    sortKey="instructionCount"
                    active={sortKey === 'instructionCount'}
                    direction={direction}
                    onSort={handleSort}
                  />
                  <CompareHeader
                    label="Temp max"
                    sortKey="tempRegisterMax"
                    active={sortKey === 'tempRegisterMax'}
                    direction={direction}
                    onSort={handleSort}
                  />
                  <CompareHeader
                    label="Temp peak"
                    sortKey="tempRegisterPeak"
                    active={sortKey === 'tempRegisterPeak'}
                    direction={direction}
                    onSort={handleSort}
                  />
                  <CompareHeader
                    label="SSA"
                    sortKey="ssaValueCount"
                    active={sortKey === 'ssaValueCount'}
                    direction={direction}
                    onSort={handleSort}
                  />
                  <CompareHeader
                    label="Live SSA"
                    sortKey="maxLiveSsaValues"
                    active={sortKey === 'maxLiveSsaValues'}
                    direction={direction}
                    onSort={handleSort}
                  />
                  <CompareHeader
                    label="Compile"
                    sortKey="compileMs"
                    active={sortKey === 'compileMs'}
                    direction={direction}
                    onSort={handleSort}
                  />
                  <CompareHeader
                    label="Binary"
                    sortKey="binaryBytes"
                    active={sortKey === 'binaryBytes'}
                    direction={direction}
                    onSort={handleSort}
                  />
                </tr>
              </thead>
              <tbody>
                {variants.map((variant) => (
                  <tr key={variant.variantId}>
                    <td>
                      {dataset === 'reference' ? (
                        <span className="variant-link" title="Summary-only reference">
                          {variant.name}
                        </span>
                      ) : (
                        <Link
                          className="variant-link"
                          to={`/variant?dataset=${dataset}&backend=${encodeURIComponent(
                            summary.backend,
                          )}&summary=${encodeURIComponent(
                            summaryPath,
                          )}&source=${encodeURIComponent(
                            summary.source.path,
                          )}&variantId=${encodeURIComponent(variant.variantId)}`}
                        >
                          {variant.name}
                        </Link>
                      )}
                      <div className="secondary-cell mono">{variant.variantId}</div>
                    </td>
                    <td>
                      <div className="define-list">
                        {variant.canonicalDefines.length > 0 ? (
                          variant.canonicalDefines.map((define) => (
                            <code key={define}>{define}</code>
                          ))
                        ) : (
                          <span className="muted">base variant</span>
                        )}
                      </div>
                    </td>
                    <td>
                      <span className={statusClass(variant.status)}>{variant.status}</span>
                      {variant.cacheHit && <span className="cache-badge">cache</span>}
                    </td>
                    <td className="numeric">
                      {formatMetric(variant.metrics?.instructionCount)}
                    </td>
                    <td className="numeric">
                      {formatMetric(variant.metrics?.tempRegisterMax)}
                    </td>
                    <td className="numeric">
                      {formatMetric(variant.metrics?.tempRegisterPeak)}
                    </td>
                    <td className="numeric">
                      {formatMetric(variant.metrics?.ssaValueCount)}
                    </td>
                    <td className="numeric">
                      {formatMetric(variant.metrics?.maxLiveSsaValues)}
                    </td>
                    <td className="numeric">{formatDuration(variant.compileMs)}</td>
                    <td className="numeric">{formatBytes(variant.binaryBytes)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
            {variants.length === 0 && (
              <div className="empty-state compact">No variants match the filter.</div>
            )}
          </div>
        </>
      )}
    </section>
  );
}
