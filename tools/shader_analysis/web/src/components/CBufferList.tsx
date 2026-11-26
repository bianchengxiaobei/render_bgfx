import { useEffect, useMemo, useRef, useState } from 'react';
import type { CbufferRead, Instruction } from '../types';
import { formatMetric } from '../utils/format';

type ReadStatus = 'first' | 'ascending' | 'out-of-order' | 'repeated' | 'dynamic';

interface CBufferRow extends CbufferRead {
  status: ReadStatus;
  delta: number | null;
  text: string;
  sourceLine: number | null;
}

interface CBufferOrderGroup {
  key: string;
  sourceLine: number | null;
  instruction: number;
  rows: CBufferRow[];
  dwords: number;
  dynamic: number;
}

function classify(reads: CbufferRead[], instructions: Instruction[]): CBufferRow[] {
  const byInstruction = new Map(instructions.map((instruction) => [instruction.index, instruction]));
  const lastOffsetBySlot = new Map<number, number>();
  return [...reads]
    .sort((left, right) => left.instruction - right.instruction)
    .map((read) => {
      const instruction = byInstruction.get(read.instruction);
      const previous = lastOffsetBySlot.get(read.slot);
      let status: ReadStatus;
      let delta: number | null = null;
      if (read.dynamic) {
        status = 'dynamic';
      } else if (previous == null) {
        status = 'first';
      } else {
        delta = read.offset - previous;
        status = delta > 0 ? 'ascending' : delta < 0 ? 'out-of-order' : 'repeated';
      }
      if (!read.dynamic) {
        lastOffsetBySlot.set(read.slot, read.offset);
      }
      return {
        ...read,
        status,
        delta,
        text: instruction?.text ?? '',
        sourceLine: instruction?.sourceLine ?? null,
      };
    });
}

function statusLabel(status: ReadStatus): string {
  switch (status) {
    case 'ascending':
      return 'ascending';
    case 'out-of-order':
      return 'out of order';
    case 'repeated':
      return 'repeated';
    case 'dynamic':
      return 'dynamic';
    case 'first':
      return 'first read';
  }
}

export default function CBufferList({
  reads,
  instructions,
  focusSourceLine = null,
  view,
  onViewChange,
  onClearFocus,
  onJumpInstruction,
  onJumpSource,
}: {
  reads: CbufferRead[];
  instructions: Instruction[];
  focusSourceLine?: number | null;
  view?: 'table' | 'order';
  onViewChange?: (view: 'table' | 'order') => void;
  onClearFocus?: () => void;
  onJumpInstruction?: (index: number) => void;
  onJumpSource?: (line: number) => void;
}) {
  const rows = useMemo(() => classify(reads, instructions), [reads, instructions]);
  const [localView, setLocalView] = useState<'table' | 'order'>('table');
  const activeView = view ?? localView;
  const changeView = (next: 'table' | 'order') => {
    setLocalView(next);
    onViewChange?.(next);
  };
  const focusedCount = useMemo(
    () =>
      focusSourceLine == null
        ? 0
        : rows.filter((row) => row.sourceLine === focusSourceLine).length,
    [focusSourceLine, rows],
  );
  const firstFocusIndex = useMemo(
    () =>
      focusSourceLine == null
        ? -1
        : rows.findIndex((row) => row.sourceLine === focusSourceLine),
    [focusSourceLine, rows],
  );
  const orderGroups = useMemo(() => {
    const groups = new Map<string, CBufferOrderGroup>();
    for (const row of rows) {
      const key =
        row.sourceLine != null ? `L${row.sourceLine}` : `I${row.instruction}`;
      const group =
        groups.get(key) ??
        ({
          key,
          sourceLine: row.sourceLine,
          instruction: row.instruction,
          rows: [],
          dwords: 0,
          dynamic: 0,
        } satisfies CBufferOrderGroup);
      group.rows.push(row);
      group.dwords += row.dwords;
      if (row.dynamic) {
        group.dynamic += 1;
      }
      groups.set(key, group);
    }
    return [...groups.values()];
  }, [rows]);
  const firstFocusGroupIndex = useMemo(
    () =>
      focusSourceLine == null
        ? -1
        : orderGroups.findIndex((group) => group.sourceLine === focusSourceLine),
    [focusSourceLine, orderGroups],
  );
  const firstFocusedRowRef = useRef<HTMLTableRowElement | null>(null);
  const firstFocusedOrderRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    if (focusSourceLine != null) {
      const target =
        activeView === 'table' ? firstFocusedRowRef.current : firstFocusedOrderRef.current;
      target?.scrollIntoView({ block: 'center', behavior: 'smooth' });
    }
  }, [activeView, firstFocusGroupIndex, firstFocusIndex, focusSourceLine]);
  const mappedSourceLines = useMemo(
    () => new Set(rows.map((row) => row.sourceLine).filter((line): line is number => line != null)),
    [rows],
  );

  const slots = useMemo(() => {
    const map = new Map<number, { count: number; dynamic: number; min: number; max: number }>();
    for (const row of rows) {
      const entry = map.get(row.slot) ?? {
        count: 0,
        dynamic: 0,
        min: Number.POSITIVE_INFINITY,
        max: Number.NEGATIVE_INFINITY,
      };
      entry.count += 1;
      if (row.dynamic) {
        entry.dynamic += 1;
      } else {
        entry.min = Math.min(entry.min, row.offset);
        entry.max = Math.max(entry.max, row.offset);
      }
      map.set(row.slot, entry);
    }
    return [...map.entries()].sort(([left], [right]) => left - right);
  }, [rows]);

  if (rows.length === 0) {
    return <div className="empty-state compact">No cbuffer reads in this detail.</div>;
  }

  return (
    <div className="cbuffer-view">
      <div className="cbuffer-summary">
        {slots.map(([slot, entry]) => (
          <div className="cbuffer-slot" key={slot}>
            <strong>cb{slot}</strong>
            <span>{formatMetric(entry.count)} reads</span>
            <span>
              offset {Number.isFinite(entry.min) ? entry.min : 'n/a'}–
              {Number.isFinite(entry.max) ? entry.max : 'n/a'}
            </span>
            {entry.dynamic > 0 && <span className="cb-dynamic">{entry.dynamic} dynamic</span>}
          </div>
        ))}
        {mappedSourceLines.size > 0 && (
          <div className="cbuffer-slot cbuffer-source-summary">
            <strong>source</strong>
            <span>{formatMetric(mappedSourceLines.size)} mapped lines</span>
          </div>
        )}
      </div>
      <div className="cbuffer-toolbar">
        <div className="cbuffer-view-toggle" role="group" aria-label="CBuffer view">
          <button
            className={activeView === 'table' ? 'active' : ''}
            type="button"
            aria-pressed={activeView === 'table'}
            onClick={() => changeView('table')}
          >
            Table
          </button>
          <button
            className={activeView === 'order' ? 'active' : ''}
            type="button"
            aria-pressed={activeView === 'order'}
            onClick={() => changeView('order')}
          >
            Read order
          </button>
        </div>
        <span className="muted">
          {formatMetric(rows.length)} reads · {formatMetric(orderGroups.length)} group(s)
        </span>
      </div>
      {focusSourceLine != null && (
        <div className={`cbuffer-focus${focusedCount === 0 ? ' empty' : ''}`}>
          <span>
            Source line <strong>{focusSourceLine}</strong> ·{' '}
            {focusedCount === 0
              ? 'no mapped cbuffer reads'
              : `${formatMetric(focusedCount)} mapped read(s)`}
          </span>
          {onClearFocus && (
            <button className="button ghost" type="button" onClick={onClearFocus}>
              Show all
            </button>
          )}
        </div>
      )}
      {activeView === 'table' && (
      <div className="table-panel cbuffer-table">
        <table className="data-table">
          <thead>
            <tr>
              <th className="numeric">#</th>
              <th className="numeric">Instruction</th>
              <th>Opcode</th>
              <th className="numeric">Source</th>
              <th className="numeric">Slot</th>
              <th className="numeric">Offset</th>
              <th className="numeric">Dwords</th>
              <th className="numeric">Delta</th>
              <th>Access</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((row, index) => {
              const focused =
                focusSourceLine != null && row.sourceLine === focusSourceLine;
              const dimmed = focusSourceLine != null && focusedCount > 0 && !focused;
              return (
              <tr
                className={`cb-row cb-${row.status}${focused ? ' cb-focus' : ''}${
                  dimmed ? ' cb-dimmed' : ''
                }`}
                key={`${row.instruction}-${row.slot}-${row.offset}-${index}`}
                ref={index === firstFocusIndex ? firstFocusedRowRef : undefined}
              >
                <td className="numeric muted">{index + 1}</td>
                <td className="numeric">
                  {onJumpInstruction ? (
                    <button
                      className="link-button mono"
                      type="button"
                      onClick={() => onJumpInstruction(row.instruction)}
                    >
                      {row.instruction}
                    </button>
                  ) : (
                    <span className="mono">{row.instruction}</span>
                  )}
                </td>
                <td className="mono cb-instruction" title={row.text}>
                  {row.text}
                </td>
                <td
                  className="numeric cb-source"
                  title={
                    row.sourceLine == null
                      ? 'No source line mapping; rebuild with debug info'
                      : `Jump to source line ${row.sourceLine}`
                  }
                >
                  {row.sourceLine == null ? (
                    <span className="muted">—</span>
                  ) : onJumpSource ? (
                    <button
                      className="link-button mono"
                      type="button"
                      onClick={() => onJumpSource(row.sourceLine!)}
                    >
                      L{row.sourceLine}
                    </button>
                  ) : (
                    <span className="mono">L{row.sourceLine}</span>
                  )}
                </td>
                <td className="numeric">cb{row.slot}</td>
                <td className="numeric">{row.dynamic ? 'n/a' : row.offset}</td>
                <td className="numeric">{formatMetric(row.dwords)}</td>
                <td className="numeric">
                  {row.delta == null ? '—' : row.delta > 0 ? `+${row.delta}` : row.delta}
                </td>
                <td>
                  <span className={`cb-status cb-status-${row.status}`}>
                    {statusLabel(row.status)}
                  </span>
                </td>
              </tr>
              );
            })}
          </tbody>
        </table>
      </div>
      )}
      {activeView === 'order' && (
        <div className="cbuffer-order">
          {orderGroups.map((group, groupIndex) => {
            const focusedGroup =
              focusSourceLine != null && group.sourceLine === focusSourceLine;
            const dimmedGroup =
              focusSourceLine != null && focusedCount > 0 && !focusedGroup;
            return (
              <div
                className={`cbuffer-order-group${focusedGroup ? ' cb-focus' : ''}${
                  dimmedGroup ? ' cb-dimmed' : ''
                }`}
                key={group.key}
                ref={groupIndex === firstFocusGroupIndex ? firstFocusedOrderRef : undefined}
              >
                <div className="cbuffer-order-head">
                  {group.sourceLine != null ? (
                    onJumpSource ? (
                      <button
                        className="link-button mono"
                        type="button"
                        onClick={() => onJumpSource(group.sourceLine!)}
                      >
                        Source L{group.sourceLine}
                      </button>
                    ) : (
                      <span className="mono">Source L{group.sourceLine}</span>
                    )
                  ) : onJumpInstruction ? (
                    <button
                      className="link-button mono"
                      type="button"
                      onClick={() => onJumpInstruction(group.instruction)}
                    >
                      Instruction {group.instruction}
                    </button>
                  ) : (
                    <span className="mono">Instruction {group.instruction}</span>
                  )}
                  <span>{formatMetric(group.rows.length)} read(s)</span>
                  <span>{formatMetric(group.dwords)} DWORD</span>
                  {group.dynamic > 0 && (
                    <span className="cb-dynamic">{group.dynamic} dynamic</span>
                  )}
                </div>
                <div className="cbuffer-order-reads">
                  {group.rows.map((row, rowIndex) => (
                    <div
                      className={`cbuffer-order-read cb-${row.status}`}
                      key={`${row.instruction}-${row.slot}-${row.offset}-${rowIndex}`}
                    >
                      <span className="cbuffer-order-slot">cb{row.slot}</span>
                      <span className="cbuffer-order-instruction">
                        {onJumpInstruction ? (
                          <button
                            className="link-button mono"
                            type="button"
                            onClick={() => onJumpInstruction(row.instruction)}
                          >
                            {row.instruction}
                          </button>
                        ) : (
                          <span className="mono">{row.instruction}</span>
                        )}
                      </span>
                      <span className="cbuffer-order-message" title={row.text}>
                        {row.dynamic
                          ? `Read ${row.dwords} DWORD scalar offset.`
                          : `Read ${row.dwords} DWORD at offset ${row.offset}.`}
                      </span>
                      <span className={`cb-status cb-status-${row.status}`}>
                        {statusLabel(row.status)}
                      </span>
                    </div>
                  ))}
                </div>
              </div>
            );
          })}
        </div>
      )}
      <p className="muted table-note">
        Offsets are compared per cbuffer slot in instruction order. Green is ascending,
        red is out of order, blue is a dynamic offset; DXBC offset is bytes. Source
        links use instruction debug mappings and show a dash when debug info is unavailable.
        Read order groups reads by source line, falling back to instruction when unmapped.
      </p>
    </div>
  );
}
