import { Fragment, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import Prism from 'prismjs';
import 'prismjs/components/prism-c';
import 'prismjs/components/prism-hlsl';
import { loadSourceText, sourceUrl } from '../api';
import type { CbufferRead, Instruction, LivenessInterval } from '../types';
import { copyText, errorMessage } from '../utils/format';

interface HighlightedLine {
  number: number;
  html: string;
  text: string;
}

interface SourceJumpEdge {
  fromLine: number;
  toLine: number;
  fromIndex: number;
  toIndex: number;
  fromText: string;
  toText: string;
  label: string;
  color: string;
  indent: number;
}

interface JumpGeometry {
  edge: SourceJumpEdge;
  top: number;
  height: number;
}

interface SourceCbufferUsage {
  count: number;
  dynamic: number;
  slots: Set<number>;
  min: number | null;
  max: number | null;
  firstInstruction: number;
  reads: CbufferRead[];
}

function sourceJumpColor(fromIndex: number, toIndex: number): string {
  let hash = 42;
  for (const character of `${fromIndex}:${toIndex}`) {
    hash = (hash * 31 + character.charCodeAt(0)) | 0;
  }
  return `hsl(${Math.abs(hash) % 360} 78% 62%)`;
}

function compareLaneLabels(left: string, right: string): number {
  const leftSeparator = left.indexOf(':');
  const rightSeparator = right.indexOf(':');
  const leftClass = leftSeparator >= 0 ? left.slice(0, leftSeparator) : left;
  const rightClass = rightSeparator >= 0 ? right.slice(0, rightSeparator) : right;
  if (leftClass !== rightClass) {
    return leftClass.localeCompare(rightClass);
  }
  const leftIndex = Number.parseInt(
    leftSeparator >= 0 ? left.slice(leftSeparator + 1) : '',
    10,
  );
  const rightIndex = Number.parseInt(
    rightSeparator >= 0 ? right.slice(rightSeparator + 1) : '',
    10,
  );
  if (Number.isFinite(leftIndex) && Number.isFinite(rightIndex) && leftIndex !== rightIndex) {
    return leftIndex - rightIndex;
  }
  return left.localeCompare(right);
}

function laneColor(label: string): string {
  const [className, indexText] = label.split(':');
  let classHash = 0;
  for (const character of className) {
    classHash = (classHash * 31 + character.charCodeAt(0)) | 0;
  }
  const index = Number.parseInt(indexText ?? '', 10);
  const hue =
    ((Number.isFinite(index) ? index * 47 : 0) + Math.abs(classHash) * 13) % 360;
  return `hsl(${hue} 72% 60%)`;
}

export default function SourceView({
  sourcePath,
  sourceOverride = null,
  highlightLine,
  search,
  onSearchChange,
  matchPosition = null,
  onSelectMatch,
  onSelectLine,
  instructions = [],
  liveness = [],
  laneClass = null,
  selectedLane = null,
  cbufferReads = [],
  backend = 'dxbc',
  onInspectInstruction,
  onInspectCbuffer,
  onInspectRegisters,
  onInspectLane,
  onSelectLaneClass,
}: {
  sourcePath: string;
  sourceOverride?: string | null;
  highlightLine: number | null;
  search: string;
  onSearchChange: (value: string) => void;
  matchPosition?: number | null;
  onSelectMatch?: (position: number) => void;
  onSelectLine?: (line: number) => void;
  instructions?: Instruction[];
  liveness?: LivenessInterval[];
  laneClass?: string | null;
  selectedLane?: string | null;
  onSelectLaneClass?: (className: string | null) => void;
  cbufferReads?: CbufferRead[];
  backend?: string;
  onInspectInstruction?: (index: number) => void;
  onInspectCbuffer?: (sourceLine: number) => void;
  onInspectRegisters?: (instructionIndex: number) => void;
  onInspectLane?: (registerKey: string, instructionIndex: number) => void;
}) {
  const [source, setSource] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [copyStatus, setCopyStatus] = useState<'source' | 'line' | null>(null);
  const [hoveredJump, setHoveredJump] = useState<SourceJumpEdge | null>(null);
  const [jumpGeometry, setJumpGeometry] = useState<JumpGeometry[]>([]);
  const [expandedLine, setExpandedLine] = useState<number | null>(null);
  const [hoveredLane, setHoveredLane] = useState<string | null>(null);
  const [laneMenu, setLaneMenu] = useState<{
    line: number;
    left: number;
    top: number;
    instruction: number;
    labels: string[];
  } | null>(null);
  const lineRefs = useRef(new Map<number, HTMLDivElement>());
  const sourceScrollRef = useRef<HTMLDivElement>(null);
  const sourceLinesRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    setExpandedLine(null);
    setHoveredLane(null);
    setLaneMenu(null);
  }, [laneClass, sourcePath]);

  useEffect(() => {
    if (!laneMenu) {
      return;
    }
    const close = () => setLaneMenu(null);
    const onPointerDown = (event: PointerEvent) => {
      const target = event.target as Element | null;
      if (!target?.closest('.source-lane-menu')) {
        close();
      }
    };
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        close();
      }
    };
    const onScroll = (event: Event) => {
      const target = event.target as Element | null;
      if (!target?.closest?.('.source-lane-menu')) {
        close();
      }
    };
    document.addEventListener('pointerdown', onPointerDown, true);
    document.addEventListener('keydown', onKeyDown);
    document.addEventListener('scroll', onScroll, true);
    window.addEventListener('resize', close);
    return () => {
      document.removeEventListener('pointerdown', onPointerDown, true);
      document.removeEventListener('keydown', onKeyDown);
      document.removeEventListener('scroll', onScroll, true);
      window.removeEventListener('resize', close);
    };
  }, [laneMenu]);

  useEffect(() => {
    if (expandedLine == null) {
      return;
    }
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setExpandedLine(null);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [expandedLine]);

  useEffect(() => {
    if (sourceOverride != null) {
      setSource(sourceOverride);
      setLoading(false);
      setError(null);
      return;
    }
    const controller = new AbortController();
    setLoading(true);
    setError(null);
    loadSourceText(sourcePath, controller.signal)
      .then((text) => {
        setSource(text);
        setLoading(false);
      })
      .catch((loadError: unknown) => {
        if (!controller.signal.aborted) {
          setError(errorMessage(loadError));
          setLoading(false);
        }
      });
    return () => controller.abort();
  }, [sourceOverride, sourcePath]);

  const lines = useMemo<HighlightedLine[]>(() => {
    if (source == null) {
      return [];
    }
    const grammar = Prism.languages.hlsl;
    return source.split(/\r?\n/).map((text, index) => ({
      number: index + 1,
      text,
      html: Prism.highlight(text, grammar, 'hlsl'),
    }));
  }, [source]);

  const matchingLineNumbers = useMemo(() => {
    const needle = search.trim().toLowerCase();
    if (!needle) {
      return [];
    }
    return lines
      .filter((line) => line.text.toLowerCase().includes(needle))
      .map((line) => line.number);
  }, [lines, search]);

  const matchingLines = useMemo(
    () => new Set(matchingLineNumbers),
    [matchingLineNumbers],
  );

  const pressureKey = backend === 'spirv' ? 'ssa' : 'temp';
  const pressureLabel = pressureKey === 'ssa' ? 'SSA' : 'temp';
  const laneClasses = useMemo(
    () => [...new Set(liveness.map((interval) => interval.class))].sort(),
    [liveness],
  );
  const effectiveLaneClass =
    laneClass != null && laneClasses.includes(laneClass) ? laneClass : null;
  const visibleLiveness = useMemo(
    () =>
      effectiveLaneClass
        ? liveness.filter((interval) => interval.class === effectiveLaneClass)
        : liveness,
    [effectiveLaneClass, liveness],
  );
  const sourceMapping = useMemo(() => {
    const map = new Map<number, number[]>();
    const instructionsByLine = new Map<number, Instruction[]>();
    const pressureByLine = new Map<number, number>();
    const pressureIndexByLine = new Map<number, number>();
    const aliveByLine = new Map<number, number>();
    const aliveLabelsByLine = new Map<number, Set<string>>();
    const aliveRangesByLine = new Map<number, Map<string, Set<string>>>();
    const usageByLine = new Map<number, { reads: Set<string>; writes: Set<string> }>();
    let inRange = 0;
    let outside = 0;
    for (const instruction of instructions) {
      const line = instruction.sourceLine;
      if (line == null) {
        continue;
      }
      if (line < 1 || line > lines.length) {
        outside += 1;
        continue;
      }
      const entries = map.get(line) ?? [];
      entries.push(instruction.index);
      map.set(line, entries);
      const lineInstructions = instructionsByLine.get(line) ?? [];
      lineInstructions.push(instruction);
      instructionsByLine.set(line, lineInstructions);
      const pressure = instruction.pressure[pressureKey];
      if (pressure != null && pressure > (pressureByLine.get(line) ?? 0)) {
        pressureByLine.set(line, pressure);
        pressureIndexByLine.set(line, instruction.index);
      }
      let aliveCount = 0;
      const aliveLabels = aliveLabelsByLine.get(line) ?? new Set<string>();
      const aliveRanges = aliveRangesByLine.get(line) ?? new Map<string, Set<string>>();
      for (const interval of visibleLiveness) {
        if (instruction.index >= interval.start && instruction.index <= interval.end) {
          aliveCount += 1;
          const label = `${interval.class}:${interval.index}`;
          aliveLabels.add(label);
          const ranges = aliveRanges.get(label) ?? new Set<string>();
          ranges.add(`${interval.start}–${interval.end}`);
          aliveRanges.set(label, ranges);
        }
      }
      aliveByLine.set(line, Math.max(aliveByLine.get(line) ?? 0, aliveCount));
      aliveLabelsByLine.set(line, aliveLabels);
      aliveRangesByLine.set(line, aliveRanges);
      const usage = usageByLine.get(line) ?? { reads: new Set<string>(), writes: new Set<string>() };
      for (const read of instruction.reads) {
        usage.reads.add(`${read.class}:${read.index}`);
      }
      for (const write of instruction.writes) {
        usage.writes.add(`${write.class}:${write.index}`);
      }
      usageByLine.set(line, usage);
      inRange += 1;
    }
    const maxPressure = [...pressureByLine.values()].reduce(
      (maximum, value) => Math.max(maximum, value),
      0,
    );
    const maxAlive = [...aliveByLine.values()].reduce(
      (maximum, value) => Math.max(maximum, value),
      0,
    );
    return {
      map,
      instructionsByLine,
      pressureByLine,
      pressureIndexByLine,
      maxPressure,
      aliveByLine,
      maxAlive,
      aliveLabelsByLine,
      aliveRangesByLine,
      usageByLine,
      inRange,
      outside,
    };
  }, [instructions, lines.length, pressureKey, visibleLiveness]);

  const laneHighlightLines = useMemo(() => {
    if (hoveredLane == null) {
      return null;
    }
    const intervals = visibleLiveness.filter(
      (interval) => `${interval.class}:${interval.index}` === hoveredLane,
    );
    if (intervals.length === 0) {
      return null;
    }
    const result = new Set<number>();
    for (const [line, instructionIndexes] of sourceMapping.map) {
      if (
        instructionIndexes.some((index) =>
          intervals.some((interval) => index >= interval.start && index <= interval.end),
        )
      ) {
        result.add(line);
      }
    }
    return result;
  }, [hoveredLane, sourceMapping, visibleLiveness]);
  const selectedLaneLines = useMemo(() => {
    if (selectedLane == null) {
      return null;
    }
    const intervals = visibleLiveness.filter(
      (interval) => `${interval.class}:${interval.index}` === selectedLane,
    );
    if (intervals.length === 0) {
      return null;
    }
    const result = new Set<number>();
    for (const [line, instructionIndexes] of sourceMapping.map) {
      if (
        instructionIndexes.some((index) =>
          intervals.some((interval) => index >= interval.start && index <= interval.end),
        )
      ) {
        result.add(line);
      }
    }
    return result;
  }, [selectedLane, sourceMapping, visibleLiveness]);
  const instructionByIndex = useMemo(
    () => new Map(instructions.map((instruction) => [instruction.index, instruction])),
    [instructions],
  );
  const cbufferByLine = useMemo(() => {
    const result = new Map<number, SourceCbufferUsage>();
    for (const read of cbufferReads) {
      const instruction = instructionByIndex.get(read.instruction);
      const line = instruction?.sourceLine;
      if (line == null || line < 1 || line > lines.length) {
        continue;
      }
      const usage =
        result.get(line) ??
        ({
          count: 0,
          dynamic: 0,
          slots: new Set<number>(),
          min: null,
          max: null,
          firstInstruction: read.instruction,
          reads: [],
        } satisfies SourceCbufferUsage);
      usage.count += 1;
      usage.firstInstruction = Math.min(usage.firstInstruction, read.instruction);
      usage.slots.add(read.slot);
      usage.reads.push(read);
      if (read.dynamic) {
        usage.dynamic += 1;
      } else {
        usage.min = usage.min == null ? read.offset : Math.min(usage.min, read.offset);
        usage.max = usage.max == null ? read.offset : Math.max(usage.max, read.offset);
      }
      result.set(line, usage);
    }
    return result;
  }, [cbufferReads, instructionByIndex, lines.length]);
  const sourceJumpEdges = useMemo(() => {
    const edges: SourceJumpEdge[] = [];
    const seen = new Set<string>();
    for (const instruction of instructions) {
      const fromLine = instruction.sourceLine;
      const targetIndex = instruction.branch?.targetIndex;
      if (fromLine == null || targetIndex == null) {
        continue;
      }
      if (fromLine < 1 || fromLine > lines.length) {
        continue;
      }
      const target = instructionByIndex.get(targetIndex);
      const toLine = target?.sourceLine;
      if (toLine == null || toLine < 1 || toLine > lines.length || toLine === fromLine) {
        continue;
      }
      const key = `${instruction.index}:${targetIndex}`;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      edges.push({
        fromLine,
        toLine,
        fromIndex: instruction.index,
        toIndex: targetIndex,
        fromText: instruction.text,
        toText: target!.text,
        label: `${instruction.branch!.kind} ${instruction.index} → ${targetIndex}`,
        color: sourceJumpColor(instruction.index, targetIndex),
        indent: instruction.branch?.indent ?? 0,
      });
    }
    return edges;
  }, [instructionByIndex, instructions, lines.length]);
  const jumpRange = hoveredJump
    ? ([
        Math.min(hoveredJump.fromLine, hoveredJump.toLine),
        Math.max(hoveredJump.fromLine, hoveredJump.toLine),
      ] as const)
    : null;
  const activeMatchIndex =
    matchingLineNumbers.length > 0
      ? Math.max(
          0,
          Math.min(matchPosition ?? 0, matchingLineNumbers.length - 1),
        )
      : null;
  const activeMatchLine =
    activeMatchIndex != null ? matchingLineNumbers[activeMatchIndex] : null;

  const effectiveHighlight =
    highlightLine != null && highlightLine >= 1 && highlightLine <= lines.length
      ? highlightLine
      : null;
  const outOfRange =
    highlightLine != null && lines.length > 0 && effectiveHighlight == null;
  const sourceFileName = sourcePath.split('/').pop() || sourcePath;

  function openLaneMenu(
    element: HTMLElement,
    line: number,
    labels: string[],
    instruction: number,
  ) {
    const rect = element.getBoundingClientRect();
    const width = 230;
    const height = Math.min(320, 48 + labels.length * 27);
    setLaneMenu({
      line,
      left: Math.max(8, Math.min(rect.left, window.innerWidth - width - 8)),
      top: Math.max(8, Math.min(rect.bottom + 4, window.innerHeight - height - 8)),
      instruction,
      labels,
    });
  }

  function lineTitle(line: HighlightedLine): string {
    const mapped = sourceMapping.instructionsByLine.get(line.number) ?? [];
    const parts: string[] = [];
    if (mapped.length > 0) {
      parts.push(
        mapped
          .slice(0, 10)
          .map((instruction) => `${String(instruction.index).padStart(5, '0')} ${instruction.text}`)
          .join('\n'),
      );
      if (mapped.length > 10) {
        parts.push(`… ${mapped.length - 10} more instruction(s)`);
      }
      const jumps = mapped
        .filter((instruction) => instruction.branch?.targetIndex != null)
        .slice(0, 5)
        .map((instruction) => {
          const target = instructionByIndex.get(instruction.branch!.targetIndex!);
          const targetLine = target?.sourceLine != null ? ` (source L${target.sourceLine})` : '';
          return `${instruction.branch!.kind} ${instruction.index} → ${instruction.branch!.targetIndex}${targetLine}`;
        });
      if (jumps.length > 0) {
        parts.push(`branches:\n${jumps.join('\n')}`);
      }
    }
    parts.push(`${sourceFileName}:${line.number}`);
    parts.push(
      onSelectLine
        ? 'Click to select this line; double-click to copy path:line'
        : 'Double-click to copy path:line',
    );
    return parts.join('\n\n');
  }

  function scrollToLine(lineNumber: number, center = false) {
    const element = lineRefs.current.get(lineNumber);
    const container = sourceScrollRef.current;
    if (!element || !container) {
      return;
    }
    const elementRect = element.getBoundingClientRect();
    const containerRect = container.getBoundingClientRect();
    if (
      !center &&
      elementRect.top >= containerRect.top &&
      elementRect.bottom <= containerRect.bottom
    ) {
      return;
    }
    const top =
      elementRect.top -
      containerRect.top +
      container.scrollTop -
      container.clientHeight / 3;
    container.scrollTop = Math.max(0, top);
  }

  useLayoutEffect(() => {
    if (sourceJumpEdges.length === 0) {
      setJumpGeometry([]);
      return;
    }
    const container = sourceLinesRef.current;
    if (!container) {
      return;
    }
    const containerTop = container.getBoundingClientRect().top;
    const geometry: JumpGeometry[] = [];
    for (const edge of sourceJumpEdges) {
      const fromElement = lineRefs.current.get(edge.fromLine);
      const toElement = lineRefs.current.get(edge.toLine);
      if (!fromElement || !toElement) {
        continue;
      }
      const fromRect = fromElement.getBoundingClientRect();
      const toRect = toElement.getBoundingClientRect();
      const fromTop = fromRect.top - containerTop + fromRect.height / 2;
      const toTop = toRect.top - containerTop + toRect.height / 2;
      geometry.push({
        edge,
        top: Math.min(fromTop, toTop),
        height: Math.abs(toTop - fromTop),
      });
    }
    setJumpGeometry(geometry);
  }, [expandedLine, sourceJumpEdges, lines.length]);

  useEffect(() => {
    if (effectiveHighlight != null) {
      scrollToLine(effectiveHighlight);
    }
  }, [effectiveHighlight, lines.length]);

  useEffect(() => {
    if (activeMatchLine != null) {
      scrollToLine(activeMatchLine, true);
    }
  }, [activeMatchLine]);

  function navigateMatch(direction: -1 | 1) {
    if (matchingLineNumbers.length === 0 || !onSelectMatch) {
      return;
    }
    const current = activeMatchIndex ?? 0;
    const next =
      (current + direction + matchingLineNumbers.length) %
      matchingLineNumbers.length;
    onSelectMatch?.(next);
  }

  async function handleCopy() {
    try {
      await copyText(sourcePath);
      setCopyStatus('source');
      window.setTimeout(() => setCopyStatus(null), 1500);
    } catch {
      setError('Copy failed');
    }
  }

  async function handleCopyLine(lineNumber: number) {
    try {
      await copyText(`${sourcePath}:${lineNumber}`);
      setCopyStatus('line');
      window.setTimeout(() => setCopyStatus(null), 1500);
    } catch {
      setError('Copy failed');
    }
  }

  return (
    <div className="source-view">
      <div className="source-heading">
        <div>
          <h2>Source</h2>
          <p className="muted mono">
            {sourcePath} {loading ? '' : `· ${lines.length} lines`}
          </p>
        </div>
        <div className="source-search-box">
          {laneClasses.length > 1 && (
            <select
              className="source-lane-class-select"
              value={effectiveLaneClass ?? ''}
              aria-label="Filter Source lane rails by register class"
              title="Filter lanes, live counts, and read/write counts by register class"
              onChange={(event) =>
                onSelectLaneClass?.(event.target.value ? event.target.value : null)
              }
            >
              <option value="">All lanes</option>
              {laneClasses.map((value) => (
                <option key={value} value={value}>
                  {value}
                </option>
              ))}
            </select>
          )}
          <label className="field grow source-search">
            <span>Find in source</span>
            <input
              type="search"
              value={search}
              placeholder="identifier, keyword, line text…"
              aria-label="Find in source"
              onChange={(event) => onSearchChange(event.target.value)}
              onKeyDown={(event) => {
                if (event.key !== 'Enter' && event.key !== 'F3') {
                  return;
                }
                event.preventDefault();
                navigateMatch(event.shiftKey ? -1 : 1);
              }}
            />
          </label>
          <div className="source-search-buttons">
            <button
              className="button ghost small-button"
              type="button"
              disabled={matchingLineNumbers.length === 0 || !onSelectMatch}
              aria-label="Previous source match"
              onClick={() => navigateMatch(-1)}
            >
              Previous
            </button>
            <button
              className="button ghost small-button"
              type="button"
              disabled={matchingLineNumbers.length === 0 || !onSelectMatch}
              aria-label="Next source match"
              onClick={() => navigateMatch(1)}
            >
              Next
            </button>
          </div>
        </div>
        <div className="heading-actions">
          {effectiveHighlight != null && (
            <button
              className="button ghost"
              type="button"
              onClick={() => handleCopyLine(effectiveHighlight)}
            >
              {copyStatus === 'line' ? 'Copied' : 'Copy path:line'}
            </button>
          )}
          <button className="button ghost" type="button" onClick={handleCopy}>
            {copyStatus === 'source' ? 'Copied' : 'Copy source path'}
          </button>
          {sourceOverride == null && (
            <a
              className="button ghost"
              href={sourceUrl(sourcePath)}
              target="_blank"
              rel="noreferrer"
            >
              Open source
            </a>
          )}
        </div>
      </div>
      {search.trim() && !loading && !error && (
        <p className="source-line-note source-search-note">
          {matchingLineNumbers.length === 0
            ? 'No lines match the source search.'
            : `${(activeMatchIndex ?? 0) + 1} / ${matchingLineNumbers.length.toLocaleString(
                'en-US',
              )} matching line(s). Use Enter/Shift+Enter or Previous/Next.`}
        </p>
      )}
      {sourceMapping.inRange > 0 && (
        <p className="source-line-note source-map-note">
          {sourceMapping.map.size.toLocaleString('en-US')} source line(s) map to{' '}
          {sourceMapping.inRange.toLocaleString('en-US')} assembly instruction(s)
          {onInspectInstruction ? '; click an asm badge to inspect the first instruction.' : '.'}
        </p>
      )}
      {sourceMapping.outside > 0 && (
        <p className="source-line-note source-line-warning">
          {sourceMapping.outside.toLocaleString('en-US')} instruction(s) reference
          preprocessed source lines outside this file; their line mapping is not shown.
        </p>
      )}
      {sourceJumpEdges.length > 0 && (
        <p className="source-line-note source-jump-note">
          {sourceJumpEdges.length.toLocaleString('en-US')} source jump line(s) shown in
          the gutter; hover a line to highlight its source range.
        </p>
      )}
      {effectiveHighlight != null && (
        <p className="source-line-note">
          Selected source line <strong>{effectiveHighlight}</strong>. Click a line to
          update the deep link; double-click to copy <code>path:line</code>.
        </p>
      )}
      {outOfRange && (
        <p className="source-line-note source-line-warning">
          Debug source line <strong>{highlightLine}</strong> is outside this file (
          {lines.length} lines). SPIR-V debug line numbers may refer to preprocessed
          source; the original file mapping is not available in this report.
        </p>
      )}
      {loading && <div className="empty-state compact">Loading source…</div>}
      {error && (
        <div className="empty-state compact">
          Source unavailable: {error}. Check that <code>serve.mjs</code> is running
          with the repository root.
        </div>
      )}
      {!loading && !error && (
        <div className="source-scroll" ref={sourceScrollRef}>
          <div className="source-lines" ref={sourceLinesRef}>
          {lines.map((line) => (
            <Fragment key={line.number}>
            <div
              className={[
                'source-line',
                onSelectLine ? 'selectable' : '',
                line.number === effectiveHighlight ? 'active' : '',
                matchingLines.has(line.number) ? 'match' : '',
                line.number === activeMatchLine ? 'match-active' : '',
                jumpRange &&
                line.number >= jumpRange[0] &&
                line.number <= jumpRange[1]
                  ? 'jump-highlight'
                  : '',
                laneHighlightLines?.has(line.number) ? 'lane-highlight' : '',
                selectedLaneLines?.has(line.number) ? 'lane-selected' : '',
              ]
                .filter(Boolean)
                .join(' ')}
              title={lineTitle(line)}
              onClick={() => {
                if (onSelectLine && !window.getSelection()?.toString()) {
                  onSelectLine(line.number);
                }
              }}
              onDoubleClick={() => {
                void handleCopyLine(line.number);
              }}
              ref={(element) => {
                if (element) {
                  lineRefs.current.set(line.number, element);
                } else {
                  lineRefs.current.delete(line.number);
                }
              }}
            >
              <span className="source-number">{line.number}</span>
              <span className="source-jump-gutter" aria-hidden="true" />
              <code
                className="source-code"
                dangerouslySetInnerHTML={{ __html: line.html || ' ' }}
              />
              {onInspectInstruction &&
                (sourceMapping.map.get(line.number)?.length ?? 0) > 0 && (
                  <button
                    className="source-asm-link"
                    type="button"
                    aria-expanded={
                      sourceMapping.map.get(line.number)!.length > 1
                        ? expandedLine === line.number
                        : undefined
                    }
                    title={
                      sourceMapping.map.get(line.number)!.length > 1
                        ? `${expandedLine === line.number ? 'Hide' : 'Show'} ${sourceMapping.map.get(line.number)!.length} mapped assembly instructions`
                        : `Inspect instruction ${sourceMapping.map.get(line.number)![0]} mapped to line ${line.number}`
                    }
                    onClick={(event) => {
                      event.stopPropagation();
                      const mapped = sourceMapping.map.get(line.number)!;
                      if (mapped.length > 1) {
                        setExpandedLine((current) =>
                          current === line.number ? null : line.number,
                        );
                      } else {
                        onInspectInstruction(mapped[0]);
                      }
                    }}
                    onDoubleClick={(event) => event.stopPropagation()}
                  >
                    asm {sourceMapping.map.get(line.number)!.length}
                    {sourceMapping.map.get(line.number)!.length > 1 && (
                      <span className="source-asm-caret" aria-hidden="true">
                        {' ▾'}
                      </span>
                    )}
                  </button>
                )}
              {(() => {
                const usage = sourceMapping.usageByLine.get(line.number);
                if (!usage) {
                  return null;
                }
                const classPrefix =
                  effectiveLaneClass != null ? `${effectiveLaneClass}:` : null;
                const reads = classPrefix
                  ? [...usage.reads].filter((key) => key.startsWith(classPrefix))
                  : [...usage.reads];
                const writes = classPrefix
                  ? [...usage.writes].filter((key) => key.startsWith(classPrefix))
                  : [...usage.writes];
                if (reads.length === 0 && writes.length === 0) {
                  return null;
                }
                const firstInstruction = sourceMapping.map.get(line.number)?.[0];
                const usageTitle = [
                  reads.length ? `read: ${reads.join(', ')}` : '',
                  writes.length ? `write: ${writes.join(', ')}` : '',
                ]
                  .filter(Boolean)
                  .join(' | ');
                return (
                  <button
                    className="source-rw"
                    type="button"
                    disabled={!onInspectRegisters || firstInstruction == null}
                    title={`${usageTitle}${
                      firstInstruction != null
                        ? `\n\nInspect registers at instruction ${firstInstruction}`
                        : ''
                    }`}
                    onClick={(event) => {
                      event.stopPropagation();
                      if (firstInstruction != null) {
                        onInspectRegisters?.(firstInstruction);
                      }
                    }}
                    onDoubleClick={(event) => event.stopPropagation()}
                  >
                    {reads.length > 0 && (
                      <span className="asm-rw-item asm-read">R{reads.length}</span>
                    )}
                    {writes.length > 0 && (
                      <span className="asm-rw-item asm-write">W{writes.length}</span>
                    )}
                  </button>
                );
              })()}
              {cbufferByLine.has(line.number) &&
                (() => {
                  const usage = cbufferByLine.get(line.number)!;
                  const slots = [...usage.slots].sort((left, right) => left - right);
                  const range =
                    usage.min == null
                      ? 'dynamic offset'
                      : usage.min === usage.max
                        ? `offset ${usage.min}`
                        : `offsets ${usage.min}–${usage.max}`;
                  const reads = usage.reads
                    .slice()
                    .sort((left, right) => left.instruction - right.instruction)
                    .slice(0, 8)
                    .map(
                      (read) =>
                        `instruction ${read.instruction} · cb${read.slot} · ${
                          read.dynamic ? 'dynamic offset' : `${read.dwords} DWORD at ${read.offset}`
                        }`,
                    )
                    .join('\n');
                  const more =
                    usage.reads.length > 8 ? `\n… ${usage.reads.length - 8} more read(s)` : '';
                  return (
                    <button
                      className="source-cbuffer"
                      type="button"
                      disabled={!onInspectCbuffer}
                      title={`${usage.count} cbuffer read(s) on line ${line.number} · ${slots
                        .map((slot) => `cb${slot}`)
                        .join(', ')} · ${range}${
                        usage.dynamic ? ` · ${usage.dynamic} dynamic` : ''
                      }\n${reads}${more}\n\nOpen CBuffer reads`}
                      onClick={(event) => {
                        event.stopPropagation();
                        onInspectCbuffer?.(line.number);
                      }}
                      onDoubleClick={(event) => event.stopPropagation()}
                    >
                      CB{usage.count}
                    </button>
                  );
                })()}
              {(() => {
                const aliveCount = sourceMapping.aliveByLine.get(line.number) ?? 0;
                if (aliveCount <= 0) {
                  return null;
                }
                const ranges = sourceMapping.aliveRangesByLine.get(line.number);
                const labels = [...(sourceMapping.aliveLabelsByLine.get(line.number) ?? [])]
                  .sort(compareLaneLabels)
                  .map((label) => {
                    const labelRanges = ranges?.get(label);
                    return labelRanges?.size
                      ? `${label} [${[...labelRanges].join(', ')}]`
                      : label;
                  });
                const labelText = labels.slice(0, 16).join(', ');
                const more =
                  labels.length > 16 ? `, … +${labels.length - 16} more` : '';
                const firstInstruction = sourceMapping.map.get(line.number)?.[0];
                const title =
                  `Max ${aliveCount} live registers across mapped instructions on line ${line.number}` +
                  (labelText ? `\n${labelText}${more}` : '') +
                  (firstInstruction != null
                    ? `\n\nInspect registers at instruction ${firstInstruction}`
                    : '');
                return (
                  <button
                    className="source-alive"
                    type="button"
                    disabled={!onInspectRegisters || firstInstruction == null}
                    title={title}
                    onClick={(event) => {
                      event.stopPropagation();
                      if (firstInstruction != null) {
                        onInspectRegisters?.(firstInstruction);
                      }
                    }}
                    onDoubleClick={(event) => event.stopPropagation()}
                  >
                    A{aliveCount}
                  </button>
                );
              })()}
              {(() => {
                const labels = [
                  ...(sourceMapping.aliveLabelsByLine.get(line.number) ?? []),
                ].sort(compareLaneLabels);
                if (labels.length === 0) {
                  return null;
                }
                const ranges = sourceMapping.aliveRangesByLine.get(line.number);
                const hiddenLabels = labels.slice(12);
                const lineInstruction = sourceMapping.instructionsByLine.get(
                  line.number,
                )?.[0]?.index;
                const laneTitle = labels
                  .map((label) => {
                    const labelRanges = ranges?.get(label);
                    return labelRanges?.size
                      ? `${label} [${[...labelRanges].join(', ')}]`
                      : label;
                  })
                  .join('\n');
                return (
                  <span className="source-lane-rail" title={laneTitle}>
                    {labels.slice(0, 12).map((label) => {
                      const labelRanges = ranges?.get(label);
                      const lineInstruction = sourceMapping.instructionsByLine.get(
                        line.number,
                      )?.[0]?.index;
                      return (
                        <button
                          className={`source-lane${
                            selectedLane === label ? ' selected' : ''
                          }`}
                          key={label}
                          type="button"
                          data-lane={label}
                          tabIndex={onInspectLane ? 0 : -1}
                          aria-label={`Inspect ${label} in Registers`}
                          style={{ background: laneColor(label) }}
                          title={`${label}${
                            labelRanges?.size ? ` [${[...labelRanges].join(', ')}]` : ''
                          }\nHighlight mapped source lines in this lifetime${
                            selectedLane === label ? '\nSelected lane' : ''
                          }${
                            onInspectLane ? '\n\nClick to inspect this lane in Registers' : ''
                          }`}
                          onClick={(event) => {
                            event.stopPropagation();
                            if (lineInstruction != null) {
                              onInspectLane?.(label, lineInstruction);
                            }
                          }}
                          onDoubleClick={(event) => event.stopPropagation()}
                          onMouseEnter={() => setHoveredLane(label)}
                          onMouseLeave={() => setHoveredLane(null)}
                        />
                      );
                    })}
                    {hiddenLabels.length > 0 &&
                      (onInspectLane && lineInstruction != null ? (
                        <button
                          className="source-lane-more"
                          type="button"
                          title={`Show ${hiddenLabels.length} more live lane(s) on line ${line.number}`}
                          aria-label={`Show ${hiddenLabels.length} more live lanes`}
                          onClick={(event) => {
                            event.stopPropagation();
                            openLaneMenu(
                              event.currentTarget,
                              line.number,
                              hiddenLabels,
                              lineInstruction,
                            );
                          }}
                          onDoubleClick={(event) => event.stopPropagation()}
                        >
                          +{hiddenLabels.length}
                        </button>
                      ) : (
                        <span className="source-lane-more">+{hiddenLabels.length}</span>
                      ))}
                  </span>
                );
              })()}
              {(() => {
                const pressure = sourceMapping.pressureByLine.get(line.number);
                if (pressure == null) {
                  return null;
                }
                const instructionIndex = sourceMapping.pressureIndexByLine.get(line.number);
                return (
                  <button
                    className="source-pressure"
                    type="button"
                    disabled={!onInspectRegisters || instructionIndex == null}
                    title={`Peak ${pressureLabel} pressure ${pressure} at instruction ${
                      instructionIndex ?? '—'
                    } across ${
                      sourceMapping.instructionsByLine.get(line.number)?.length ?? 0
                    } mapped instruction(s) on line ${line.number}${
                      instructionIndex != null
                        ? `\n\nInspect registers at instruction ${instructionIndex}`
                        : ''
                    }`}
                    onClick={(event) => {
                      event.stopPropagation();
                      if (instructionIndex != null) {
                        onInspectRegisters?.(instructionIndex);
                      }
                    }}
                    onDoubleClick={(event) => event.stopPropagation()}
                  >
                    P{pressure}
                  </button>
                );
              })()}
              {(() => {
                const pressure = sourceMapping.pressureByLine.get(line.number) ?? null;
                const alive = sourceMapping.aliveByLine.get(line.number) ?? 0;
                if (pressure == null && alive <= 0) {
                  return null;
                }
                const pressurePercent =
                  pressure != null &&
                  pressure > 0 &&
                  sourceMapping.maxPressure > 0
                    ? Math.max(
                        4,
                        Math.min(100, (pressure / sourceMapping.maxPressure) * 100),
                      )
                    : 0;
                const alivePercent =
                  alive > 0 && sourceMapping.maxAlive > 0
                    ? Math.max(4, Math.min(100, (alive / sourceMapping.maxAlive) * 100))
                    : 0;
                return (
                  <span
                    className="source-register-profile"
                    aria-hidden="true"
                    title={`Register profile on line ${line.number}\nPeak ${pressureLabel} pressure ${pressure ?? '—'} / ${sourceMapping.maxPressure}\nLive registers ${alive} / ${sourceMapping.maxAlive}`}
                  >
                    {pressure != null && (
                      <span className="source-profile-track source-profile-pressure">
                        <span
                          className="source-profile-fill"
                          style={{ width: `${pressurePercent}%` }}
                        />
                      </span>
                    )}
                    {alive > 0 && (
                      <span className="source-profile-track source-profile-alive">
                        <span
                          className="source-profile-fill"
                          style={{ width: `${alivePercent}%` }}
                        />
                      </span>
                    )}
                  </span>
                );
              })()}
            </div>
            {expandedLine === line.number && (
              <div className="source-instruction-list">
                {(sourceMapping.instructionsByLine.get(line.number) ?? []).map((instruction) => (
                  <button
                    className="source-instruction-row"
                    key={instruction.index}
                    type="button"
                    title={`Inspect instruction ${instruction.index}: ${instruction.text}`}
                    onClick={() => onInspectInstruction?.(instruction.index)}
                  >
                    <span className="source-instruction-index">#{instruction.index}</span>
                    <span className="source-instruction-opcode">{instruction.opcode}</span>
                    <span className="source-instruction-text">{instruction.text}</span>
                    {instruction.branch?.targetIndex != null && (
                      <span className="source-instruction-branch">
                        {instruction.branch.kind} → {instruction.branch.targetIndex}
                      </span>
                    )}
                  </button>
                ))}
              </div>
            )}
            </Fragment>
          ))}
          {jumpGeometry.length > 0 && (
            <div className="source-jump-layer">
              {jumpGeometry.map(({ edge, top, height }) => (
                <button
                  key={`${edge.fromIndex}:${edge.toIndex}`}
                  type="button"
                  className={`source-jump-wire${
                    hoveredJump?.fromIndex === edge.fromIndex &&
                    hoveredJump?.toIndex === edge.toIndex
                      ? ' active'
                      : ''
                  }${onSelectLine ? ' clickable' : ''}`}
                  style={{
                    top,
                    height: Math.max(2, height),
                    left: 4 + Math.min(4, Math.max(0, edge.indent)) * 7,
                    color: edge.color,
                    zIndex:
                      hoveredJump?.fromIndex === edge.fromIndex &&
                      hoveredJump?.toIndex === edge.toIndex
                        ? 3
                        : 1,
                  }}
                  tabIndex={onSelectLine ? 0 : -1}
                  aria-label={`${edge.label}; jump to source line ${edge.toLine}`}
                  title={`${edge.label}\nFrom L${edge.fromLine} ${edge.fromText}\nTo L${edge.toLine} ${edge.toText}\n\nClick to jump to L${edge.toLine}`}
                  onClick={(event) => {
                    event.stopPropagation();
                    onSelectLine?.(edge.toLine);
                  }}
                  onFocus={() => setHoveredJump(edge)}
                  onBlur={() => setHoveredJump(null)}
                  onMouseEnter={() => setHoveredJump(edge)}
                  onMouseLeave={() => setHoveredJump(null)}
                />
              ))}
            </div>
          )}
          </div>
        </div>
      )}
      {laneMenu && (
        <div
          className="source-lane-menu"
          role="menu"
          aria-label={`More live lanes on line ${laneMenu.line}`}
          style={{ left: laneMenu.left, top: laneMenu.top }}
          onMouseLeave={() => setHoveredLane(null)}
        >
          <div className="source-lane-menu-header">
            More live lanes · line {laneMenu.line}
          </div>
          {laneMenu.labels.map((label) => (
            <button
              className="source-lane-menu-item"
              key={label}
              type="button"
              role="menuitem"
              title={`Inspect ${label} in Registers`}
              onMouseEnter={() => setHoveredLane(label)}
              onClick={() => {
                onInspectLane?.(label, laneMenu.instruction);
                setHoveredLane(null);
                setLaneMenu(null);
              }}
            >
              <span className="source-lane-menu-swatch" style={{ background: laneColor(label) }} />
              <span>{label}</span>
            </button>
          ))}
        </div>
      )}
    </div>
  );
}
