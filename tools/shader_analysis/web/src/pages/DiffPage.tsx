import { diffLines } from 'diff';
import type { Change } from 'diff';
import { useEffect, useMemo, useRef, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import {
  detailUrlAt,
  loadDetailAt,
  loadDiff,
  summaryPathFor,
} from '../api';
import type { DetailReport, DiffEntry, DiffStatus, ReportDiff } from '../types';
import {
  backendLabel,
  errorMessage,
  formatDate,
  formatMetric,
  stageLabel,
} from '../utils/format';

const DEFAULT_DIFF = '/shader_analysis/reports/diff.json';
const DEFAULT_CURRENT = '/shader_analysis/reports';
const DEFAULT_REFERENCE = '/shader_analysis/reference';

function statusClass(status: DiffStatus): string {
  return `diff-status diff-status-${status}`;
}

function metricDeltaClass(value: number): string {
  if (value > 0) {
    return 'delta-regression';
  }
  if (value < 0) {
    return 'delta-improvement';
  }
  return 'delta-neutral';
}

function DiffChunk({ change }: { change: Change }) {
  const lines = change.value.split('\n');
  if (lines.length > 0 && lines[lines.length - 1] === '') {
    lines.pop();
  }
  const prefix = change.added ? '+' : change.removed ? '-' : ' ';
  const className = change.added
    ? 'diff-lines diff-added'
    : change.removed
      ? 'diff-lines diff-removed'
      : 'diff-lines diff-context';
  if (!change.added && !change.removed && lines.length > 20) {
    return <div className="diff-gap">{lines.length} unchanged lines</div>;
  }
  return (
    <div className={className}>
      {lines.map((line, index) => (
        <div className="diff-line" key={`${prefix}-${index}`}>
          <span className="diff-gutter">{prefix}</span>
          <span>{line || ' '}</span>
        </div>
      ))}
    </div>
  );
}

function assemblyText(detail: DetailReport): string {
  return detail.instructions.map((instruction) => instruction.text).join('\n');
}

export default function DiffPage() {
  const [searchParams, setSearchParams] = useSearchParams();
  const diffPath = searchParams.get('diff') ?? DEFAULT_DIFF;
  const currentRoot = searchParams.get('current') ?? DEFAULT_CURRENT;
  const referenceRoot = searchParams.get('reference') ?? DEFAULT_REFERENCE;
  const query = searchParams.get('q') ?? '';
  const backendFilter = searchParams.get('backend') ?? 'all';
  const statusFilter = searchParams.get('status') ?? 'all';
  const compareVariant = searchParams.get('compare');
  const [diffPathInput, setDiffPathInput] = useState(diffPath);
  const [currentInput, setCurrentInput] = useState(currentRoot);
  const [referenceInput, setReferenceInput] = useState(referenceRoot);
  const [diff, setDiff] = useState<ReportDiff | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [selected, setSelected] = useState<DiffEntry | null>(null);
  const [currentDetail, setCurrentDetail] = useState<DetailReport | null>(null);
  const [referenceDetail, setReferenceDetail] = useState<DetailReport | null>(null);
  const [compareLoading, setCompareLoading] = useState(false);
  const [compareError, setCompareError] = useState<string | null>(null);
  const compareRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const controller = new AbortController();
    setLoading(true);
    setError(null);
    loadDiff(diffPath, controller.signal)
      .then((loaded) => {
        setDiff(loaded);
        setLoading(false);
      })
      .catch((loadError: unknown) => {
        if (!controller.signal.aborted) {
          setError(errorMessage(loadError));
          setLoading(false);
        }
      });
    return () => controller.abort();
  }, [diffPath]);

  useEffect(() => {
    if (!diff || !compareVariant) {
      return;
    }
    const entry = diff.entries.find((item) => item.variantId === compareVariant);
    if (entry && selected?.variantId !== entry.variantId) {
      setSelected(entry);
    }
  }, [compareVariant, diff, selected]);

  useEffect(() => {
    if (!selected) {
      return;
    }
    const controller = new AbortController();
    setCompareLoading(true);
    setCompareError(null);
    setCurrentDetail(null);
    setReferenceDetail(null);
    const summaryPath = summaryPathFor(selected.source, selected.backend);
    Promise.all([
      loadDetailAt(currentRoot, summaryPath, selected.variantId, controller.signal),
      loadDetailAt(referenceRoot, summaryPath, selected.variantId, controller.signal),
    ])
      .then(([current, reference]) => {
        setCurrentDetail(current);
        setReferenceDetail(reference);
        setCompareLoading(false);
      })
      .catch((loadError: unknown) => {
        if (!controller.signal.aborted) {
          setCompareError(errorMessage(loadError));
          setCompareLoading(false);
        }
      });
    return () => controller.abort();
  }, [currentRoot, referenceRoot, selected]);

  useEffect(() => {
    if (currentDetail && referenceDetail) {
      requestAnimationFrame(() => compareRef.current?.scrollIntoView({ block: 'start' }));
    }
  }, [currentDetail, referenceDetail]);

  const backends = useMemo(
    () => [...new Set((diff?.entries ?? []).map((entry) => entry.backend))].sort(),
    [diff],
  );

  const entries = useMemo(() => {
    const needle = query.trim().toLowerCase();
    return (diff?.entries ?? []).filter((entry) => {
      if (backendFilter !== 'all' && entry.backend !== backendFilter) {
        return false;
      }
      if (statusFilter !== 'all' && entry.status !== statusFilter) {
        return false;
      }
      if (!needle) {
        return true;
      }
      return [entry.source, entry.stage, entry.variantId, ...entry.canonicalDefines]
        .join(' ')
        .toLowerCase()
        .includes(needle);
    });
  }, [backendFilter, diff, query, statusFilter]);

  const counts = useMemo(() => {
    const all = diff?.entries ?? [];
    return {
      total: all.length,
      changed: all.filter((entry) => entry.status === 'changed').length,
      incomparable: all.filter((entry) => entry.status === 'incomparable').length,
      added: diff?.addedVariants.length ?? 0,
      removed: diff?.removedVariants.length ?? 0,
    };
  }, [diff]);

  const changes = useMemo(() => {
    if (!currentDetail || !referenceDetail) {
      return [];
    }
    return diffLines(assemblyText(referenceDetail), assemblyText(currentDetail));
  }, [currentDetail, referenceDetail]);

  function updateQuery(changesToApply: Record<string, string>) {
    const next = new URLSearchParams(searchParams);
    for (const [key, value] of Object.entries(changesToApply)) {
      if (!value || value === 'all') {
        next.delete(key);
      } else {
        next.set(key, value);
      }
    }
    setSearchParams(next, { replace: true });
  }

  function applyPaths() {
    updateQuery({
      diff: diffPathInput,
      current: currentInput,
      reference: referenceInput,
    });
  }

  function startCompare(entry: DiffEntry) {
    setSelected(entry);
    setCurrentDetail(null);
    setReferenceDetail(null);
    updateQuery({ compare: entry.variantId });
  }

  function closeCompare() {
    setSelected(null);
    updateQuery({ compare: '' });
  }

  return (
    <section>
      <div className="page-heading">
        <div>
          <p className="eyebrow">Report comparison</p>
          <h1>Diff</h1>
          <p className="muted">
            Compare metric deltas and per-variant disassembly between current and
            reference report roots.
          </p>
        </div>
        <Link className="button secondary" to="/">
          Back to reports
        </Link>
      </div>

      <div className="panel diff-source-panel">
        <div className="diff-path-grid">
          <label className="field grow">
            <span>Diff JSON</span>
            <input
              value={diffPathInput}
              onChange={(event) => setDiffPathInput(event.target.value)}
            />
          </label>
          <label className="field grow">
            <span>Current root</span>
            <input
              value={currentInput}
              onChange={(event) => setCurrentInput(event.target.value)}
            />
          </label>
          <label className="field grow">
            <span>Reference root</span>
            <input
              value={referenceInput}
              onChange={(event) => setReferenceInput(event.target.value)}
            />
          </label>
          <button className="button" type="button" onClick={applyPaths}>
            Load
          </button>
        </div>
        {diff && (
          <div className="diff-side-meta">
            <span>
              <strong>Current</strong> {diff.current.root} ·{' '}
              {formatDate(diff.current.generatedAt)}
            </span>
            <span>
              <strong>Reference</strong> {diff.reference.root} ·{' '}
              {formatDate(diff.reference.generatedAt)}
            </span>
          </div>
        )}
      </div>

      {loading && <div className="panel empty-state">Loading diff…</div>}
      {error && (
        <div className="panel error-panel">
          <h2>Diff unavailable</h2>
          <p>{error}</p>
          <p className="muted">
            Generate one with{' '}
            <code>hbg_shader_analysis diff &lt;current&gt; &lt;reference&gt; --out shader_analysis/reports/diff.json</code>.
          </p>
        </div>
      )}

      {!loading && !error && diff && (
        <>
          <div className="stat-grid five">
            <div className="stat">
              <span>Compared variants</span>
              <strong>{formatMetric(counts.total)}</strong>
            </div>
            <div className="stat">
              <span>Changed</span>
              <strong className={counts.changed > 0 ? 'danger-text' : ''}>
                {formatMetric(counts.changed)}
              </strong>
            </div>
            <div className="stat">
              <span>Incomparable</span>
              <strong>{formatMetric(counts.incomparable)}</strong>
            </div>
            <div className="stat">
              <span>Added</span>
              <strong className="ok-text">{formatMetric(counts.added)}</strong>
            </div>
            <div className="stat">
              <span>Removed</span>
              <strong className={counts.removed > 0 ? 'danger-text' : ''}>
                {formatMetric(counts.removed)}
              </strong>
            </div>
          </div>

          {(diff.sourceHashChanged || diff.toolchainChanged) && (
            <div className="diff-warning">
              {diff.sourceHashChanged && (
                <span>Source hashes differ; matched variants are marked incomparable.</span>
              )}
              {diff.toolchainChanged && (
                <span>Compiler/toolchain fingerprints differ.</span>
              )}
            </div>
          )}

          <div className="filter-bar">
            <label className="field grow">
              <span>Search source, variant or define</span>
              <input
                type="search"
                value={query}
                onChange={(event) => updateQuery({ q: event.target.value })}
              />
            </label>
            <label className="field">
              <span>Backend</span>
              <select
                value={backendFilter}
                onChange={(event) => updateQuery({ backend: event.target.value })}
              >
                <option value="all">All backends</option>
                {backends.map((backend) => (
                  <option key={backend} value={backend}>
                    {backendLabel(backend)}
                  </option>
                ))}
              </select>
            </label>
            <label className="field">
              <span>Status</span>
              <select
                value={statusFilter}
                onChange={(event) => updateQuery({ status: event.target.value })}
              >
                <option value="all">All statuses</option>
                <option value="same">same</option>
                <option value="changed">changed</option>
                <option value="incomparable">incomparable</option>
              </select>
            </label>
          </div>

          <div className="panel table-panel">
            <table className="data-table diff-table">
              <thead>
                <tr>
                  <th>Status</th>
                  <th>Program</th>
                  <th>Variant</th>
                  <th>Defines</th>
                  <th>Metric deltas</th>
                  <th />
                </tr>
              </thead>
              <tbody>
                {entries.map((entry) => (
                  <tr key={`${entry.backend}:${entry.variantId}`}>
                    <td>
                      <span className={statusClass(entry.status)}>{entry.status}</span>
                    </td>
                    <td>
                      <div>{entry.source}</div>
                      <div className="secondary-cell">
                        {backendLabel(entry.backend)} · {stageLabel(entry.stage)}
                      </div>
                    </td>
                    <td className="mono">{entry.variantId}</td>
                    <td>
                      <div className="define-list">
                        {entry.canonicalDefines.length ? (
                          entry.canonicalDefines.map((define) => (
                            <code key={define}>{define}</code>
                          ))
                        ) : (
                          <span className="muted">base</span>
                        )}
                      </div>
                    </td>
                    <td>
                      <div className="delta-list">
                        {Object.entries(entry.metricDeltas).length ? (
                          Object.entries(entry.metricDeltas)
                            .sort(([left], [right]) => left.localeCompare(right))
                            .map(([metric, value]) => (
                              <span
                                className={`metric-delta ${metricDeltaClass(value)}`}
                                key={metric}
                                title={`${metric}: ${value > 0 ? '+' : ''}${value}`}
                              >
                                {metric} {value > 0 ? '+' : ''}
                                {formatMetric(value)}
                              </span>
                            ))
                        ) : (
                          <span className="muted">
                            {entry.status === 'same'
                              ? 'no metric changes'
                              : entry.status === 'incomparable'
                                ? 'source/toolchain mismatch'
                                : 'metadata change'}
                          </span>
                        )}
                      </div>
                    </td>
                    <td>
                      {entry.status === 'changed' ||
                      entry.status === 'same' ||
                      entry.status === 'incomparable' ? (
                        <button
                          className="button ghost small-button"
                          type="button"
                          onClick={() => startCompare(entry)}
                        >
                          Assembly diff
                        </button>
                      ) : null}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
            {entries.length === 0 && (
              <div className="empty-state compact">No entries match the filters.</div>
            )}
          </div>

          <div className="two-column">
            <div className="panel">
              <h2>Added variants</h2>
              {diff.addedVariants.length === 0 ? (
                <p className="muted">None.</p>
              ) : (
                <div className="variant-ref-list">
                  {diff.addedVariants.map((variant) => (
                    <div className="variant-ref" key={`${variant.backend}:${variant.variantId}`}>
                      <span className={statusClass(variant.status)}>{variant.status}</span>
                      <span>{variant.source}</span>
                      <code>{variant.variantId}</code>
                    </div>
                  ))}
                </div>
              )}
            </div>
            <div className="panel">
              <h2>Removed variants</h2>
              {diff.removedVariants.length === 0 ? (
                <p className="muted">None.</p>
              ) : (
                <div className="variant-ref-list">
                  {diff.removedVariants.map((variant) => (
                    <div className="variant-ref" key={`${variant.backend}:${variant.variantId}`}>
                      <span className={statusClass(variant.status)}>{variant.status}</span>
                      <span>{variant.source}</span>
                      <code>{variant.variantId}</code>
                    </div>
                  ))}
                </div>
              )}
            </div>
          </div>

          {selected && (
            <div className="panel assembly-diff-panel" ref={compareRef}>
              <div className="section-heading">
                <div>
                  <h2>Assembly diff</h2>
                  <p className="muted mono">
                    {selected.source} · {selected.variantId} · {selected.backend}
                  </p>
                </div>
                <button
                  className="button ghost"
                  type="button"
                  onClick={closeCompare}
                >
                  Close
                </button>
              </div>
              {compareLoading && (
                <div className="empty-state compact">Loading current and reference details…</div>
              )}
              {compareError && (
                <div className="empty-state compact">
                  <p>Assembly diff unavailable: {compareError}</p>
                  <p className="muted">
                    Reference baselines intentionally contain summaries only. Point the
                    reference root at a report directory with variant details to compare
                    assembly.
                  </p>
                  <p className="muted">
                    Current detail: {detailUrlAt(currentRoot, summaryPathFor(selected.source, selected.backend), selected.variantId)}
                  </p>
                </div>
              )}
              {!compareLoading && !compareError && currentDetail && referenceDetail && (
                <>
                  <div className="diff-counts">
                    <span>{assemblyText(referenceDetail).split('\n').length} reference lines</span>
                    <span>{assemblyText(currentDetail).split('\n').length} current lines</span>
                    <span>{changes.filter((change) => change.added).length} added chunks</span>
                    <span>{changes.filter((change) => change.removed).length} removed chunks</span>
                  </div>
                  <div className="assembly-diff">
                    {changes.map((change, index) => (
                      <DiffChunk key={index} change={change} />
                    ))}
                  </div>
                </>
              )}
            </div>
          )}
        </>
      )}
    </section>
  );
}
