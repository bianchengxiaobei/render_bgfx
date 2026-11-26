import { useEffect, useMemo, useState } from 'react';
import { Link, useNavigate, useSearchParams } from 'react-router-dom';
import { loadIndex, reportLabel } from '../api';
import type { ReportDataset, ReportIndex } from '../types';
import {
  backendLabel,
  errorMessage,
  formatDate,
  formatMetric,
  shortHash,
  stageLabel,
} from '../utils/format';

function asDataset(value: string | null): ReportDataset {
  return value === 'reference' ? 'reference' : 'reports';
}

export default function IndexPage() {
  const [searchParams, setSearchParams] = useSearchParams();
  const navigate = useNavigate();
  const dataset = asDataset(searchParams.get('dataset'));
  const query = searchParams.get('q') ?? '';
  const backend = searchParams.get('backend') ?? 'all';

  const [index, setIndex] = useState<ReportIndex | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const controller = new AbortController();
    setLoading(true);
    setError(null);
    loadIndex(dataset, controller.signal)
      .then((data) => {
        setIndex(data);
        setLoading(false);
      })
      .catch((loadError: unknown) => {
        if (!controller.signal.aborted) {
          setError(errorMessage(loadError));
          setLoading(false);
        }
      });
    return () => controller.abort();
  }, [dataset]);

  const backends = useMemo(
    () =>
      [...new Set((index?.entries ?? []).map((entry) => entry.backend))].sort(),
    [index],
  );

  const entries = useMemo(() => {
    const needle = query.trim().toLowerCase();
    return (index?.entries ?? []).filter((entry) => {
      if (backend !== 'all' && entry.backend !== backend) {
        return false;
      }
      if (!needle) {
        return true;
      }
      return [entry.source, entry.stage, entry.backend, entry.summaryPath]
        .join(' ')
        .toLowerCase()
        .includes(needle);
    });
  }, [index, query, backend]);

  const totals = useMemo(
    () => ({
      programs: index?.entries.length ?? 0,
      variants: (index?.entries ?? []).reduce(
        (sum, entry) => sum + entry.variantCount,
        0,
      ),
      failed: (index?.entries ?? []).reduce(
        (sum, entry) => sum + entry.failedCount,
        0,
      ),
    }),
    [index],
  );

  function updateQuery(changes: Record<string, string>) {
    const next = new URLSearchParams(searchParams);
    for (const [key, value] of Object.entries(changes)) {
      if (!value || value === 'all') {
        next.delete(key);
      } else {
        next.set(key, value);
      }
    }
    setSearchParams(next, { replace: true });
  }

  function openProgram(summaryPath: string, entryBackend: string, source: string) {
    const params = new URLSearchParams({
      dataset,
      backend: entryBackend,
      summary: summaryPath,
      source,
    });
    navigate(`/program?${params.toString()}`);
  }

  return (
    <section>
      <div className="page-heading">
        <div>
          <p className="eyebrow">{reportLabel(dataset)} dataset</p>
          <h1>Shader reports</h1>
          <p className="muted">
            Browse indexed DXBC and SPIR-V programs, variants and disassembly.
          </p>
        </div>
        <Link className="button secondary" to={`/?dataset=${dataset}`}>
          Refresh
        </Link>
      </div>

      <div className="stat-grid">
        <div className="stat">
          <span>Programs</span>
          <strong>{formatMetric(totals.programs)}</strong>
        </div>
        <div className="stat">
          <span>Variants</span>
          <strong>{formatMetric(totals.variants)}</strong>
        </div>
        <div className="stat">
          <span>Failed variants</span>
          <strong className={totals.failed > 0 ? 'danger-text' : ''}>
            {formatMetric(totals.failed)}
          </strong>
        </div>
        <div className="stat">
          <span>Index generated</span>
          <strong className="stat-date">{formatDate(index?.generatedAt)}</strong>
        </div>
      </div>

      <div className="filter-bar">
        <label className="field grow">
          <span>Search source, stage or path</span>
          <input
            type="search"
            value={query}
            placeholder="forward, fragment, passes/..."
            onChange={(event) => updateQuery({ q: event.target.value })}
          />
        </label>
        <label className="field">
          <span>Backend</span>
          <select
            value={backend}
            onChange={(event) => updateQuery({ backend: event.target.value })}
          >
            <option value="all">All backends</option>
            {backends.map((value) => (
              <option key={value} value={value}>
                {backendLabel(value)}
              </option>
            ))}
          </select>
        </label>
        {(query || backend !== 'all') && (
          <button
            className="button ghost filter-clear"
            type="button"
            onClick={() => updateQuery({ q: '', backend: 'all' })}
          >
            Clear
          </button>
        )}
      </div>

      {loading && <div className="panel empty-state">Loading index…</div>}
      {error && (
        <div className="panel error-panel">
          <h2>Index unavailable</h2>
          <p>{error}</p>
          <p className="muted">
            Generate reports with <code>hbg_shader_analysis build</code>, then run{' '}
            <code>node tools/shader_analysis/serve.mjs</code>.
          </p>
        </div>
      )}

      {!loading && !error && (
        <div className="panel table-panel">
          <table className="data-table report-table">
            <thead>
              <tr>
                <th>Backend</th>
                <th>Program</th>
                <th>Stage</th>
                <th className="numeric">Variants</th>
                <th className="numeric">OK</th>
                <th className="numeric">Failed</th>
                <th>Source hash</th>
                <th>Generated</th>
              </tr>
            </thead>
            <tbody>
              {entries.map((entry) => (
                <tr
                  key={`${entry.backend}:${entry.summaryPath}`}
                  className="clickable-row"
                  tabIndex={0}
                  onClick={() =>
                    openProgram(entry.summaryPath, entry.backend, entry.source)
                  }
                  onKeyDown={(event) => {
                    if (event.key === 'Enter' || event.key === ' ') {
                      openProgram(entry.summaryPath, entry.backend, entry.source);
                    }
                  }}
                >
                  <td>
                    <span className={`backend-badge backend-${entry.backend}`}>
                      {entry.backend}
                    </span>
                  </td>
                  <td>
                    <div className="primary-cell">{entry.source}</div>
                    <div className="secondary-cell">{entry.summaryPath}</div>
                  </td>
                  <td>{stageLabel(entry.stage)}</td>
                  <td className="numeric">{formatMetric(entry.variantCount)}</td>
                  <td className="numeric ok-text">{formatMetric(entry.okCount)}</td>
                  <td
                    className={
                      entry.failedCount > 0 ? 'numeric danger-text' : 'numeric'
                    }
                  >
                    {formatMetric(entry.failedCount)}
                  </td>
                  <td>
                    <code title={entry.sourceSha256}>
                      {shortHash(entry.sourceSha256)}
                    </code>
                  </td>
                  <td className="nowrap">{formatDate(entry.generatedAt)}</td>
                </tr>
              ))}
            </tbody>
          </table>
          {entries.length === 0 && (
            <div className="empty-state compact">
              No programs match the current filters.
            </div>
          )}
        </div>
      )}
    </section>
  );
}
