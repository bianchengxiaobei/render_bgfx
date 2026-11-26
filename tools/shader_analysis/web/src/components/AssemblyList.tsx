import { useEffect, useMemo, useRef, useState } from 'react';
import type { MouseEvent } from 'react';
import { useVirtualizer } from '@tanstack/react-virtual';
import { loadSourceText } from '../api';
import type { Instruction, LivenessInterval } from '../types';
import {
  clampMatchPosition,
  matchingInstructionIndices,
} from '../utils/assemblySearch';
import { dependencyKeysFor } from '../utils/dependencies';

const ROW_HEIGHT = 24;
const JUMP_GUTTER = 52;
const VISIBLE_ASM_LANES = 16;
const EMPTY_LANES: number[] = [];

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

interface LaneActivity {
  labels: string[];
  idByLabel: Map<string, number>;
  activeByInstruction: Map<number, number[]>;
  usageByInstruction: Map<number, { reads: Set<number>; writes: Set<number> }>;
  rangesByLane: Map<number, string[]>;
  firstWriteByLane: Map<number, number>;
}

function pushEvent(events: Map<number, number[]>, index: number, id: number) {
  const entries = events.get(index);
  if (entries) {
    entries.push(id);
  } else {
    events.set(index, [id]);
  }
}

function buildLaneActivity(
  instructions: Instruction[],
  liveness: LivenessInterval[],
): LaneActivity {
  if (liveness.length === 0) {
    return {
      labels: [],
      idByLabel: new Map(),
      activeByInstruction: new Map(),
      usageByInstruction: new Map(),
      rangesByLane: new Map(),
      firstWriteByLane: new Map(),
    };
  }
  const keys = new Set<string>();
  const addKey = (registerClass: string, index: number) => {
    keys.add(`${registerClass}:${index}`);
  };
  for (const interval of liveness) {
    addKey(interval.class, interval.index);
  }
  for (const instruction of instructions) {
    for (const read of instruction.reads) {
      addKey(read.class, read.index);
    }
    for (const write of instruction.writes) {
      addKey(write.class, write.index);
    }
  }
  const labels = [...keys].sort(compareLaneLabels);
  const idByLabel = new Map(labels.map((label, index) => [label, index]));
  const addedAt = new Map<number, number[]>();
  const removedAt = new Map<number, number[]>();
  const rangesByLane = new Map<number, string[]>();
  for (const interval of liveness) {
    const id = idByLabel.get(`${interval.class}:${interval.index}`);
    if (id == null) {
      continue;
    }
    const start = Math.max(0, interval.start);
    if (interval.end < start) {
      continue;
    }
    pushEvent(addedAt, start, id);
    pushEvent(removedAt, interval.end + 1, id);
    const range = `${start}–${interval.end}`;
    const ranges = rangesByLane.get(id) ?? [];
    if (!ranges.includes(range)) {
      ranges.push(range);
      rangesByLane.set(id, ranges);
    }
  }
  const instructionIndices = [...new Set(instructions.map((item) => item.index))].sort(
    (left, right) => left - right,
  );
  const eventIndices = [
    ...new Set([...addedAt.keys(), ...removedAt.keys()]),
  ].sort((left, right) => left - right);
  const active = new Set<number>();
  const activeByInstruction = new Map<number, number[]>();
  let eventCursor = 0;
  for (const index of instructionIndices) {
    while (eventCursor < eventIndices.length && eventIndices[eventCursor] <= index) {
      const eventIndex = eventIndices[eventCursor];
      for (const id of removedAt.get(eventIndex) ?? []) {
        active.delete(id);
      }
      for (const id of addedAt.get(eventIndex) ?? []) {
        active.add(id);
      }
      eventCursor += 1;
    }
    if (active.size > 0) {
      activeByInstruction.set(
        index,
        [...active].sort((left, right) => left - right),
      );
    }
  }
  const usageByInstruction = new Map<
    number,
    { reads: Set<number>; writes: Set<number> }
  >();
  const firstWriteByLane = new Map<number, number>();
  for (const instruction of instructions) {
    const usage = { reads: new Set<number>(), writes: new Set<number>() };
    for (const read of instruction.reads) {
      const id = idByLabel.get(`${read.class}:${read.index}`);
      if (id != null) {
        usage.reads.add(id);
      }
    }
    for (const write of instruction.writes) {
      const id = idByLabel.get(`${write.class}:${write.index}`);
      if (id != null) {
        usage.writes.add(id);
        const previous = firstWriteByLane.get(id);
        if (previous == null || instruction.index < previous) {
          firstWriteByLane.set(id, instruction.index);
        }
      }
    }
    if (usage.reads.size > 0 || usage.writes.size > 0) {
      usageByInstruction.set(instruction.index, usage);
    }
  }
  return {
    labels,
    idByLabel,
    activeByInstruction,
    usageByInstruction,
    rangesByLane,
    firstWriteByLane,
  };
}

interface JumpEdge {
  fromRow: number;
  toRow: number;
  sourceRow: number;
  targetRow: number;
  fromIndex: number;
  toIndex: number;
  indent: number;
  color: string;
  label: string;
}

function jumpColor(fromIndex: number, toIndex: number): string {
  let hash = 42;
  for (const character of `${fromIndex}:${toIndex}`) {
    hash = (hash * 31 + character.charCodeAt(0)) | 0;
  }
  return `hsl(${Math.abs(hash) % 360} 78% 62%)`;
}

function refsText(refs: { class: string; index: number; mask: string }[]): string {
  return refs.map((ref) => `${ref.class}${ref.index}${ref.mask}`).join(', ');
}

interface RefEntry {
  key: string;
  label: string;
}

function inspectRefs(refs: Instruction['reads']): RefEntry[] {
  const entries = new Map<string, RefEntry>();
  for (const ref of refs) {
    const key = `${ref.class}:${ref.index}`;
    if (entries.has(key)) {
      continue;
    }
    const label =
      ref.class === 'ssa'
        ? `%${ref.index}`
        : ref.class === 'resource'
          ? `res${ref.index}`
          : `${ref.class}${ref.index}`;
    entries.set(key, { key, label });
  }
  return [...entries.values()];
}

function Highlight({ text, query }: { text: string; query: string }) {
  const needle = query.trim();
  if (!needle) {
    return <>{text}</>;
  }
  const lowerText = text.toLowerCase();
  const lowerNeedle = needle.toLowerCase();
  const parts: { value: string; match: boolean }[] = [];
  let offset = 0;
  while (offset < text.length) {
    const found = lowerText.indexOf(lowerNeedle, offset);
    if (found < 0) {
      parts.push({ value: text.slice(offset), match: false });
      break;
    }
    if (found > offset) {
      parts.push({ value: text.slice(offset, found), match: false });
    }
    parts.push({ value: text.slice(found, found + needle.length), match: true });
    offset = found + needle.length;
  }
  return (
    <>
      {parts.map((part, index) =>
        part.match ? (
          <mark key={index}>{part.value}</mark>
        ) : (
          <span key={index}>{part.value}</span>
        ),
      )}
    </>
  );
}

function opcodeText(instruction: Instruction, query: string) {
  const text = instruction.text;
  if (text.startsWith(instruction.opcode)) {
    return (
      <>
        <span className="asm-opcode">
          <Highlight text={instruction.opcode} query={query} />
        </span>
        <span className="asm-operands">
          <Highlight text={text.slice(instruction.opcode.length)} query={query} />
        </span>
      </>
    );
  }
  return (
    <span className="asm-operands">
      <Highlight text={text} query={query} />
    </span>
  );
}

function buildJumpEdges(
  instructions: Instruction[],
  rowByIndex: Map<number, number>,
): JumpEdge[] {
  const edges: JumpEdge[] = [];
  const seen = new Set<string>();
  const blockEnds: Record<string, string> = {
    if: 'endif',
    loop: 'endloop',
    switch: 'endswitch',
  };

  const addEdge = (
    fromRow: number,
    toRow: number,
    fromIndex: number,
    toIndex: number,
    indent: number,
    label: string,
  ) => {
    if (fromRow === toRow) {
      return;
    }
    const key = `${fromRow}:${toRow}`;
    if (seen.has(key)) {
      return;
    }
    seen.add(key);
    edges.push({
      fromRow: Math.min(fromRow, toRow),
      toRow: Math.max(fromRow, toRow),
      sourceRow: fromRow,
      targetRow: toRow,
      fromIndex,
      toIndex,
      indent,
      color: jumpColor(fromIndex, toIndex),
      label,
    });
  };

  instructions.forEach((instruction, row) => {
    const branch = instruction.branch;
    if (branch.targetIndex != null) {
      const targetRow = rowByIndex.get(branch.targetIndex);
      if (targetRow != null) {
        addEdge(
          row,
          targetRow,
          instruction.index,
          branch.targetIndex,
          branch.indent,
          `${branch.kind} ${instruction.index} → ${branch.targetIndex}`,
        );
      }
    }

    const endKind = blockEnds[branch.kind];
    if (!endKind) {
      return;
    }
    let depth = 0;
    for (let cursor = row; cursor < instructions.length; cursor++) {
      const kind = instructions[cursor].branch.kind;
      if (kind === branch.kind) {
        depth++;
      } else if (kind === endKind) {
        depth--;
        if (depth === 0) {
          addEdge(
            row,
            cursor,
            instruction.index,
            instructions[cursor].index,
            branch.indent,
            `${branch.kind} ${instruction.index} → ${endKind} ${instructions[cursor].index}`,
          );
          break;
        }
      }
    }
  });

  return edges;
}

export default function AssemblyList({
  instructions,
  liveness = [],
  selectedLane = null,
  laneClass = null,
  onSelectLaneClass,
  query,
  backend = 'dxbc',
  filterMatches = false,
  focusIndex = null,
  matchPosition = null,
  onSelectMatch,
  sourcePath = '',
  sourceOverride = null,
  showSourceContext = false,
  onSourceLine,
  onSelectInstruction,
  onInspectRegister,
}: {
  instructions: Instruction[];
  liveness?: LivenessInterval[];
  selectedLane?: string | null;
  laneClass?: string | null;
  onSelectLaneClass?: (className: string | null) => void;
  query: string;
  backend?: string;
  filterMatches?: boolean;
  focusIndex?: number | null;
  matchPosition?: number | null;
  onSelectMatch?: (position: number) => void;
  sourcePath?: string;
  sourceOverride?: string | null;
  showSourceContext?: boolean;
  onSourceLine?: (line: number) => void;
  onSelectInstruction?: (index: number) => void;
  onInspectRegister?: (registerKey: string, instructionIndex: number) => void;
}) {
  const parentRef = useRef<HTMLDivElement>(null);
  const [hoveredJump, setHoveredJump] = useState<JumpEdge | null>(null);
  const [hoveredLane, setHoveredLane] = useState<number | null>(null);
  const [laneMenu, setLaneMenu] = useState<{
    instruction: number;
    left: number;
    top: number;
    ids: number[];
  } | null>(null);
  const [sourceLines, setSourceLines] = useState<string[]>([]);
  const [refMenu, setRefMenu] = useState<{
    instruction: number;
    kind: 'read' | 'write';
    left: number;
    top: number;
    refs: RefEntry[];
  } | null>(null);
  const needle = query.trim().toLowerCase();
  const pressureKey: 'temp' | 'ssa' = backend === 'spirv' ? 'ssa' : 'temp';
  const pressureLabel = pressureKey === 'ssa' ? 'SSA values' : 'temp registers';
  const maxPressure = useMemo(
    () =>
      instructions.reduce(
        (maximum, instruction) =>
          Math.max(maximum, instruction.pressure[pressureKey] ?? 0),
        0,
      ),
    [instructions, pressureKey],
  );
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
  const laneActivity = useMemo(
    () => buildLaneActivity(instructions, visibleLiveness),
    [instructions, visibleLiveness],
  );
  const selectedLaneId =
    selectedLane != null ? (laneActivity.idByLabel.get(selectedLane) ?? null) : null;
  const selectedLaneVisible =
    selectedLane != null &&
    (effectiveLaneClass == null || selectedLane.startsWith(`${effectiveLaneClass}:`));
  const dependencyKeys = useMemo(
    () => dependencyKeysFor(selectedLane, instructions),
    [selectedLane, instructions],
  );
  const dependencyIds = useMemo(() => {
    const ids = new Set<number>();
    for (const key of dependencyKeys) {
      const id = laneActivity.idByLabel.get(key);
      if (id != null) {
        ids.add(id);
      }
    }
    return ids;
  }, [dependencyKeys, laneActivity]);
  const [sourceLoaded, setSourceLoaded] = useState(false);
  const sourceFileName = sourcePath.split('/').pop() || sourcePath;
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

  useEffect(() => {
    if (hoveredLane == null) {
      return;
    }
    const clear = () => setHoveredLane(null);
    const parent = parentRef.current;
    parent?.addEventListener('scroll', clear, { passive: true });
    window.addEventListener('resize', clear);
    return () => {
      parent?.removeEventListener('scroll', clear);
      window.removeEventListener('resize', clear);
    };
  }, [hoveredLane]);

  useEffect(() => {
    setHoveredLane(null);
    setLaneMenu(null);
  }, [effectiveLaneClass, query, filterMatches, instructions]);

  useEffect(() => {
    if (!laneMenu) {
      return;
    }
    const close = () => {
      setLaneMenu(null);
      setHoveredLane(null);
    };
    const onPointerDown = (event: PointerEvent) => {
      const target = event.target as Element | null;
      if (!target?.closest('.asm-lane-menu, .asm-lane-more')) {
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
      if (!target?.closest?.('.asm-lane-menu')) {
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

  function sourceContextText(line: number): string | null {
    if (!sourceLoaded) {
      return null;
    }
    const text = sourceLines[line - 1];
    if (text == null) {
      return null;
    }
    return text.trim() || '(blank line)';
  }

  useEffect(() => {
    if (!refMenu) {
      return;
    }
    const close = () => setRefMenu(null);
    const onPointerDown = (event: PointerEvent) => {
      const target = event.target as Element | null;
      if (!target?.closest('.asm-ref-menu')) {
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
      if (!target?.closest?.('.asm-ref-menu')) {
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
  }, [refMenu]);

  function openRefMenu(
    event: MouseEvent<HTMLButtonElement>,
    instruction: Instruction,
    kind: 'read' | 'write',
  ) {
    if (!onInspectRegister) {
      return;
    }
    event.stopPropagation();
    const sourceRefs = kind === 'read' ? instruction.reads : instruction.writes;
    const scopedRefs = effectiveLaneClass
      ? sourceRefs.filter((ref) => ref.class === effectiveLaneClass)
      : sourceRefs;
    const refs = inspectRefs(scopedRefs);
    if (refs.length === 0) {
      return;
    }
    const rect = event.currentTarget.getBoundingClientRect();
    const width = 240;
    const height = Math.min(320, 48 + refs.length * 27);
    setRefMenu({
      instruction: instruction.index,
      kind,
      left: Math.max(8, Math.min(rect.left, window.innerWidth - width - 8)),
      top: Math.max(8, Math.min(rect.bottom + 4, window.innerHeight - height - 8)),
      refs,
    });
  }

  function openLaneMenu(
    event: MouseEvent<HTMLButtonElement>,
    instruction: Instruction,
    ids: number[],
  ) {
    if (!onInspectRegister || ids.length === 0) {
      return;
    }
    event.stopPropagation();
    const rect = event.currentTarget.getBoundingClientRect();
    const width = 230;
    const height = Math.min(320, 48 + ids.length * 27);
    setLaneMenu({
      instruction: instruction.index,
      left: Math.max(8, Math.min(rect.left, window.innerWidth - width - 8)),
      top: Math.max(8, Math.min(rect.bottom + 4, window.innerHeight - height - 8)),
      ids,
    });
    setHoveredLane(null);
  }

  const matchingInstructionList = useMemo(
    () => matchingInstructionIndices(instructions, query),
    [instructions, query],
  );
  const matchingIndices = useMemo(
    () => new Set(matchingInstructionList),
    [matchingInstructionList],
  );
  const activeMatchPosition = clampMatchPosition(
    matchPosition,
    matchingInstructionList.length,
  );
  const activeMatchInstruction =
    activeMatchPosition != null
      ? (matchingInstructionList[activeMatchPosition] ?? null)
      : null;
  const filtered = useMemo(
    () =>
      filterMatches && needle
        ? instructions.filter((instruction) => matchingIndices.has(instruction.index))
        : instructions,
    [filterMatches, instructions, matchingIndices, needle],
  );
  const rowByIndex = useMemo(
    () => new Map(filtered.map((instruction, row) => [instruction.index, row])),
    [filtered],
  );
  const instructionByIndex = useMemo(
    () => new Map(instructions.map((instruction) => [instruction.index, instruction])),
    [instructions],
  );
  const jumpEdges = useMemo(
    () => (filterMatches && needle ? [] : buildJumpEdges(filtered, rowByIndex)),
    [filterMatches, filtered, needle, rowByIndex],
  );

  const virtualizer = useVirtualizer({
    count: filtered.length,
    getScrollElement: () => parentRef.current,
    estimateSize: () => ROW_HEIGHT,
    overscan: 30,
  });

  useEffect(() => {
    if (focusIndex == null) {
      return;
    }
    const row = filtered.findIndex((instruction) => instruction.index === focusIndex);
    if (row < 0) {
      return;
    }
    const visible = virtualizer
      .getVirtualItems()
      .some((item) => item.index === row);
    if (!visible) {
      virtualizer.scrollToIndex(row, { align: 'center' });
    }
  }, [filtered, focusIndex, virtualizer]);

  useEffect(() => {
    if (activeMatchInstruction == null) {
      return;
    }
    const row = rowByIndex.get(activeMatchInstruction);
    if (row == null) {
      return;
    }
    const visible = virtualizer
      .getVirtualItems()
      .some((item) => item.index === row);
    if (!visible) {
      virtualizer.scrollToIndex(row, { align: 'center' });
    }
  }, [activeMatchInstruction, rowByIndex, virtualizer]);

  useEffect(() => {
    setHoveredJump(null);
  }, [filterMatches, instructions, query]);

  const virtualItems = virtualizer.getVirtualItems();
  const firstVisibleRow = virtualItems[0]?.index ?? 0;
  const lastVisibleRow = virtualItems[virtualItems.length - 1]?.index ?? -1;
  const visibleJumps = useMemo(() => {
    if (lastVisibleRow < firstVisibleRow) {
      return [];
    }
    const candidates = jumpEdges.filter(
      (edge) => edge.fromRow <= lastVisibleRow && edge.toRow >= firstVisibleRow,
    );
    const sourceVisible = candidates.filter(
      (edge) => edge.fromRow >= firstVisibleRow && edge.fromRow <= lastVisibleRow,
    );
    const sourceRows = new Set(sourceVisible);
    return [
      ...sourceVisible,
      ...candidates.filter((edge) => !sourceRows.has(edge)),
    ].slice(0, 240);
  }, [firstVisibleRow, jumpEdges, lastVisibleRow]);

  if (instructions.length === 0) {
    return <div className="empty-state compact">No disassembly instructions in this detail.</div>;
  }

  return (
    <div className="assembly-shell">
      <div className="assembly-toolbar">
        <span>
          {filterMatches && needle
            ? `${filtered.length.toLocaleString('en-US')} of ${instructions.length.toLocaleString('en-US')} instructions`
            : `${instructions.length.toLocaleString('en-US')} instructions`}
        </span>
        <span className="muted">
          {needle
            ? `${matchingIndices.size.toLocaleString('en-US')} matching${
                filterMatches ? ' · filtered' : ' · highlighted'
              }${
                activeMatchPosition != null
                  ? ` · ${(activeMatchPosition + 1).toLocaleString('en-US')} / ${matchingInstructionList.length.toLocaleString('en-US')}`
                  : ''
              }`
            : 'virtualized'}
          {jumpEdges.length > 0 ? ` · ${jumpEdges.length} jump lines` : ''}
          {selectedLaneVisible
            ? ` · selected ${selectedLane} (${dependencyKeys.size} related)`
            : ''}
        </span>
        {laneClasses.length > 1 && (
          <label className="asm-lane-class">
            <span>Lane class</span>
            <select
              value={effectiveLaneClass ?? ''}
              title="Filter lane rails, live counts, and read/write counts by register class"
              aria-label="Filter Assembly lane rails by register class"
              onChange={(event) =>
                onSelectLaneClass?.(event.target.value ? event.target.value : null)
              }
            >
              <option value="">All</option>
              {laneClasses.map((value) => (
                <option key={value} value={value}>
                  {value}
                </option>
              ))}
            </select>
          </label>
        )}
        {activeMatchPosition != null && onSelectMatch && (
          <div className="asm-search-nav">
            <button
              className="button ghost small-button"
              type="button"
              aria-label="Previous assembly search match"
              onClick={() =>
                onSelectMatch(
                  (activeMatchPosition - 1 + matchingInstructionList.length) %
                    matchingInstructionList.length,
                )
              }
            >
              Prev
            </button>
            <button
              className="button ghost small-button"
              type="button"
              aria-label="Next assembly search match"
              onClick={() =>
                onSelectMatch(
                  (activeMatchPosition + 1) % matchingInstructionList.length,
                )
              }
            >
              Next
            </button>
          </div>
        )}
      </div>
      {filterMatches && needle && filtered.length === 0 ? (
        <div className="empty-state compact">No instructions match the search.</div>
      ) : (
        <div ref={parentRef} className="assembly-scroll">
          <div
            className="assembly-canvas"
            style={{ height: `${virtualizer.getTotalSize()}px` }}
          >
            {virtualItems.map((virtualRow) => {
              const instruction = filtered[virtualRow.index];
              const indent = Math.max(0, instruction.branch?.indent ?? 0);
              const pressure = instruction.pressure[pressureKey] ?? null;
              const pressurePercent =
                pressure != null && maxPressure > 0
                  ? Math.max(2, Math.min(100, (pressure / maxPressure) * 100))
                  : 0;
              const scopedReads = effectiveLaneClass
                ? instruction.reads.filter(
                    (read) => read.class === effectiveLaneClass,
                  )
                : instruction.reads;
              const scopedWrites = effectiveLaneClass
                ? instruction.writes.filter(
                    (write) => write.class === effectiveLaneClass,
                  )
                : instruction.writes;
              const readCount = new Set(
                scopedReads.map((read) => `${read.class}:${read.index}`),
              ).size;
              const writeCount = new Set(
                scopedWrites.map((write) => `${write.class}:${write.index}`),
              ).size;
              const activeLaneIds =
                laneActivity.activeByInstruction.get(instruction.index) ?? EMPTY_LANES;
              const laneUsage = laneActivity.usageByInstruction.get(instruction.index);
              const visibleLaneIds = activeLaneIds.slice(0, VISIBLE_ASM_LANES);
              const hiddenLaneCount = activeLaneIds.length - visibleLaneIds.length;
              const laneTitle =
                activeLaneIds.length > 0
                  ? `${activeLaneIds.length} live register lane(s)\n${activeLaneIds
                      .slice(0, 20)
                      .map((id) => {
                        const label = laneActivity.labels[id];
                        const markers = [
                          laneUsage?.reads.has(id) ? 'read' : '',
                          laneUsage?.writes.has(id) ? 'write' : '',
                        ].filter(Boolean);
                        return markers.length ? `${label} · ${markers.join('/')}` : label;
                      })
                      .join('\n')}${
                      activeLaneIds.length > 20
                        ? `\n… ${activeLaneIds.length - 20} more`
                        : ''
                    }`
                  : '';
              const jumpHighlighted =
                hoveredJump != null &&
                virtualRow.index >= hoveredJump.fromRow &&
                virtualRow.index <= hoveredJump.toRow;
              const laneHighlighted =
                hoveredLane != null && activeLaneIds.includes(hoveredLane);
              const laneSelected =
                selectedLaneId != null && activeLaneIds.includes(selectedLaneId);
              const sourceContext =
                showSourceContext &&
                instruction.sourceLine != null &&
                (virtualRow.index === 0 ||
                  filtered[virtualRow.index - 1]?.sourceLine !== instruction.sourceLine)
                  ? sourceContextText(instruction.sourceLine)
                  : null;
              return (
                <div
                  key={`${instruction.index}:${virtualRow.index}`}
                  className={`asm-row asm-branch-${instruction.branch?.kind ?? 'none'}${
                    instruction.index === focusIndex ? ' focused' : ''
                  }${jumpHighlighted ? ' jump-highlight' : ''}${
                    laneSelected ? ' lane-selected' : ''
                  }${laneHighlighted ? ' lane-highlight' : ''}${
                    needle && matchingIndices.has(instruction.index) ? ' search-match' : ''
                  }${
                    instruction.index === activeMatchInstruction
                      ? ' search-match-active'
                      : ''
                  }${onSelectInstruction ? ' selectable' : ''}`}
                  style={{
                    transform: `translateY(${virtualRow.start}px)`,
                    paddingLeft: `${8 + JUMP_GUTTER + indent * 16}px`,
                  }}
                  onClick={(event) => {
                    if (
                      !onSelectInstruction ||
                      (event.target as HTMLElement).closest('button, a')
                    ) {
                      return;
                    }
                    onSelectInstruction(instruction.index);
                  }}
                  onDoubleClick={(event) => {
                    if (
                      instruction.sourceLine == null ||
                      !onSourceLine ||
                      (event.target as HTMLElement).closest('button, a')
                    ) {
                      return;
                    }
                    onSourceLine(instruction.sourceLine);
                  }}
                  title={[
                    scopedReads.length ? `read: ${refsText(scopedReads)}` : '',
                    scopedWrites.length ? `write: ${refsText(scopedWrites)}` : '',
                    instruction.resourceRefs.length
                      ? `resources: ${instruction.resourceRefs
                          .map((ref) => `${ref.kind}:${ref.name}`)
                          .join(', ')}`
                      : '',
                    instruction.cbufferRead
                      ? `cb${instruction.cbufferRead.slot} offset ${instruction.cbufferRead.offset}`
                      : '',
                    instruction.branch?.targetIndex != null
                      ? `jump: ${instruction.index} → ${instruction.branch.targetIndex}`
                      : '',
                    instruction.sourceLine != null
                      ? sourcePreview(instruction.sourceLine)
                      : '',
                    onSelectInstruction ? 'click: focus this instruction in Registers' : '',
                  ]
                    .filter(Boolean)
                    .join('\n')}
                >
                  <span className="asm-index">
                    {String(instruction.index).padStart(5, '0')}
                  </span>
                  <span className={`asm-branch-marker branch-${instruction.branch?.kind ?? 'none'}`}>
                    {instruction.branch?.kind !== 'none' ? '│' : ''}
                  </span>
                  {opcodeText(instruction, query)}
                  {sourceContext != null && (
                    <span
                      className="asm-source-context"
                      title={sourcePreview(instruction.sourceLine!)}
                    >
                      {sourceContext}
                    </span>
                  )}
                  {instruction.cbufferRead && (
                    <span className="asm-badge">
                      cb{instruction.cbufferRead.slot}:{instruction.cbufferRead.offset}
                    </span>
                  )}
                  {instruction.sourceLine != null &&
                    (onSourceLine ? (
                      <button
                        className="asm-source link-button"
                        type="button"
                        onClick={() => onSourceLine(instruction.sourceLine!)}
                      >
                        L{instruction.sourceLine}
                      </button>
                    ) : (
                      <span className="asm-source">L{instruction.sourceLine}</span>
                    ))}
                  {(readCount > 0 || writeCount > 0) && (
                    <span
                      className="asm-rw"
                      title={[
                        readCount > 0 ? `read: ${refsText(scopedReads)}` : '',
                        writeCount > 0 ? `write: ${refsText(scopedWrites)}` : '',
                        onInspectRegister
                          ? 'click a read/write badge to inspect a register in Registers'
                          : '',
                      ]
                        .filter(Boolean)
                        .join('\n')}
                      aria-label={`${readCount} reads, ${writeCount} writes`}
                    >
                      {readCount > 0 &&
                        (onInspectRegister ? (
                          <button
                            className="asm-rw-item asm-read"
                            type="button"
                            title={`Inspect one of ${readCount} read register(s)`}
                            onClick={(event) => openRefMenu(event, instruction, 'read')}
                          >
                            R{readCount}
                          </button>
                        ) : (
                          <span className="asm-rw-item asm-read">R{readCount}</span>
                        ))}
                      {writeCount > 0 &&
                        (onInspectRegister ? (
                          <button
                            className="asm-rw-item asm-write"
                            type="button"
                            title={`Inspect one of ${writeCount} write register(s)`}
                            onClick={(event) => openRefMenu(event, instruction, 'write')}
                          >
                            W{writeCount}
                          </button>
                        ) : (
                          <span className="asm-rw-item asm-write">W{writeCount}</span>
                        ))}
                    </span>
                  )}
                  {activeLaneIds.length > 0 && (
                    <span
                      className="asm-lanes"
                      title={laneTitle}
                      aria-label={`${activeLaneIds.length} live register lanes`}
                      onMouseLeave={() => setHoveredLane(null)}
                    >
                      {visibleLaneIds.map((id) => {
                        const label = laneActivity.labels[id];
                        const read = laneUsage?.reads.has(id) ?? false;
                        const write = laneUsage?.writes.has(id) ?? false;
                        const dependent =
                          selectedLaneId != null &&
                          id !== selectedLaneId &&
                          dependencyIds.has(id);
                        const unrelated =
                          selectedLaneId != null &&
                          id !== selectedLaneId &&
                          !dependent;
                        const ranges = laneActivity.rangesByLane.get(id) ?? [];
                        const rangeText = ranges.length
                          ? ` [${ranges.slice(0, 3).join(', ')}${
                              ranges.length > 3 ? '…' : ''
                            }]`
                          : '';
                        const firstWrite = laneActivity.firstWriteByLane.get(id);
                        const writer =
                          firstWrite != null ? instructionByIndex.get(firstWrite) : null;
                        const writerSource =
                          writer?.sourceLine != null
                            ? sourcePreview(writer.sourceLine)
                            : '';
                        const writeText = writer
                          ? `write ${writer.index} ${writer.opcode}${
                              writerSource ? ` · ${writerSource}` : ''
                            }`
                          : '';
                        return (
                          <button
                            className={`asm-lane${read ? ' asm-lane-read' : ''}${
                              write ? ' asm-lane-write' : ''
                            }${unrelated ? ' asm-lane-unrelated' : ''}${
                              dependent ? ' asm-lane-dependent' : ''
                            }${id === selectedLaneId ? ' asm-lane-selected' : ''}`}
                            key={label}
                            type="button"
                            tabIndex={onInspectRegister ? 0 : -1}
                            style={{ background: laneColor(label) }}
                            title={`${label}${rangeText}${
                              read ? ' · read' : ''
                            }${write ? ' · write' : ''}${
                              dependent ? ' · dependent' : ''
                            }${id === selectedLaneId ? ' · selected' : ''}${
                              writeText ? `\n${writeText}` : ''
                            }${
                              onInspectRegister
                                ? '\nClick to inspect this lane in Registers'
                                : ''
                            }\nHover or focus to highlight its live Assembly rows`}
                            aria-label={`Inspect live lane ${label}`}
                            onMouseEnter={() => setHoveredLane(id)}
                            onMouseLeave={() =>
                              setHoveredLane((current) => (current === id ? null : current))
                            }
                            onFocus={() => setHoveredLane(id)}
                            onBlur={() =>
                              setHoveredLane((current) => (current === id ? null : current))
                            }
                            onClick={(event) => {
                              event.stopPropagation();
                              onInspectRegister?.(label, instruction.index);
                            }}
                            onDoubleClick={(event) => event.stopPropagation()}
                          />
                        );
                      })}
                      {hiddenLaneCount > 0 &&
                        (onInspectRegister ? (
                          <button
                            className="asm-lane-more"
                            type="button"
                            title={`View ${hiddenLaneCount} more live lane(s)`}
                            aria-label={`View ${hiddenLaneCount} more live lanes`}
                            aria-haspopup="menu"
                            onClick={(event) =>
                              openLaneMenu(
                                event,
                                instruction,
                                activeLaneIds.slice(VISIBLE_ASM_LANES),
                              )
                            }
                            onDoubleClick={(event) => event.stopPropagation()}
                          >
                            +{hiddenLaneCount}
                          </button>
                        ) : (
                          <span className="asm-lane-more">+{hiddenLaneCount}</span>
                        ))}
                    </span>
                  )}
                  {pressure != null && (
                    <span
                      className="asm-pressure"
                      title={`${pressure.toLocaleString('en-US')} live ${pressureLabel} at instruction ${instruction.index}`}
                      aria-label={`${pressure} live ${pressureLabel}`}
                    >
                      <span className="asm-pressure-label">
                        {pressureKey === 'ssa' ? 'ssa' : 'r'}
                      </span>
                      <span className="asm-pressure-track">
                        <span
                          className="asm-pressure-bar"
                          style={{ width: `${pressurePercent}%` }}
                        />
                      </span>
                      <span className="asm-pressure-value">
                        {pressure.toLocaleString('en-US')}
                      </span>
                    </span>
                  )}
                </div>
              );
            })}
            <div className="asm-jump-layer">
              {visibleJumps.map((edge) => {
                const top = Math.max(edge.fromRow, firstVisibleRow) * ROW_HEIGHT;
                const bottom =
                  (Math.min(edge.toRow, lastVisibleRow) + 1) * ROW_HEIGHT;
                const sourceVisible =
                  edge.sourceRow >= firstVisibleRow && edge.sourceRow <= lastVisibleRow;
                const targetVisible =
                  edge.targetRow >= firstVisibleRow && edge.targetRow <= lastVisibleRow;
                const left =
                  4 + Math.min(8, Math.max(0, edge.indent)) * 5 + (edge.fromIndex % 3);
                const fromInstruction = instructionByIndex.get(edge.fromIndex);
                const toInstruction = instructionByIndex.get(edge.toIndex);
                const tooltip = [
                  edge.label,
                  `From ${edge.fromIndex}: ${fromInstruction?.text ?? '—'}`,
                  `To ${edge.toIndex}: ${toInstruction?.text ?? '—'}`,
                  `rows ${edge.sourceRow + 1}–${edge.targetRow + 1}`,
                  onSelectInstruction
                    ? `Click to select target instruction ${edge.toIndex}`
                    : '',
                ]
                  .filter(Boolean)
                  .join('\n');
                return (
                  <div
                    key={`${edge.fromIndex}:${edge.toIndex}:${edge.label}`}
                    className={`asm-jump-group${onSelectInstruction ? ' clickable' : ''}`}
                    style={{ top, height: Math.max(2, bottom - top), left }}
                    role={onSelectInstruction ? 'button' : undefined}
                    tabIndex={onSelectInstruction ? 0 : undefined}
                    aria-label={tooltip}
                    onClick={(event) => {
                      event.stopPropagation();
                      onSelectInstruction?.(edge.toIndex);
                    }}
                    onKeyDown={(event) => {
                      if (
                        onSelectInstruction &&
                        (event.key === 'Enter' || event.key === ' ')
                      ) {
                        event.preventDefault();
                        onSelectInstruction(edge.toIndex);
                      }
                    }}
                    onMouseEnter={() => setHoveredJump(edge)}
                    onMouseLeave={() => setHoveredJump(null)}
                  >
                    <span
                      className="asm-jump-wire"
                      style={{ borderLeftColor: edge.color }}
                      title={tooltip}
                    />
                    {sourceVisible && (
                      <span
                        className="asm-jump-endpoint start"
                        style={{
                          background: edge.color,
                          top: edge.sourceRow * ROW_HEIGHT - top + 7,
                        }}
                      />
                    )}
                    {targetVisible && (
                      <span
                        className="asm-jump-endpoint target"
                        style={{
                          background: edge.color,
                          top: edge.targetRow * ROW_HEIGHT - top + 7,
                        }}
                      />
                    )}
                  </div>
                );
              })}
            </div>
          </div>
        </div>
      )}
      {refMenu && (
        <div
          className="asm-ref-menu"
          role="menu"
          aria-label={`${refMenu.kind === 'read' ? 'Read' : 'Write'} registers at instruction ${refMenu.instruction}`}
          style={{ left: refMenu.left, top: refMenu.top }}
        >
          <div className="asm-ref-menu-header">
            <span className={refMenu.kind === 'read' ? 'asm-read' : 'asm-write'}>
              {refMenu.kind === 'read' ? 'Reads' : 'Writes'}
            </span>
            <span>· instruction {refMenu.instruction}</span>
          </div>
          {refMenu.refs.map((ref) => (
            <button
              className="asm-ref-menu-item"
              key={ref.key}
              type="button"
              role="menuitem"
              title={`Inspect ${ref.key} in Registers`}
              onClick={() => {
                onInspectRegister?.(ref.key, refMenu.instruction);
                setRefMenu(null);
              }}
            >
              <span>{ref.label}</span>
              <span className="asm-ref-menu-key">{ref.key}</span>
            </button>
          ))}
        </div>
      )}
      {laneMenu && (
        <div
          className="asm-lane-menu"
          role="menu"
          aria-label={`More live lanes at instruction ${laneMenu.instruction}`}
          style={{ left: laneMenu.left, top: laneMenu.top }}
          onMouseLeave={() => setHoveredLane(null)}
        >
          <div className="asm-lane-menu-header">
            More live lanes · instruction {laneMenu.instruction}
          </div>
          {laneMenu.ids.map((id) => {
            const label = laneActivity.labels[id];
            return (
              <button
                className="asm-lane-menu-item"
                key={label}
                type="button"
                role="menuitem"
                title={`Inspect ${label} in Registers`}
                onMouseEnter={() => setHoveredLane(id)}
                onClick={() => {
                  onInspectRegister?.(label, laneMenu.instruction);
                  setLaneMenu(null);
                  setHoveredLane(null);
                }}
              >
                <span
                  className="asm-lane-menu-swatch"
                  style={{ background: laneColor(label) }}
                />
                <span>{label}</span>
              </button>
            );
          })}
        </div>
      )}
    </div>
  );
}
