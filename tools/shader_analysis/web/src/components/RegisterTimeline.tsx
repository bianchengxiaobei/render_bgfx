import { useEffect, useMemo, useRef, useState } from 'react';
import type { MouseEvent } from 'react';
import type { DetailReport, Instruction, LivenessInterval } from '../types';
import { loadSourceText } from '../api';
import { formatMetric } from '../utils/format';
import { dependencyKeysFor, registerKey } from '../utils/dependencies';

const LANE_HEIGHT = 18;
const PRESSURE_HEIGHT = 54;
const LABEL_WIDTH = 174;
const MAX_LANES = 72;
type LaneSort = 'lifetime' | 'index';

interface Lane {
  index: number;
  start: number;
  end: number;
  intervals: LivenessInterval[];
  reads: number[];
  writes: number[];
}

interface SelectedRegister {
  className: string;
  index: number;
}

function laneLabel(className: string, index: number): string {
  if (className === 'ssa') {
    return `%${index}`;
  }
  if (className === 'resource') {
    return `res${index}`;
  }
  return `${className}${index}`;
}

function spanColor(span: number, maxSpan: number, alpha: number): string {
  const ratio = maxSpan > 0 ? Math.min(1, span / maxSpan) : 0;
  const hue = 210 + ratio * 115;
  return `hsla(${hue}, 78%, 62%, ${alpha})`;
}

function laneAliveAt(lane: Lane, instruction: number): boolean {
  return lane.intervals.some(
    (interval) => instruction >= interval.start && instruction <= interval.end,
  );
}

function buildUsage(
  instructions: Instruction[],
  className: string,
): Map<number, { reads: number[]; writes: number[] }> {
  const usage = new Map<number, { reads: number[]; writes: number[] }>();
  for (const instruction of instructions) {
    for (const read of instruction.reads) {
      if (read.class !== className) {
        continue;
      }
      const entry = usage.get(read.index) ?? { reads: [], writes: [] };
      entry.reads.push(instruction.index);
      usage.set(read.index, entry);
    }
    for (const write of instruction.writes) {
      if (write.class !== className) {
        continue;
      }
      const entry = usage.get(write.index) ?? { reads: [], writes: [] };
      entry.writes.push(instruction.index);
      usage.set(write.index, entry);
    }
  }
  return usage;
}


export default function RegisterTimeline({
  detail,
  backend,
  focusInstruction = null,
  focusRegister = null,
  focusClass = null,
  sortMode = 'lifetime',
  page = 0,
  sourcePath = '',
  sourceOverride = null,
  onSelectRegister,
  onSelectClass,
  onSelectSort,
  onSelectPage,
  onFocusInstruction,
  onClearFocus,
  onSourceLine,
}: {
  detail: DetailReport;
  backend: string;
  focusInstruction?: number | null;
  focusRegister?: string | null;
  focusClass?: string | null;
  sortMode?: LaneSort;
  onSelectSort?: (mode: LaneSort) => void;
  page?: number;
  onSelectPage?: (page: number) => void;
  sourcePath?: string;
  sourceOverride?: string | null;
  onSelectRegister?: (registerKey: string | null) => void;
  onSelectClass?: (className: string) => void;
  onFocusInstruction?: (index: number) => void;
  onClearFocus?: () => void;
  onSourceLine?: (line: number) => void;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [width, setWidth] = useState(0);
  const [className, setClassName] = useState('');
  const [search, setSearch] = useState('');
  const [selected, setSelected] = useState<SelectedRegister | null>(null);
  const [hover, setHover] = useState('');
  const [hoverInstruction, setHoverInstruction] = useState<number | null>(null);
  const [sourceLines, setSourceLines] = useState<string[]>([]);
  const [sourceLoaded, setSourceLoaded] = useState(false);
  const focusAppliedRef = useRef<string | null>(null);

  const classes = useMemo(
    () => [...new Set(detail.liveness.map((interval) => interval.class))].sort(),
    [detail.liveness],
  );

  useEffect(() => {
    if (classes.length === 0) {
      setClassName('');
      return;
    }
    if (classes.includes(className)) {
      return;
    }
    const preferred = backend === 'spirv' ? 'ssa' : 'r';
    setClassName(classes.includes(preferred) ? preferred : classes[0]);
    setSelected(null);
  }, [backend, classes, className]);

  useEffect(() => {
    const separator = focusRegister?.indexOf(':') ?? -1;
    const registerClass =
      separator > 0 ? focusRegister?.slice(0, separator) : null;
    const requested =
      registerClass && classes.includes(registerClass)
        ? registerClass
        : focusClass && classes.includes(focusClass)
          ? focusClass
          : null;
    if (!requested || requested === className) {
      return;
    }
    setClassName(requested);
    setSelected(null);
  }, [className, classes, focusClass, focusRegister]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) {
      return;
    }
    const observer = new ResizeObserver((entries) => {
      setWidth(entries[0]?.contentRect.width ?? 0);
    });
    observer.observe(canvas);
    return () => observer.disconnect();
  }, []);

  useEffect(() => {
    if (sourceOverride != null) {
      setSourceLines(sourceOverride.split(/\r?\n/));
      setSourceLoaded(true);
      return;
    }
    if (!sourcePath) {
      setSourceLines([]);
      setSourceLoaded(false);
      return;
    }
    const controller = new AbortController();
    setSourceLoaded(false);
    loadSourceText(sourcePath, controller.signal)
      .then((text) => {
        setSourceLines(text.split(/\r?\n/));
        setSourceLoaded(true);
      })
      .catch(() => {
        if (!controller.signal.aborted) {
          setSourceLines([]);
          setSourceLoaded(true);
        }
      });
    return () => controller.abort();
  }, [sourceOverride, sourcePath]);

  const sourceFileName = sourcePath.split('/').pop() || sourcePath;
  function sourcePreview(line: number): string {
    if (!sourceLoaded) {
      return `source L${line}`;
    }
    const text = sourceLines[line - 1];
    if (text == null) {
      return `source L${line} (outside ${sourceFileName})`;
    }
    return `source ${sourceFileName}:${line} — ${text.trim() || '(blank line)'}`;
  }

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setSelected(null);
        onSelectRegister?.(null);
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [onSelectRegister]);

  useEffect(() => {
    focusAppliedRef.current = null;
  }, [detail.variantId]);

  useEffect(() => {
    setSelected(null);
  }, [className, detail.variantId]);

  useEffect(() => {
    if (!focusRegister) {
      focusAppliedRef.current = null;
      return;
    }
    if (focusAppliedRef.current === focusRegister) {
      return;
    }
    const separator = focusRegister.indexOf(':');
    if (separator <= 0) {
      return;
    }
    const requestedClass = focusRegister.slice(0, separator);
    const requestedIndex = Number.parseInt(focusRegister.slice(separator + 1), 10);
    if (
      !classes.includes(requestedClass) ||
      !Number.isInteger(requestedIndex) ||
      requestedIndex < 0
    ) {
      return;
    }
    if (className !== requestedClass) {
      setClassName(requestedClass);
      return;
    }
    setSelected({ className: requestedClass, index: requestedIndex });
    focusAppliedRef.current = focusRegister;
  }, [className, classes, focusRegister]);

  const usage = useMemo(
    () => buildUsage(detail.instructions, className),
    [detail.instructions, className],
  );
  const instructionByIndex = useMemo(
    () => new Map(detail.instructions.map((instruction) => [instruction.index, instruction])),
    [detail.instructions],
  );

  const allLanes = useMemo<Lane[]>(() => {
    const byIndex = new Map<number, LivenessInterval[]>();
    for (const interval of detail.liveness) {
      if (interval.class !== className) {
        continue;
      }
      const intervals = byIndex.get(interval.index) ?? [];
      intervals.push(interval);
      byIndex.set(interval.index, intervals);
    }
    const filter = search.trim().toLowerCase();
    const lanes: Lane[] = [];
    for (const [index, intervals] of byIndex) {
      const label = laneLabel(className, index);
      if (filter && !label.toLowerCase().includes(filter)) {
        continue;
      }
      let start = Number.POSITIVE_INFINITY;
      let end = Number.NEGATIVE_INFINITY;
      for (const interval of intervals) {
        start = Math.min(start, interval.start);
        end = Math.max(end, interval.end);
      }
      const entry = usage.get(index);
      lanes.push({
        index,
        start,
        end,
        intervals,
        reads: entry?.reads ?? [],
        writes: entry?.writes ?? [],
      });
    }
    lanes.sort((left, right) =>
      sortMode === 'index'
        ? left.index - right.index
        : right.end - right.start - (left.end - left.start) ||
          left.index - right.index,
    );
    return lanes;
  }, [className, detail.liveness, search, sortMode, usage]);

  const pageCount = Math.max(1, Math.ceil(allLanes.length / MAX_LANES));
  const requestedPage = Number.isFinite(page) ? Math.floor(page) : 0;
  const safePage = Math.min(Math.max(0, requestedPage), pageCount - 1);
  const pageStart = safePage * MAX_LANES;
  const lanes = useMemo(() => {
    const pageLanes = allLanes.slice(pageStart, pageStart + MAX_LANES);
    if (
      selected &&
      selected.className === className &&
      !pageLanes.some((lane) => lane.index === selected.index)
    ) {
      const pinned = allLanes.find((lane) => lane.index === selected.index);
      if (pinned) {
        return [pinned, ...pageLanes];
      }
    }
    return pageLanes;
  }, [allLanes, className, pageStart, selected]);

  useEffect(() => {
    if (Number.isFinite(page) && page !== safePage) {
      onSelectPage?.(safePage);
    }
  }, [onSelectPage, page, safePage]);
  const maxIndex = useMemo(() => {
    let result = 1;
    for (const instruction of detail.instructions) {
      result = Math.max(result, instruction.index);
    }
    for (const interval of detail.liveness) {
      result = Math.max(result, interval.end);
    }
    return result;
  }, [detail.instructions, detail.liveness]);

  const dependencyKeys = useMemo(
    () =>
      dependencyKeysFor(
        selected ? registerKey(selected.className, selected.index) : null,
        detail.instructions,
      ),
    [selected, detail.instructions],
  );

  const maxSpan = useMemo(
    () => allLanes.reduce((maximum, lane) => Math.max(maximum, lane.end - lane.start), 1),
    [allLanes],
  );
  const selectedLane = useMemo(
    () =>
      selected
        ? (lanes.find((lane) => lane.index === selected.index) ?? null)
        : null,
    [lanes, selected],
  );
  const selectedWrite = useMemo(() => {
    if (!selectedLane || selectedLane.writes.length === 0) {
      return null;
    }
    return instructionByIndex.get(selectedLane.writes[0]) ?? null;
  }, [instructionByIndex, selectedLane]);
  const pressureLabel = backend === 'dxbc' ? 'temp pressure' : 'SSA pressure';
  const maxPressure = useMemo(
    () =>
      detail.instructions.reduce((maximum, instruction) => {
        const pressure =
          backend === 'dxbc' ? instruction.pressure.temp : instruction.pressure.ssa;
        return Math.max(maximum, pressure ?? 0);
      }, 0),
    [backend, detail.instructions],
  );

  const focusedInstruction =
    focusInstruction != null && focusInstruction >= 0
      ? Math.min(maxIndex, Math.floor(focusInstruction))
      : null;
  const focusedLiveCount = useMemo(() => {
    if (focusedInstruction == null) {
      return 0;
    }
    const alive = new Set<number>();
    for (const interval of detail.liveness) {
      if (
        interval.class === className &&
        focusedInstruction >= interval.start &&
        focusedInstruction <= interval.end
      ) {
        alive.add(interval.index);
      }
    }
    return alive.size;
  }, [className, detail.liveness, focusedInstruction]);
  const focusedUsage = useMemo(() => {
    if (focusedInstruction == null) {
      return null;
    }
    const instruction = instructionByIndex.get(focusedInstruction);
    if (!instruction) {
      return { reads: 0, writes: 0 };
    }
    return {
      reads: instruction.reads.filter((read) => read.class === className).length,
      writes: instruction.writes.filter((write) => write.class === className).length,
    };
  }, [className, focusedInstruction, instructionByIndex]);
  const focusedInstructionData =
    focusedInstruction != null
      ? (instructionByIndex.get(focusedInstruction) ?? null)
      : null;

  const canvasHeight = PRESSURE_HEIGHT + Math.max(1, lanes.length) * LANE_HEIGHT + 12;

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || width === 0) {
      return;
    }
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.max(1, Math.floor(width * ratio));
    canvas.height = Math.max(1, Math.floor(canvasHeight * ratio));
    canvas.style.height = `${canvasHeight}px`;
    const context = canvas.getContext('2d');
    if (!context) {
      return;
    }
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    context.clearRect(0, 0, width, canvasHeight);

    const plotLeft = LABEL_WIDTH;
    const plotWidth = Math.max(20, width - plotLeft - 14);
    const xFor = (index: number) => plotLeft + (index / maxIndex) * plotWidth;
    const pressureTop = 10;

    context.fillStyle = 'rgba(76, 194, 255, 0.07)';
    context.fillRect(plotLeft, pressureTop, plotWidth, PRESSURE_HEIGHT - 22);

    const pressureBuckets = new Map<number, number>();
    for (const instruction of detail.instructions) {
      const pressure = backend === 'dxbc' ? instruction.pressure.temp : instruction.pressure.ssa;
      if (pressure == null) {
        continue;
      }
      const bucket = Math.max(
        0,
        Math.min(plotWidth - 1, Math.floor(((instruction.index / maxIndex) * plotWidth))),
      );
      pressureBuckets.set(bucket, Math.max(pressureBuckets.get(bucket) ?? 0, pressure));
    }
    for (const [bucket, pressure] of pressureBuckets) {
      const normalized = maxPressure > 0 ? pressure / maxPressure : 0;
      context.fillStyle = `hsla(${175 - normalized * 150}, 76%, 60%, 0.75)`;
      const barHeight = Math.max(1, normalized * (PRESSURE_HEIGHT - 28));
      context.fillRect(
        plotLeft + bucket,
        PRESSURE_HEIGHT - 12 - barHeight,
        1,
        barHeight,
      );
    }
    context.fillStyle = '#8ea3b8';
    context.font = '11px Cascadia Code, Consolas, monospace';
    context.fillText(`${pressureLabel} · max ${maxPressure}`, plotLeft, 11);

    context.strokeStyle = 'rgba(142, 163, 184, 0.14)';
    context.lineWidth = 1;
    for (let step = 0; step <= 8; step++) {
      const x = xFor((maxIndex * step) / 8);
      context.beginPath();
      context.moveTo(x, PRESSURE_HEIGHT);
      context.lineTo(x, canvasHeight);
      context.stroke();
      context.fillStyle = '#63798d';
      context.fillText(formatMetric(Math.round((maxIndex * step) / 8)), x + 2, canvasHeight - 2);
    }

    lanes.forEach((lane, row) => {
      const y = PRESSURE_HEIGHT + row * LANE_HEIGHT;
      const key = registerKey(className, lane.index);
      const highlighted = dependencyKeys.size > 0 && dependencyKeys.has(key);
      const dimmed = dependencyKeys.size > 0 && !highlighted;
      const focusedAlive =
        focusedInstruction != null && laneAliveAt(lane, focusedInstruction);
      const focusDimmed = focusedInstruction != null && !focusedAlive;
      const laneAlpha = dimmed ? 0.16 : focusDimmed ? 0.45 : 1;
      context.fillStyle = focusedAlive
        ? 'rgba(255, 191, 105, 0.11)'
        : selected?.index === lane.index
          ? 'rgba(76, 194, 255, 0.12)'
          : 'transparent';
      context.fillRect(0, y, width, LANE_HEIGHT);
      context.fillStyle = dimmed || focusDimmed
        ? '#506475'
        : focusedAlive
          ? '#ffcc86'
          : '#b7cde2';
      context.font = '11px Cascadia Code, Consolas, monospace';
      context.fillText(
        `${laneLabel(className, lane.index)}  ${lane.intervals.length}×`,
        8,
        y + 12,
      );

      for (const interval of lane.intervals) {
        const x0 = xFor(interval.start);
        const x1 = Math.max(x0 + 2, xFor(interval.end + 1));
        context.fillStyle = selected?.index === lane.index
          ? 'rgba(255, 142, 229, 0.9)'
          : spanColor(interval.end - interval.start, maxSpan, 0.72 * laneAlpha);
        context.fillRect(x0, y + 3, x1 - x0, LANE_HEIGHT - 6);
      }

      for (const read of lane.reads) {
        context.fillStyle = `rgba(76, 194, 255, ${0.95 * laneAlpha})`;
        context.fillRect(xFor(read), y + 2, 1.5, 4);
      }
      for (const write of lane.writes) {
        context.fillStyle = `rgba(255, 123, 114, ${0.95 * laneAlpha})`;
        context.fillRect(xFor(write), y + LANE_HEIGHT - 7, 1.5, 4);
      }
    });

    if (focusedInstruction != null) {
      const x = xFor(focusedInstruction);
      context.save();
      context.strokeStyle = 'rgba(255, 191, 105, 0.85)';
      context.lineWidth = 1;
      context.setLineDash([4, 3]);
      context.beginPath();
      context.moveTo(x, PRESSURE_HEIGHT - 4);
      context.lineTo(x, canvasHeight - 18);
      context.stroke();
      context.restore();
      context.fillStyle = '#ffbf69';
      context.font = '10px Cascadia Code, Consolas, monospace';
      const label = `#${focusedInstruction}`;
      const labelX = Math.max(plotLeft + 2, Math.min(x - 12, width - 48));
      context.fillText(label, labelX, PRESSURE_HEIGHT + 10);
    }
  }, [
    backend,
    canvasHeight,
    className,
    dependencyKeys,
    detail.instructions,
    focusedInstruction,
    lanes,
    maxIndex,
    maxPressure,
    maxSpan,
    pressureLabel,
    selected,
    width,
  ]);

  function hitTest(event: MouseEvent<HTMLCanvasElement>) {
    const canvas = canvasRef.current;
    if (!canvas) {
      return null;
    }
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    if (x < LABEL_WIDTH || y < PRESSURE_HEIGHT) {
      return null;
    }
    const row = Math.floor((y - PRESSURE_HEIGHT) / LANE_HEIGHT);
    const lane = lanes[row];
    if (!lane) {
      return null;
    }
    const plotWidth = Math.max(20, width - LABEL_WIDTH - 14);
    const instruction = Math.round(((x - LABEL_WIDTH) / plotWidth) * maxIndex);
    return { lane, instruction: Math.max(0, Math.min(maxIndex, instruction)) };
  }

  function pressureHitTest(event: MouseEvent<HTMLCanvasElement>) {
    const canvas = canvasRef.current;
    if (!canvas) {
      return null;
    }
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    if (x < LABEL_WIDTH || y < 0 || y >= PRESSURE_HEIGHT) {
      return null;
    }
    const plotWidth = Math.max(20, width - LABEL_WIDTH - 14);
    const instruction = Math.round(((x - LABEL_WIDTH) / plotWidth) * maxIndex);
    return Math.max(0, Math.min(maxIndex, instruction));
  }

  function handleMove(event: MouseEvent<HTMLCanvasElement>) {
    const pressureInstruction = pressureHitTest(event);
    if (pressureInstruction != null) {
      const instruction = instructionByIndex.get(pressureInstruction);
      const pressureValue = instruction
        ? backend === 'dxbc'
          ? instruction.pressure.temp
          : instruction.pressure.ssa
        : null;
      setHoverInstruction(pressureInstruction);
      setHover(
        `pressure ${pressureValue == null ? '—' : pressureValue} · ${
          instruction ? instruction.opcode : 'no instruction at this point'
        }`,
      );
      return;
    }
    const hit = hitTest(event);
    if (!hit) {
      setHover('');
      setHoverInstruction(null);
      return;
    }
    const { lane, instruction } = hit;
    const active = lane.intervals.some(
      (interval) => instruction >= interval.start && instruction <= interval.end,
    );
    const hovered = instructionByIndex.get(instruction);
    const pressureValue = hovered
      ? backend === 'dxbc'
        ? hovered.pressure.temp
        : hovered.pressure.ssa
      : null;
    const writeIndex =
      lane.writes.filter((index) => index <= instruction).at(-1) ?? lane.writes[0];
    const writer = writeIndex != null ? instructionByIndex.get(writeIndex) : null;
    const writerSource =
      writer?.sourceLine != null ? sourcePreview(writer.sourceLine) : null;
    setHoverInstruction(instruction);
    setHover(
      `${laneLabel(className, lane.index)} · ${active ? 'alive' : 'outside lifetime'} · ` +
        `${lane.reads.length} reads / ${lane.writes.length} writes · range ${lane.start}–${lane.end}` +
        `${writer ? ` · write ${writer.index} ${writer.opcode}` : ' · no write'}` +
        `${writerSource ? ` · ${writerSource}` : ''}` +
        `${hovered ? ` · ${hovered.opcode}` : ''} · ` +
        `${pressureValue == null ? 'pressure —' : `pressure ${pressureValue}`}`,
    );
  }

  function handleClick(event: MouseEvent<HTMLCanvasElement>) {
    const hit = hitTest(event);
    if (!hit) {
      setSelected(null);
      onSelectRegister?.(null);
      return;
    }
    setSelected({ className, index: hit.lane.index });
    onSelectRegister?.(registerKey(className, hit.lane.index));
  }

  function handleDoubleClick(event: MouseEvent<HTMLCanvasElement>) {
    const hit = hitTest(event);
    if (hit && onFocusInstruction) {
      onFocusInstruction(hit.lane.start);
    }
  }

  if (detail.liveness.length === 0) {
    return <div className="empty-state compact">No liveness intervals in this detail.</div>;
  }

  return (
    <div className="register-view">
      <div className="register-toolbar">
        <label className="field">
          <span>Register class</span>
          <select
            value={className}
            onChange={(event) => {
              const next = event.target.value;
              setClassName(next);
              setSelected(null);
              onSelectRegister?.(null);
              onSelectClass?.(next);
              if (page !== 0) {
                onSelectPage?.(0);
              }
            }}
          >
            {classes.map((value) => (
              <option key={value} value={value}>
                {value}
              </option>
            ))}
          </select>
        </label>
        <label className="field">
          <span>Sort lanes</span>
          <select
            value={sortMode}
            onChange={(event) => {
              if (page !== 0) {
                onSelectPage?.(0);
              }
              onSelectSort?.(event.target.value as LaneSort);
            }}
          >
            <option value="lifetime">Longest lifetime</option>
            <option value="index">Register index</option>
          </select>
        </label>
        <label className="field grow">
          <span>Find register</span>
          <input
            type="search"
            value={search}
            placeholder={className === 'ssa' ? '%1234' : 'r12'}
            onChange={(event) => {
              setSearch(event.target.value);
              if (page !== 0) {
                onSelectPage?.(0);
              }
            }}
          />
        </label>
        <button
          className="button ghost"
          type="button"
          onClick={() => {
            setSelected(null);
            setSearch('');
            onSelectRegister?.(null);
          }}
        >
          Clear
        </button>
        {pageCount > 1 && (
          <div className="register-page-controls">
            <button
              className="button ghost small-button"
              type="button"
              disabled={safePage === 0}
              onClick={() => onSelectPage?.(safePage - 1)}
            >
              Prev
            </button>
            <span className="muted">
              {safePage + 1}/{pageCount}
            </span>
            <button
              className="button ghost small-button"
              type="button"
              disabled={safePage >= pageCount - 1}
              onClick={() => onSelectPage?.(safePage + 1)}
            >
              Next
            </button>
          </div>
        )}
      </div>
      <div className="register-legend">
        <span className="legend-lifetime">lifetime</span>
        <span className="legend-read">read</span>
        <span className="legend-write">write</span>
        <span className="legend-pressure">{pressureLabel}</span>
        <span className="muted">
          Escape clears selection · double-click jumps to assembly · click an Assembly row
          to focus an instruction
        </span>
      </div>
      {focusedInstruction != null && (
        <div className="register-focus-note">
          <div className="register-focus-main">
            <span>
              Instruction <strong>{focusedInstruction}</strong>
              {focusedInstructionData && (
                <span className="register-focus-opcode"> · {focusedInstructionData.opcode}</span>
              )}{' '}
              · {focusedLiveCount} live {className} lane(s)
              {focusedUsage
                ? ` · ${focusedUsage.reads} read / ${focusedUsage.writes} write`
                : ''}
            </span>
            {focusedInstructionData && (
              <span className="register-focus-text" title={focusedInstructionData.text}>
                {focusedInstructionData.text}
              </span>
            )}
          </div>
          <div className="register-focus-actions">
            {focusedInstructionData?.sourceLine != null && onSourceLine && (
              <button
                className="link-button"
                type="button"
                onClick={() => onSourceLine(focusedInstructionData.sourceLine!)}
              >
                Source L{focusedInstructionData.sourceLine}
              </button>
            )}
            {onClearFocus && (
              <button className="link-button" type="button" onClick={onClearFocus}>
                Clear focus
              </button>
            )}
          </div>
        </div>
      )}
      <div className="register-hover">
        {selected
          ? `selected ${laneLabel(selected.className, selected.index)} · ${dependencyKeys.size} dependent register(s) highlighted`
          : hover || 'Click a lane to highlight its dependency chain.'}
        {hoverInstruction != null && <span className="muted"> · instruction {hoverInstruction}</span>}
      </div>
      {selectedLane && (
        <div className="register-selection">
          <div className="register-focus-main">
            <span>
              First write{' '}
              {selectedWrite ? (
                <>
                  {onFocusInstruction ? (
                    <button
                      className="link-button mono"
                      type="button"
                      onClick={() => onFocusInstruction(selectedWrite.index)}
                    >
                      instruction {selectedWrite.index}
                    </button>
                  ) : (
                    <strong>instruction {selectedWrite.index}</strong>
                  )}
                  <span className="register-focus-opcode"> · {selectedWrite.opcode}</span>
                </>
              ) : (
                <span className="muted">not present in this shader</span>
              )}
            </span>
            {selectedWrite && (
              <span className="register-focus-text" title={selectedWrite.text}>
                {selectedWrite.text}
              </span>
            )}
            {sourcePath && selectedWrite?.sourceLine != null && (
              <span
                className="register-source-text"
                title={sourcePreview(selectedWrite.sourceLine)}
              >
                {sourcePreview(selectedWrite.sourceLine)}
              </span>
            )}
          </div>
          <div className="register-focus-actions">
            {selectedWrite?.sourceLine != null && onSourceLine && (
              <button
                className="link-button"
                type="button"
                onClick={() => onSourceLine(selectedWrite.sourceLine!)}
              >
                Source L{selectedWrite.sourceLine}
              </button>
            )}
          </div>
        </div>
      )}
      <div className="register-canvas-wrap">
        <canvas
          ref={canvasRef}
          className="register-canvas"
          aria-label="Register lifetime, read, write and pressure timeline"
          onMouseMove={handleMove}
          onMouseLeave={() => {
            setHover('');
            setHoverInstruction(null);
          }}
          onClick={handleClick}
          onDoubleClick={handleDoubleClick}
        />
      </div>
      <p className="muted table-note">
        Showing {lanes.length} of {allLanes.length} {className} lane(s),{' '}
        {sortMode === 'index' ? 'register index order' : 'longest lifetimes first'}
        {pageCount > 1 ? `, page ${safePage + 1}/${pageCount}` : ''}; a selected lane
        outside the current page is pinned at the top. Lifetimes and pressure are static
        linear-scan estimates, not GPU occupancy.
      </p>
    </div>
  );
}
