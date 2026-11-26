import { useEffect, useMemo, useState } from 'react';
import type { KeyboardEvent } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { detailRelativePath, loadDetail, loadSummary, reportLabel, sourceUrl } from '../api';
import AssemblyList from '../components/AssemblyList';
import CBufferList from '../components/CBufferList';
import RegisterTimeline from '../components/RegisterTimeline';
import SourceView from '../components/SourceView';
import type {
  DetailReport,
  Metrics,
  ReportDataset,
  ReportSummary,
  VariantSummary,
} from '../types';
import {
  clampMatchPosition,
  matchingInstructionIndices,
} from '../utils/assemblySearch';
import {
  backendLabel,
  copyText,
  errorMessage,
  formatBytes,
  formatDate,
  formatDuration,
  formatMetric,
  shadercVersion,
  stageLabel,
  statusClass,
} from '../utils/format';

type Tab = 'overview' | 'assembly' | 'registers' | 'cbuffer' | 'source';

const TABS: Tab[] = ['overview', 'assembly', 'registers', 'cbuffer', 'source'];

function datasetFrom(value: string | null): ReportDataset {
  return value === 'reference' ? 'reference' : 'reports';
}

interface MetricCard {
  label: string;
  value: string;
  hint?: string;
}

function metricCards(backend: string, metrics: Metrics | null | undefined): MetricCard[] {
  const value = (field: number | null | undefined) => formatMetric(field);
  const m = metrics ?? {};
  const cards: MetricCard[] = [
    { label: 'Instructions', value: value(m.instructionCount) },
    { label: 'Branches', value: value(m.branchCount) },
    { label: 'Loops', value: value(m.loopCount) },
    { label: 'Warnings', value: value(m.warningCount) },
    { label: 'Errors', value: value(m.errorCount) },
    { label: 'Source lines', value: value(m.sourceLineCount) },
  ];

  if (backend === 'dxbc') {
    cards.splice(
      1,
      0,
      { label: 'Temp max', value: value(m.tempRegisterMax), hint: 'r# registers' },
      { label: 'Temp peak', value: value(m.tempRegisterPeak), hint: 'peak live' },
      { label: 'Inputs', value: value(m.inputCount) },
      { label: 'Outputs', value: value(m.outputCount) },
      { label: 'Cbuffer slots', value: value(m.cbufferSlotCount) },
      { label: 'Cbuffer dwords', value: value(m.cbufferDwordsRead) },
      { label: 'Dynamic cbuffer', value: value(m.dynamicCBufferReadCount) },
      { label: 'Textures', value: value(m.textureCount) },
      { label: 'Samplers', value: value(m.samplerCount) },
      { label: 'Samples', value: value(m.sampleCount) },
      { label: 'Loads', value: value(m.loadCount) },
      { label: 'Stores', value: value(m.storeCount) },
      { label: 'UAVs', value: value(m.uavCount) },
      { label: 'Structured buffers', value: value(m.structuredBufferCount) },
    );
  } else {
    cards.splice(
      1,
      0,
      { label: 'SSA values', value: value(m.ssaValueCount), hint: 'SPIR-V ids' },
      { label: 'Live SSA', value: value(m.maxLiveSsaValues), hint: 'linear estimate' },
      { label: 'Images', value: value(m.imageCount) },
      { label: 'Samplers', value: value(m.samplerCount) },
      { label: 'Uniform buffers', value: value(m.uniformBufferCount) },
      { label: 'UBO bytes', value: value(m.uniformBufferBytes) },
      { label: 'Storage buffers', value: value(m.storageBufferCount) },
      { label: 'Push constants', value: value(m.pushConstantCount) },
      { label: 'Push constant bytes', value: value(m.pushConstantBytes) },
      { label: 'Access chains', value: value(m.accessChainCount) },
      { label: 'Loads', value: value(m.loadCount) },
      { label: 'Stores', value: value(m.storeCount) },
      { label: 'Cbuffer dwords', value: value(m.cbufferDwordsRead) },
    );
  }
  return cards;
}

export default function VariantPage() {
  const [searchParams, setSearchParams] = useSearchParams();
  const dataset = datasetFrom(searchParams.get('dataset'));
  const summaryPath = searchParams.get('summary') ?? '';
  const variantId = searchParams.get('variantId') ?? '';
  const requestedSource = searchParams.get('source') ?? '';
  const sourceSearch = searchParams.get('q') ?? '';
  const sourceHitParam = Number.parseInt(searchParams.get('qhit') ?? '', 10);
  const sourceMatch = Number.isFinite(sourceHitParam) ? sourceHitParam - 1 : 0;
  const assemblyQuery = searchParams.get('asmq') ?? '';
  const assemblyFilter = searchParams.get('asmfilter') === 'matches';
  const assemblyContext = searchParams.get('asmcontext') === '1';
  const requestedTab = searchParams.get('tab') as Tab | null;
  const tab: Tab = requestedTab && TABS.includes(requestedTab) ? requestedTab : 'overview';
  const lineParam = Number.parseInt(searchParams.get('line') ?? '', 10);
  const highlightLine = Number.isFinite(lineParam) ? lineParam : null;
  const instructionParam = Number.parseInt(searchParams.get('instruction') ?? '', 10);
  const focusInstruction = Number.isFinite(instructionParam) ? instructionParam : null;
  const cbLineParam = Number.parseInt(searchParams.get('cbline') ?? '', 10);
  const focusCbufferLine = Number.isFinite(cbLineParam) ? cbLineParam : null;
  const cbufferView = searchParams.get('cbview') === 'order' ? 'order' : 'table';
  const focusRegisterKey = searchParams.get('reg');
  const focusRegisterClass = searchParams.get('regclass');
  const registerSort = searchParams.get('regsort') === 'index' ? 'index' : 'lifetime';
  const pageParam = Number.parseInt(searchParams.get('regpage') ?? '', 10);
  const registerPage = Number.isFinite(pageParam) && pageParam > 0 ? pageParam : 0;
  const laneClass = searchParams.get('laneclass');
  const assemblyHitParam = Number.parseInt(searchParams.get('asmhit') ?? '', 10);
  const [summary, setSummary] = useState<ReportSummary | null>(null);
  const [detail, setDetail] = useState<DetailReport | null>(null);
  const [loading, setLoading] = useState(true);
  const [detailLoading, setDetailLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [detailError, setDetailError] = useState<string | null>(null);
  const [copied, setCopied] = useState<string | null>(null);
  const [mountedTabs, setMountedTabs] = useState<Set<Tab>>(() => new Set());

  useEffect(() => {
    setMountedTabs((current) =>
      current.has(tab) ? current : new Set<Tab>(current).add(tab),
    );
  }, [tab]);

  const tabMounted = (value: Tab) => value === tab || mountedTabs.has(value);
  const assemblyMatches = useMemo(
    () => matchingInstructionIndices(detail?.instructions ?? [], assemblyQuery),
    [detail, assemblyQuery],
  );
  const assemblyMatch = clampMatchPosition(
    Number.isFinite(assemblyHitParam) ? assemblyHitParam - 1 : 0,
    assemblyMatches.length,
  );

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

  useEffect(() => {
    if (!summaryPath || !variantId) {
      setDetailLoading(false);
      setDetailError('No variant id was supplied.');
      return;
    }
    if (!summary) {
      return;
    }
    const variantEntry = summary.variants.find((item) => item.variantId === variantId);
    if (!variantEntry) {
      setDetail(null);
      setDetailError('Variant id was not found in this summary.');
      setDetailLoading(false);
      return;
    }
    const controller = new AbortController();
    setDetailLoading(true);
    setDetailError(null);
    setDetail(null);
    loadDetail(
      dataset,
      summaryPath,
      variantId,
      controller.signal,
      variantEntry.detailPath,
    )
      .then((data) => {
        setDetail(data);
        setDetailLoading(false);
      })
      .catch((loadError: unknown) => {
        if (!controller.signal.aborted) {
          setDetailError(errorMessage(loadError));
          setDetailLoading(false);
        }
      });
    return () => controller.abort();
  }, [dataset, summary, summaryPath, variantId]);

  const variant = useMemo<VariantSummary | null>(
    () => summary?.variants.find((item) => item.variantId === variantId) ?? null,
    [summary, variantId],
  );

  const cards = useMemo(
    () => metricCards(summary?.backend ?? '', variant?.metrics),
    [summary?.backend, variant?.metrics],
  );

  const opcodeRows = useMemo(() => {
    const histogram = variant?.metrics?.opcodeHistogram ?? {};
    return Object.entries(histogram)
      .sort((left, right) => right[1] - left[1])
      .slice(0, 16);
  }, [variant]);

  async function handleCopy(value: string, label: string) {
    try {
      await copyText(value);
      setCopied(label);
      window.setTimeout(() => setCopied(null), 1500);
    } catch {
      setCopied('Copy failed');
    }
  }

  function selectTab(next: Tab) {
    const params = new URLSearchParams(searchParams);
    if (next === 'overview') {
      params.delete('tab');
    } else {
      params.set('tab', next);
    }
    if (next === 'cbuffer') {
      params.delete('cbline');
    }
    setSearchParams(params, { replace: true });
  }

  function handleTabKeyDown(event: KeyboardEvent<HTMLDivElement>) {
    if (event.altKey || event.ctrlKey || event.metaKey) {
      return;
    }
    const current = TABS.indexOf(tab);
    if (current < 0) {
      return;
    }
    let next: Tab | null = null;
    if (event.key === 'ArrowRight') {
      next = TABS[(current + 1) % TABS.length];
    } else if (event.key === 'ArrowLeft') {
      next = TABS[(current - 1 + TABS.length) % TABS.length];
    } else if (event.key === 'Home') {
      next = TABS[0];
    } else if (event.key === 'End') {
      next = TABS[TABS.length - 1];
    }
    if (next == null) {
      return;
    }
    event.preventDefault();
    selectTab(next);
    window.requestAnimationFrame(() => {
      document.getElementById(`variant-tab-${next}`)?.focus();
    });
  }

  function jumpToSource(line: number) {
    const params = new URLSearchParams(searchParams);
    params.set('tab', 'source');
    params.set('line', String(line));
    setSearchParams(params, { replace: true });
  }

  function jumpToAssembly(instructionIndex: number) {
    const params = new URLSearchParams(searchParams);
    params.set('tab', 'assembly');
    params.set('instruction', String(instructionIndex));
    setSearchParams(params, { replace: true });
  }

  function jumpToCbuffer(sourceLine?: number) {
    const params = new URLSearchParams(searchParams);
    params.set('tab', 'cbuffer');
    if (sourceLine == null) {
      params.delete('cbline');
    } else {
      params.set('cbline', String(sourceLine));
    }
    setSearchParams(params, { replace: true });
  }

  function jumpToRegisters(instructionIndex: number) {
    const params = new URLSearchParams(searchParams);
    params.set('tab', 'registers');
    params.set('instruction', String(instructionIndex));
    params.delete('reg');
    setSearchParams(params, { replace: true });
  }

  function jumpToRegisterLane(registerKey: string, instructionIndex: number) {
    const params = new URLSearchParams(searchParams);
    params.set('tab', 'registers');
    params.set('instruction', String(instructionIndex));
    params.set('reg', registerKey);
    const separator = registerKey.indexOf(':');
    if (separator > 0) {
      params.set('regclass', registerKey.slice(0, separator));
    }
    setSearchParams(params, { replace: true });
  }

  function selectRegister(registerKey: string | null) {
    if (registerKey === focusRegisterKey) {
      return;
    }
    const params = new URLSearchParams(searchParams);
    if (registerKey) {
      params.set('reg', registerKey);
    } else {
      params.delete('reg');
    }
    setSearchParams(params, { replace: true });
  }

  function selectRegisterClass(className: string) {
    const params = new URLSearchParams(searchParams);
    params.set('regclass', className);
    params.delete('reg');
    params.delete('regpage');
    setSearchParams(params, { replace: true });
  }

  function selectRegisterSort(next: 'lifetime' | 'index') {
    const params = new URLSearchParams(searchParams);
    if (next === 'index') {
      params.set('regsort', 'index');
    } else {
      params.delete('regsort');
    }
    params.delete('regpage');
    setSearchParams(params, { replace: true });
  }

  function selectRegisterPage(next: number) {
    const params = new URLSearchParams(searchParams);
    if (next > 0) {
      params.set('regpage', String(next));
    } else {
      params.delete('regpage');
    }
    setSearchParams(params, { replace: true });
  }

  function selectLaneClass(next: string | null) {
    const params = new URLSearchParams(searchParams);
    if (next) {
      params.set('laneclass', next);
    } else {
      params.delete('laneclass');
    }
    setSearchParams(params, { replace: true });
  }

  function selectCbufferView(next: 'table' | 'order') {
    const params = new URLSearchParams(searchParams);
    if (next === 'order') {
      params.set('cbview', 'order');
    } else {
      params.delete('cbview');
    }
    setSearchParams(params, { replace: true });
  }


  function selectInstruction(instructionIndex: number) {
    const params = new URLSearchParams(searchParams);
    params.set('instruction', String(instructionIndex));
    setSearchParams(params, { replace: true });
  }

  function clearInstruction() {
    const params = new URLSearchParams(searchParams);
    params.delete('instruction');
    setSearchParams(params, { replace: true });
  }

  function setSourceSearch(value: string) {
    const params = new URLSearchParams(searchParams);
    if (value) {
      params.set('q', value);
    } else {
      params.delete('q');
    }
    params.delete('qhit');
    setSearchParams(params, { replace: true });
  }

  function setSourceMatch(position: number) {
    const params = new URLSearchParams(searchParams);
    params.set('qhit', String(position + 1));
    setSearchParams(params, { replace: true });
  }

  function setAssemblySearch(value: string) {
    const params = new URLSearchParams(searchParams);
    if (value) {
      params.set('asmq', value);
    } else {
      params.delete('asmq');
    }
    params.delete('asmhit');
    setSearchParams(params, { replace: true });
  }

  function setAssemblyFilter(value: boolean) {
    const params = new URLSearchParams(searchParams);
    if (value) {
      params.set('asmfilter', 'matches');
    } else {
      params.delete('asmfilter');
    }
    params.delete('asmhit');
    setSearchParams(params, { replace: true });
  }

  function setAssemblyContext(value: boolean) {
    const params = new URLSearchParams(searchParams);
    if (value) {
      params.set('asmcontext', '1');
    } else {
      params.delete('asmcontext');
    }
    setSearchParams(params, { replace: true });
  }

  function setAssemblyMatch(position: number) {
    const params = new URLSearchParams(searchParams);
    params.set('asmhit', String(position + 1));
    setSearchParams(params, { replace: true });
  }

  function stepAssemblyMatch(direction: -1 | 1) {
    if (assemblyMatches.length === 0) {
      return;
    }
    const current = assemblyMatch ?? 0;
    setAssemblyMatch(
      (current + direction + assemblyMatches.length) % assemblyMatches.length,
    );
  }

  function handleAssemblySearchKeyDown(event: KeyboardEvent<HTMLInputElement>) {
    if (event.key !== 'Enter' && event.key !== 'F3') {
      return;
    }
    event.preventDefault();
    stepAssemblyMatch(event.shiftKey ? -1 : 1);
  }

  const programLink = `/?dataset=${dataset}`;

  if (!summaryPath || !variantId) {
    return (
      <section className="panel empty-state">
        <h1>No variant selected</h1>
        <Link className="button" to={programLink}>
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
        <Link
          to={`/program?dataset=${dataset}&summary=${encodeURIComponent(
            summaryPath,
          )}&source=${encodeURIComponent(requestedSource)}`}
        >
          {summary?.source.path ?? summaryPath}
        </Link>
        <span>/</span>
        <span>{variant?.name ?? variantId}</span>
      </div>

      {loading && <div className="panel empty-state">Loading variant…</div>}
      {error && (
        <div className="panel error-panel">
          <h2>Variant summary unavailable</h2>
          <p>{error}</p>
        </div>
      )}

      {!loading && !error && summary && (
        <>
          <div className="page-heading">
            <div>
              <p className="eyebrow">
                {backendLabel(summary.backend)} · {stageLabel(summary.stage)}
                {summary.profile ? ` · ${summary.profile}` : ''}
                {summary.optimization != null ? ` · -O${summary.optimization}` : ''}
              </p>
              <h1 className="program-title">{variant?.name ?? variantId}</h1>
              <p className="muted mono">
                {variantId} · {summary.source.path}
              </p>
            </div>
            <div className="heading-actions">
              <button
                className="button ghost"
                type="button"
                onClick={() => handleCopy(variantId, 'Variant id copied')}
              >
                Copy variant id
              </button>
              <button
                className="button ghost"
                type="button"
                onClick={() =>
                  handleCopy(
                    variant?.detailPath ?? detailRelativePath(summaryPath, variantId),
                    'Detail path copied',
                  )
                }
              >
                Copy detail path
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

          <div className="variant-meta">
            <span className={statusClass(variant?.status ?? 'failed')}>
              {variant?.status ?? 'missing'}
            </span>
            <span>{variant?.cacheHit ? 'cache hit' : 'compiled/cache miss'}</span>
            <span>{formatDuration(variant?.compileMs)}</span>
            <span>{formatBytes(variant?.binaryBytes)}</span>
            <span title={summary.shaderc.version}>
              shaderc {shadercVersion(summary.shaderc.version)}
            </span>
            <span>{formatDate(summary.generatedAt)}</span>
            {copied && <span className="copy-toast">{copied}</span>}
          </div>

          {summary.debugInfo && (
            <div className="debug-banner">
              <strong>Debug build.</strong> SPIR-V debug info is enabled; optimization may be
              disabled and metrics are not directly comparable with release reports.
            </div>
          )}

          <div className="panel defines-panel">
            <span className="muted">Defines</span>
            <div className="define-list">
              {variant?.canonicalDefines.length ? (
                variant.canonicalDefines.map((define) => <code key={define}>{define}</code>)
              ) : (
                <span className="muted">base variant</span>
              )}
            </div>
          </div>

          <div
            className="tabs"
            role="tablist"
            aria-label="Variant views"
            onKeyDown={handleTabKeyDown}
          >
            {TABS.map((value) => (
              <button
                key={value}
                id={`variant-tab-${value}`}
                type="button"
                role="tab"
                aria-selected={tab === value}
                aria-controls={`variant-panel-${value}`}
                tabIndex={tab === value ? 0 : -1}
                className={tab === value ? 'tab active' : 'tab'}
                onClick={() => selectTab(value)}
              >
                {value === 'cbuffer'
                  ? 'CBuffer'
                  : value.charAt(0).toUpperCase() + value.slice(1)}
              </button>
            ))}
          </div>

          {tabMounted('overview') && (
            <div
              id="variant-panel-overview"
              role="tabpanel"
              aria-labelledby="variant-tab-overview"
              hidden={tab !== 'overview'}
            >
            <>
              <div className="metric-grid">
                {cards.map((card) => (
                  <div className="metric-card" key={card.label}>
                    <span>{card.label}</span>
                    <strong>{card.value}</strong>
                    {card.hint && <small>{card.hint}</small>}
                  </div>
                ))}
              </div>

              <div className="two-column">
                <div className="panel">
                  <h2>Opcode histogram</h2>
                  {opcodeRows.length ? (
                    <table className="data-table compact-table">
                      <thead>
                        <tr>
                          <th>Opcode</th>
                          <th className="numeric">Count</th>
                        </tr>
                      </thead>
                      <tbody>
                        {opcodeRows.map(([opcode, count]) => (
                          <tr key={opcode}>
                            <td className="mono">{opcode}</td>
                            <td className="numeric">{formatMetric(count)}</td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  ) : (
                    <p className="muted">No histogram in this summary.</p>
                  )}
                </div>

                <div className="panel">
                  <h2>Resources</h2>
                  {detailLoading ? (
                    <p className="muted">Loading detail…</p>
                  ) : detailError ? (
                    <p className="muted">Detail unavailable: {detailError}</p>
                  ) : detail && detail.resources.length ? (
                    <table className="data-table compact-table">
                      <thead>
                        <tr>
                          <th>Binding</th>
                          <th>Kind</th>
                          <th>Name</th>
                          <th className="numeric">Size</th>
                        </tr>
                      </thead>
                      <tbody>
                        {[...detail.resources]
                          .sort((left, right) => left.binding - right.binding)
                          .map((resource) => (
                            <tr key={`${resource.kind}:${resource.binding}:${resource.name}`}>
                              <td className="mono">{resource.binding}</td>
                              <td>{resource.kind}</td>
                              <td className="mono">{resource.name}</td>
                              <td className="numeric">{formatBytes(resource.size)}</td>
                            </tr>
                          ))}
                      </tbody>
                    </table>
                  ) : (
                    <p className="muted">No resources in this detail.</p>
                  )}
                </div>
              </div>

              <div className="panel">
                <h2>Diagnostics</h2>
                {(variant?.logs?.warnings.length ?? 0) === 0 &&
                (variant?.logs?.errors.length ?? 0) === 0 ? (
                  <p className="muted">No warnings or errors.</p>
                ) : (
                  <div className="log-list">
                    {variant?.logs?.errors.map((log, index) => (
                      <div className="log-entry log-error" key={`error-${index}`}>
                        <strong>error</strong>
                        <pre>{[log.message, ...log.stacktrace].join('\n')}</pre>
                      </div>
                    ))}
                    {variant?.logs?.warnings.map((log, index) => (
                      <div className="log-entry log-warning" key={`warning-${index}`}>
                        <strong>warning</strong>
                        <pre>{[log.message, ...log.stacktrace].join('\n')}</pre>
                      </div>
                    ))}
                  </div>
                )}
              </div>
            </>
            </div>
          )}

          {tabMounted('assembly') && (
            <div
              id="variant-panel-assembly"
              role="tabpanel"
              aria-labelledby="variant-tab-assembly"
              className="panel assembly-panel"
              hidden={tab !== 'assembly'}
            >
              <div className="assembly-heading">
                <div>
                  <h2>Assembly</h2>
                  <p className="muted">
                    {detailLoading
                      ? 'Loading disassembly…'
                      : detail
                        ? `${detail.instructions.length.toLocaleString('en-US')} instructions · ${detail.liveness.length.toLocaleString('en-US')} live intervals`
                        : 'Detail unavailable'}
                  </p>
                </div>
                <div className="assembly-controls">
                  <button
                    className="button ghost small-button"
                    type="button"
                    disabled={focusInstruction == null}
                    title={
                      focusInstruction == null
                        ? 'Click an assembly row to focus an instruction'
                        : `Inspect registers at instruction ${focusInstruction}`
                    }
                    onClick={() => selectTab('registers')}
                  >
                    Inspect registers
                  </button>
                  <label className="checkbox-field">
                    <input
                      type="checkbox"
                      checked={assemblyFilter}
                      onChange={(event) => setAssemblyFilter(event.target.checked)}
                    />
                    <span>Filter matches</span>
                  </label>
                  <label className="checkbox-field">
                    <input
                      type="checkbox"
                      checked={assemblyContext}
                      onChange={(event) => setAssemblyContext(event.target.checked)}
                    />
                    <span>Source context</span>
                  </label>
                  <label className="field grow">
                    <span>Search instructions</span>
                    <input
                      type="search"
                      value={assemblyQuery}
                      placeholder="opcode, register, operand…"
                      onChange={(event) => setAssemblySearch(event.target.value)}
                      onKeyDown={handleAssemblySearchKeyDown}
                    />
                  </label>
                </div>
              </div>
              {detailError ? (
                <div className="empty-state compact">
                  <p>Detail unavailable: {detailError}</p>
                  <p className="muted">
                    Failed variants may not have a detail JSON. Check the summary logs.
                  </p>
                </div>
              ) : detailLoading ? (
                <div className="empty-state compact">Loading disassembly…</div>
              ) : detail ? (
                <AssemblyList
                  instructions={detail.instructions}
                  liveness={detail.liveness}
                  selectedLane={focusRegisterKey}
                  laneClass={laneClass}
                  onSelectLaneClass={selectLaneClass}
                  query={assemblyQuery}
                  backend={summary.backend}
                  filterMatches={assemblyFilter}
                  focusIndex={focusInstruction}
                  matchPosition={assemblyMatch}
                  onSelectMatch={setAssemblyMatch}
                  sourcePath={summary.source.path}
                  showSourceContext={assemblyContext}
                  onSourceLine={jumpToSource}
                  onSelectInstruction={selectInstruction}
                  onInspectRegister={jumpToRegisterLane}
                />
              ) : (
                <div className="empty-state compact">No detail loaded.</div>
              )}
            </div>
          )}

          {tabMounted('registers') && (
            <div
              id="variant-panel-registers"
              role="tabpanel"
              aria-labelledby="variant-tab-registers"
              className="panel"
              hidden={tab !== 'registers'}
            >
              <div className="section-heading">
                <div>
                  <h2>Register timeline</h2>
                  <p className="muted">
                    Lifetime strips, read/write marks and pressure are static linear-scan
                    estimates. Click a lane to follow its instruction dependency chain.
                  </p>
                </div>
              </div>
              {detailError ? (
                <div className="empty-state compact">Detail unavailable: {detailError}</div>
              ) : detailLoading ? (
                <div className="empty-state compact">Loading register data…</div>
              ) : detail ? (
                <RegisterTimeline
                  detail={detail}
                  backend={summary.backend}
                  focusInstruction={focusInstruction}
                  focusRegister={focusRegisterKey}
                  focusClass={focusRegisterClass}
                  sortMode={registerSort}
                  page={registerPage}
                  sourcePath={summary.source.path}
                  onSelectRegister={selectRegister}
                  onSelectClass={selectRegisterClass}
                  onSelectSort={selectRegisterSort}
                  onSelectPage={selectRegisterPage}
                  onFocusInstruction={jumpToAssembly}
                  onClearFocus={clearInstruction}
                  onSourceLine={jumpToSource}
                />
              ) : (
                <div className="empty-state compact">No detail loaded.</div>
              )}
            </div>
          )}

          {tabMounted('cbuffer') && (
            <div
              id="variant-panel-cbuffer"
              role="tabpanel"
              aria-labelledby="variant-tab-cbuffer"
              className="panel"
              hidden={tab !== 'cbuffer'}
            >
              <div className="section-heading">
                <div>
                  <h2>CBuffer read order</h2>
                  <p className="muted">
                    Offset order is checked per slot. Click an instruction index to jump
                    back to Assembly; source-mapped reads link to their source line.
                  </p>
                </div>
              </div>
              {detailError ? (
                <div className="empty-state compact">Detail unavailable: {detailError}</div>
              ) : detailLoading ? (
                <div className="empty-state compact">Loading cbuffer reads…</div>
              ) : detail ? (
                <CBufferList
                  reads={detail.cbufferReads}
                  instructions={detail.instructions}
                  focusSourceLine={focusCbufferLine}
                  view={cbufferView}
                  onViewChange={selectCbufferView}
                  onClearFocus={() => jumpToCbuffer()}
                  onJumpInstruction={jumpToAssembly}
                  onJumpSource={jumpToSource}
                />
              ) : (
                <div className="empty-state compact">No detail loaded.</div>
              )}
            </div>
          )}

          {tabMounted('source') && (
            <div
              id="variant-panel-source"
              role="tabpanel"
              aria-labelledby="variant-tab-source"
              className="panel"
              hidden={tab !== 'source'}
            >
              <SourceView
                sourcePath={summary.source.path}
                highlightLine={highlightLine}
                search={sourceSearch}
                onSearchChange={setSourceSearch}
                matchPosition={sourceMatch}
                onSelectMatch={setSourceMatch}
                onSelectLine={jumpToSource}
                instructions={detail?.instructions ?? []}
                liveness={detail?.liveness ?? []}
                laneClass={laneClass}
                selectedLane={focusRegisterKey}
                onSelectLaneClass={selectLaneClass}
                cbufferReads={detail?.cbufferReads ?? []}
                backend={summary.backend}
                onInspectInstruction={jumpToAssembly}
                onInspectCbuffer={jumpToCbuffer}
                onInspectRegisters={jumpToRegisters}
                onInspectLane={jumpToRegisterLane}
              />
            </div>
          )}
        </>
      )}
    </section>
  );
}
