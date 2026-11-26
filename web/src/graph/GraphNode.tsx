import { Handle, Position, type NodeProps } from '@xyflow/react';
import type { CatalogNode, Diagnostic, GraphNode as GraphNodeJson, JsonValue } from '../api/types';
import { useEditorStore } from '../state/editor';
import { NodePreview } from './NodePreview';
import { InlineSlotValue } from '../widgets/ValueWidget';
import { formatValue, slotConcreteType, typeColor, typeLabel, visibleSlots } from './model';

export interface GraphNodeData {
  [key: string]: unknown;
  node: GraphNodeJson;
  definition?: CatalogNode;
  diagnostics: Diagnostic[];
  revision: number;
  showPreviews: boolean;
}

export function GraphNodeView({ data, selected }: NodeProps) {
  const row = data as unknown as GraphNodeData;
  const node = row.node;
  const definition = row.definition;
  const setSlotValue = useEditorStore((state) => state.setSlotValue);
  const inputs = visibleSlots(definition, 'in');
  const outputs = visibleSlots(definition, 'out');
  const errorCount = row.diagnostics.filter((diagnostic) => diagnostic.severity === 'error').length;
  const warningCount = row.diagnostics.filter((diagnostic) => diagnostic.severity === 'warning').length;
  const block = definition?.blockNode === true;
  const unknown = !definition;

  return (
    <div className={[
      'sg-node',
      block ? 'block-node' : '',
      selected ? 'selected' : '',
      errorCount > 0 ? 'has-error' : '',
      warningCount > 0 && errorCount === 0 ? 'has-warning' : '',
      unknown ? 'unknown' : '',
    ].filter(Boolean).join(' ')}>
      <div className="sg-node-title">
        <span className="sg-node-title-text">{definition?.title || node.type}</span>
        <span className="sg-node-badges">
          {errorCount > 0 && <span className="badge error" title="有错误">{errorCount}</span>}
          {warningCount > 0 && errorCount === 0 && <span className="badge warning" title="有警告">{warningCount}</span>}
          {!block && definition?.hasPreview && row.showPreviews && (
            <NodePreview nodeId={node.id} revision={row.revision} enabled={row.showPreviews} />
          )}
        </span>
      </div>

      {!block && (
        <div className="sg-node-subtitle">
          <span>{definition?.category || 'Unknown'}</span>
          <span className="node-id">#{node.id}</span>
        </div>
      )}

      <div className="sg-node-body">
        <div className="sg-node-inputs">
          {inputs.map((slot) => {
            const state = node.slots[String(slot.id)];
            const connected = state?.mode === 'connected';
            const concrete = slotConcreteType(slot, state);
            return (
              <div className="sg-slot-row input" key={`in-${slot.id}`} title={`${slot.displayName}: ${typeLabel(concrete)}`}>
                <Handle
                  type="target"
                  position={Position.Left}
                  id={String(slot.id)}
                  className="sg-handle"
                  style={{ background: typeColor(concrete), borderColor: typeColor(concrete) }}
                />
                <span className="sg-port-letter" style={{ color: typeColor(concrete) }}>
                  {slotTypeLetter(concrete)}
                </span>
                <span className="sg-slot-name">{slot.displayName}</span>
                {!connected && slot.supportsLiteral && (
                  <InlineSlotValue
                    nodeId={node.id}
                    slot={slot}
                    state={state}
                    onCommit={(value: JsonValue) => {
                      void setSlotValue(node.id, slot.id, value, slotConcreteType(slot, state));
                    }}
                  />
                )}
                {connected && <span className="sg-slot-connected">linked</span>}
                {!connected && !slot.supportsLiteral && (
                  <span className="sg-slot-default">{formatValue(slot.default)}</span>
                )}
              </div>
            );
          })}
        </div>
        <div className="sg-node-outputs">
          {outputs.map((slot) => {
            const state = node.slots[String(slot.id)];
            const concrete = slotConcreteType(slot, state);
            return (
              <div className="sg-slot-row output" key={`out-${slot.id}`} title={`${slot.displayName}: ${typeLabel(concrete)}`}>
                <span className="sg-slot-name">{slot.displayName}</span>
                <span className="sg-slot-type" style={{ color: typeColor(concrete) }}>{slotTypeLetter(concrete)}</span>
                <Handle
                  type="source"
                  position={Position.Right}
                  id={String(slot.id)}
                  className="sg-handle"
                  style={{ background: typeColor(concrete), borderColor: typeColor(concrete) }}
                />
              </div>
            );
          })}
        </div>
      </div>

      {Object.keys(node.settings).length > 0 && (
        <div className="sg-node-settings">
          {Object.entries(node.settings).slice(0, 4).map(([name, value]) => (
            <span className="setting-chip" key={name}>{name}: {formatValue(value)}</span>
          ))}
        </div>
      )}

      {node.unknownData && <div className="node-unknown">Unknown node data</div>}
    </div>
  );
}

function slotTypeLetter(type: string): string {
  const normalized = type.toLowerCase();
  if (normalized === 'boolean') {
    return 'B';
  }
  if (normalized === 'color') {
    return 'C';
  }
  if (normalized === 'vector4' || normalized === 'color') {
    return '4';
  }
  if (normalized === 'vector3') {
    return '3';
  }
  if (normalized === 'vector2') {
    return '2';
  }
  if (normalized.startsWith('texture') || normalized === 'cubemap') {
    return 'T';
  }
  if (normalized.startsWith('matrix')) {
    return 'M';
  }
  return 'F';
}
