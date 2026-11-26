import { useEffect, useMemo, useState } from 'react';
import type { ChangeEvent, KeyboardEvent } from 'react';
import AssemblyList from '../components/AssemblyList';
import CBufferList from '../components/CBufferList';
import RegisterTimeline from '../components/RegisterTimeline';
import SourceView from '../components/SourceView';
import {
  clampMatchPosition,
  matchingInstructionIndices,
} from '../utils/assemblySearch';
import {
  backendLabel,
  formatBytes,
  formatDate,
  formatDuration,
  formatMetric,
  shadercVersion,
  stageLabel,
  statusClass,
} from '../utils/format';
import { useUpload } from '../upload';

type UploadTab = 'assembly' | 'registers' | 'cbuffer' | 'source';

const TABS: UploadTab[] = ['assembly', 'registers', 'cbuffer', 'source'];

export default function UploadPage() {
  const {
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
  } = useUpload();
  const [tab, setTab] = useState<UploadTab>('assembly');
  const [focusInstruction, setFocusInstruction] = useState<number | null>(null);
  const [focusRegister, setFocusRegister] = useState<string | null>(null);
  const [focusClass, setFocusClass] = useState<string | null>(null);
  const [registerSort, setRegisterSort] = useState<'lifetime' | 'index'>('lifetime');
  const [registerPage, setRegisterPage] = useState(0);
  const [laneClass, setLaneClass] = useState<string | null>(null);
  const [focusCbufferLine, setFocusCbufferLine] = useState<number | null>(null);
  const [sourceLine, setSourceLine] = useState<number | null>(null);
  const [assemblyQuery, setAssemblyQuery] = useState('');
  const [assemblyFilter, setAssemblyFilter] = useState(false);
  const [showSourceContext, setShowSourceContext] = useState(false);
  const [assemblyMatch, setAssemblyMatch] = useState<number | null>(null);
  const [sourceSearch, setSourceSearch] = useState('');
  const [sourceMatch, setSourceMatch] = useState<number | null>(null);
  const [cbufferView, setCbufferView] = useState<'table' | 'order'>('table');
  const [mountedTabs, setMountedTabs] = useState<Set<UploadTab>>(() => new Set());

  useEffect(() => {
    setMountedTabs((current) =>
      current.has(tab) ? current : new Set<UploadTab>(current).add(tab),
    );
  }, [tab]);

  const tabMounted = (value: UploadTab) => value === tab || mountedTabs.has(value);

  function handleTabKeyDown(event: KeyboardEvent<HTMLDivElement>) {
    if (event.altKey || event.ctrlKey || event.metaKey) {
      return;
    }
    const current = TABS.indexOf(tab);
    if (current < 0) {
      return;
    }
    let next: UploadTab | null = null;
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
    setTab(next);
    window.requestAnimationFrame(() => {
      document.getElementById(`upload-tab-${next}`)?.focus();
    });
  }
  const assemblyMatches = useMemo(
    () => matchingInstructionIndices(detail?.instructions ?? [], assemblyQuery),
    [detail, assemblyQuery],
  );
  const activeAssemblyMatch = clampMatchPosition(
    assemblyMatch ?? 0,
    assemblyMatches.length,
  );

  useEffect(() => {
    setTab('assembly');
    setFocusInstruction(null);
    setFocusRegister(null);
    setFocusClass(null);
    setRegisterSort('lifetime');
    setRegisterPage(0);
    setLaneClass(null);
    setFocusCbufferLine(null);
    setSourceLine(null);
    setAssemblyQuery('');
    setAssemblyFilter(false);
    setShowSourceContext(false);
    setAssemblyMatch(null);
    setSourceSearch('');
    setSourceMatch(null);
    setCbufferView('table');
  }, [detail, summary]);

  function updateAssemblySearch(value: string) {
    setAssemblyQuery(value);
    setAssemblyMatch(null);
  }

  function updateAssemblyFilter(value: boolean) {
    setAssemblyFilter(value);
    setAssemblyMatch(null);
  }

  function updateSourceSearch(value: string) {
    setSourceSearch(value);
    setSourceMatch(null);
  }

  function stepAssemblyMatch(direction: -1 | 1) {
    if (assemblyMatches.length === 0) {
      return;
    }
    const current = activeAssemblyMatch ?? 0;
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

  function jumpToAssembly(index: number) {
    setFocusInstruction(index);
    setTab('assembly');
  }

  function jumpToRegisters(index: number) {
    setFocusInstruction(index);
    setFocusRegister(null);
    setTab('registers');
  }

  function jumpToRegister(key: string, index: number) {
    const separator = key.indexOf(':');
    setFocusInstruction(index);
    setFocusRegister(key);
    setFocusClass(separator > 0 ? key.slice(0, separator) : null);
    setTab('registers');
  }

  function jumpToSource(line: number) {
    setSourceLine(line);
    setTab('source');
  }

  function jumpToCbuffer(line: number) {
    setFocusCbufferLine(line);
    setTab('cbuffer');
  }

  function chooseFile(event: ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0];
    event.target.value = '';
    if (file) {
      void loadFile(file);
    }
  }

  function chooseSourceFile(event: ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0];
    event.target.value = '';
    if (file) {
      void loadSource(file);
    }
  }

  if (!detail && !summary) {
    return (
      <section className="panel upload-panel">
        <p className="eyebrow">Offline report JSON</p>
        <h1>Open a report JSON</h1>
        <p className="muted">
          Drag a variant detail or report summary JSON onto the window, or choose a
          file. The report is parsed in this browser and never sent to the server.
          After loading a detail, drop or choose its matching shader source file to
          view Source and source tooltips offline.
        </p>
        <div className="upload-dropzone">
          <strong>Drop detail or summary JSON here</strong>
          <span>
            Detail JSON needs instructions/liveness; summary JSON needs source and
            variants.
          </span>
          <label className="button upload-file-button">
            Choose JSON
            <input
              type="file"
              accept=".json,application/json"
              onChange={chooseFile}
            />
          </label>
        </div>
        {loading && <p className="muted">Reading report…</p>}
        {error && (
          <div className="panel error-panel">
            <strong>Upload failed</strong>
            <p>{error}</p>
          </div>
        )}
        <p className="muted upload-note">
          Detail files enable all variant views; summary files show the variant table
          and metrics. Add a source file named like <code>forward.fs.sc</code> after
          loading a detail for offline Source and previews. Index JSON is not
          supported.
        </p>
      </section>
    );
  }

  if (summary) {
    const failed = summary.variants.filter(
      (variant) => variant.status === 'failed',
    ).length;
    return (
      <section className="upload-page">
        <div className="page-heading">
          <div>
            <p className="eyebrow">
              Uploaded summary · {backendLabel(summary.backend)} ·{' '}
              {stageLabel(summary.stage)}
              {summary.profile ? ` · ${summary.profile}` : ''}
              {summary.optimization != null ? ` · -O${summary.optimization}` : ''}
            </p>
            <h1 className="program-title">{summary.source.path}</h1>
            <p className="muted mono">{fileName}</p>
          </div>
          <div className="heading-actions">
            <label className="button ghost upload-file-button">
              Replace JSON
              <input
                type="file"
                accept=".json,application/json"
                onChange={chooseFile}
              />
            </label>
            <button className="button ghost" type="button" onClick={clear}>
              Clear upload
            </button>
          </div>
        </div>

        <div className="variant-meta">
          <span>{formatMetric(summary.variants.length)} variants</span>
          <span>{formatMetric(summary.variants.length - failed)} ok</span>
          {failed > 0 && <span>{formatMetric(failed)} failed</span>}
          <span>{formatDate(summary.generatedAt)}</span>
          <span title={summary.shaderc.version}>
            shaderc {shadercVersion(summary.shaderc.version)}
          </span>
          <span>summary only</span>
          {loading && <span>reading report…</span>}
        </div>

        {error && (
          <div className="panel error-panel">
            <strong>Upload failed</strong>
            <p>{error}</p>
          </div>
        )}

        <div className="panel">
          <div className="section-heading">
            <div>
              <h2>Variants</h2>
              <p className="muted">
                Summary metrics only. Drop a variant detail JSON for Assembly,
                Registers, CBuffer and Source.
              </p>
            </div>
          </div>
          <div className="table-panel">
            <table className="data-table">
              <thead>
                <tr>
                  <th>Variant</th>
                  <th>Status</th>
                  <th>Defines</th>
                  <th className="numeric">Instructions</th>
                  <th className="numeric">Peak</th>
                  <th className="numeric">Binary</th>
                  <th className="numeric">Compile</th>
                </tr>
              </thead>
              <tbody>
                {summary.variants.map((variant) => {
                  const defines = variant.canonicalDefines.length
                    ? variant.canonicalDefines.join(' ')
                    : 'base';
                  const peak =
                    summary.backend === 'dxbc'
                      ? variant.metrics?.tempRegisterPeak
                      : variant.metrics?.maxLiveSsaValues;
                  return (
                    <tr key={variant.variantId}>
                      <td>
                        <div>{variant.name || variant.variantId}</div>
                        <div className="muted mono">{variant.variantId}</div>
                      </td>
                      <td>
                        <span className={statusClass(variant.status)}>
                          {variant.status}
                        </span>
                      </td>
                      <td className="mono" title={defines}>
                        {defines}
                      </td>
                      <td className="numeric">
                        {formatMetric(variant.metrics?.instructionCount)}
                      </td>
                      <td className="numeric">{formatMetric(peak)}</td>
                      <td className="numeric">{formatBytes(variant.binaryBytes)}</td>
                      <td className="numeric">{formatDuration(variant.compileMs)}</td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        </div>

        <p className="muted upload-note">
          Uploaded summaries cannot open a variant detail unless the detail JSON is
          dropped as well. Data stays in this browser and is cleared on refresh.
        </p>
      </section>
    );
  }

  if (!detail) {
    return null;
  }

  return (
    <section className="upload-page">
      <div className="page-heading">
        <div>
          <p className="eyebrow">
            Uploaded detail · {backendLabel(detail.backend)} · {stageLabel(detail.stage)}
          </p>
          <h1 className="program-title">{detail.variantId}</h1>
          <p className="muted mono">
            {fileName} · {detail.source}
          </p>
        </div>
        <div className="heading-actions">
          <label className="button ghost upload-file-button">
            Replace JSON
            <input
              type="file"
              accept=".json,application/json"
              onChange={chooseFile}
            />
          </label>
          <label
            className="button ghost upload-file-button"
            title="Source file must match the detail source file name"
          >
            {sourceText ? 'Replace source' : 'Load source'}
            <input
              type="file"
              accept=".sc,.hlsl,.h,.glsl,.vert,.frag,.comp,.txt"
              onChange={chooseSourceFile}
            />
          </label>
          <button className="button ghost" type="button" onClick={clear}>
            Clear upload
          </button>
        </div>
      </div>

      <div className="variant-meta">
        <span>{formatMetric(detail.instructions.length)} instructions</span>
        <span>{formatMetric(detail.liveness.length)} live intervals</span>
        <span>{formatMetric(detail.cbufferReads.length)} cbuffer reads</span>
        <span>{sourceName ? `local source: ${sourceName}` : 'source via server'}</span>
        <span>local only</span>
        {loading && <span>reading report…</span>}
      </div>

      {error && (
        <div className="panel error-panel">
          <strong>Upload failed</strong>
          <p>{error}</p>
        </div>
      )}

      <div
        className="tabs"
        role="tablist"
        aria-label="Uploaded variant views"
        onKeyDown={handleTabKeyDown}
      >
        {TABS.map((value) => (
          <button
            key={value}
            id={`upload-tab-${value}`}
            type="button"
            role="tab"
            aria-selected={tab === value}
            aria-controls={`upload-panel-${value}`}
            tabIndex={tab === value ? 0 : -1}
            className={tab === value ? 'tab active' : 'tab'}
            onClick={() => setTab(value)}
          >
            {value === 'cbuffer'
              ? 'CBuffer'
              : value.charAt(0).toUpperCase() + value.slice(1)}
          </button>
        ))}
      </div>

      {tabMounted('assembly') && (
        <div
          id="upload-panel-assembly"
          role="tabpanel"
          aria-labelledby="upload-tab-assembly"
          className="panel assembly-panel"
          hidden={tab !== 'assembly'}
        >
          <div className="assembly-heading">
            <div>
              <h2>Assembly</h2>
              <p className="muted">
                {formatMetric(detail.instructions.length)} instructions ·{' '}
                {formatMetric(detail.liveness.length)} live intervals
              </p>
            </div>
            <div className="assembly-controls">
              <button
                className="button ghost small-button"
                type="button"
                disabled={focusInstruction == null}
                onClick={() => setTab('registers')}
              >
                Inspect registers
              </button>
              <label className="checkbox-field">
                <input
                  type="checkbox"
                  checked={assemblyFilter}
                  onChange={(event) => updateAssemblyFilter(event.target.checked)}
                />
                <span>Filter matches</span>
              </label>
              <label className="checkbox-field">
                <input
                  type="checkbox"
                  checked={showSourceContext}
                  onChange={(event) => setShowSourceContext(event.target.checked)}
                />
                <span>Source context</span>
              </label>
              <label className="field grow">
                <span>Search instructions</span>
                <input
                  type="search"
                  value={assemblyQuery}
                  placeholder="opcode, register, operand…"
                  onChange={(event) => updateAssemblySearch(event.target.value)}
                  onKeyDown={handleAssemblySearchKeyDown}
                />
              </label>
            </div>
          </div>
          <AssemblyList
            instructions={detail.instructions}
            liveness={detail.liveness}
            laneClass={laneClass}
            onSelectLaneClass={setLaneClass}
            selectedLane={focusRegister}
            query={assemblyQuery}
            backend={detail.backend}
            filterMatches={assemblyFilter}
            focusIndex={focusInstruction}
            matchPosition={activeAssemblyMatch}
            onSelectMatch={setAssemblyMatch}
            sourcePath={detail.source}
            sourceOverride={sourceText}
            showSourceContext={showSourceContext}
            onSourceLine={jumpToSource}
            onSelectInstruction={setFocusInstruction}
            onInspectRegister={jumpToRegister}
          />
        </div>
      )}

      {tabMounted('registers') && (
        <div
          id="upload-panel-registers"
          role="tabpanel"
          aria-labelledby="upload-tab-registers"
          className="panel"
          hidden={tab !== 'registers'}
        >
          <div className="section-heading">
            <div>
              <h2>Registers</h2>
              <p className="muted">
                Canvas lifetimes and pressure are linear-scan estimates, not GPU
                occupancy.
              </p>
            </div>
          </div>
          <RegisterTimeline
            detail={detail}
            backend={detail.backend}
            focusInstruction={focusInstruction}
            focusRegister={focusRegister}
            focusClass={focusClass}
            sortMode={registerSort}
            page={registerPage}
            sourcePath={detail.source}
            sourceOverride={sourceText}
            onSelectRegister={(key) => {
              setFocusRegister(key);
              if (key) {
                const separator = key.indexOf(':');
                setFocusClass(separator > 0 ? key.slice(0, separator) : null);
              }
            }}
            onSelectSort={setRegisterSort}
            onSelectPage={setRegisterPage}
            onSelectClass={(next) => {
              setFocusClass(next);
              setFocusRegister(null);
            }}
            onFocusInstruction={jumpToAssembly}
            onClearFocus={() => setFocusInstruction(null)}
            onSourceLine={jumpToSource}
          />
        </div>
      )}

      {tabMounted('cbuffer') && (
        <div
          id="upload-panel-cbuffer"
          role="tabpanel"
          aria-labelledby="upload-tab-cbuffer"
          className="panel"
          hidden={tab !== 'cbuffer'}
        >
          <div className="section-heading">
            <div>
              <h2>CBuffer read order</h2>
              <p className="muted">
                Offset order is checked per slot. Source-mapped reads link to their
                source line.
              </p>
            </div>
          </div>
          <CBufferList
            reads={detail.cbufferReads}
            instructions={detail.instructions}
            focusSourceLine={focusCbufferLine}
            view={cbufferView}
            onViewChange={setCbufferView}
            onClearFocus={() => setFocusCbufferLine(null)}
            onJumpInstruction={jumpToAssembly}
            onJumpSource={jumpToSource}
          />
        </div>
      )}

      {tabMounted('source') && (
        <div
          id="upload-panel-source"
          role="tabpanel"
          aria-labelledby="upload-tab-source"
          className="panel"
          hidden={tab !== 'source'}
        >
          <SourceView
            sourcePath={detail.source}
            sourceOverride={sourceText}
            highlightLine={sourceLine}
            search={sourceSearch}
            onSearchChange={updateSourceSearch}
            matchPosition={sourceMatch}
            onSelectMatch={setSourceMatch}
            onSelectLine={setSourceLine}
            instructions={detail.instructions}
            liveness={detail.liveness}
            laneClass={laneClass}
            selectedLane={focusRegister}
            onSelectLaneClass={setLaneClass}
            cbufferReads={detail.cbufferReads}
            backend={detail.backend}
            onInspectInstruction={jumpToAssembly}
            onInspectCbuffer={jumpToCbuffer}
            onInspectRegisters={jumpToRegisters}
            onInspectLane={jumpToRegister}
          />
        </div>
      )}
    </section>
  );
}
