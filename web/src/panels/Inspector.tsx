import { useEffect, useMemo, useState } from 'react';
import type { CatalogSlot, JsonValue, Keyword, Property } from '../api/types';
import { useEditorStore } from '../state/editor';
import { ValueEditor } from '../widgets/ValueWidget';
import { NodePreview } from '../graph/NodePreview';
import {
  effectiveSlotValue,
  findNode,
  formatValue,
  incomingEdge,
  isDynamic,
  isGradientValue,
  slotConcreteType,
  typeColor,
  typeLabel,
  visibleSlots,
} from '../graph/model';

export function Inspector() {
  const graph = useEditorStore((state) => state.graph);
  const selection = useEditorStore((state) => state.selection);
  const catalogMap = useEditorStore((state) => state.catalogMap);
  const revision = useEditorStore((state) => state.revision);
  const showPreviews = useEditorStore((state) => state.showPreviews);
  const diagnostics = useEditorStore((state) => state.diagnostics);
  const nodes = selection.nodes;

  const property = selection.property
    ? graph?.properties.find((item) => item.refName === selection.property)
    : undefined;
  const keyword = selection.keyword
    ? graph?.keywords.find((item) => item.refName === selection.keyword)
    : undefined;
  const node = nodes.length === 1 ? findNode(graph, nodes[0]) : undefined;
  const definition = node ? catalogMap[node.type] : undefined;

  return (
    <section className="panel inspector">
      <div className="panel-title">
        <span>Inspector</span>
        <span className="panel-count">{node ? `#${node.id}` : property?.name || keyword?.name || 'Graph'}</span>
      </div>
      <div className="inspector-scroll">
        {node ? (
          <>
            <div className="inspector-heading">
              <span className="type-dot" style={{ background: definition?.blockNode ? '#c98354' : '#7597bb' }} />
              <div>
                <div className="inspector-heading-title">{definition?.title || node.type}</div>
                <div className="inspector-heading-subtitle">{definition?.category || 'Unknown node'}</div>
              </div>
            </div>
            {definition?.hasPreview && showPreviews && (
              <div className="inspector-preview-large">
                <NodePreview nodeId={node.id} revision={revision} enabled={showPreviews} />
              </div>
            )}
            <NodeInspector
              nodeId={node.id}
              node={node}
              definition={definition}
              graph={graph}
              diagnostics={diagnostics.filter((item) => item.nodeId === node.id)}
            />
          </>
        ) : property ? (
          <PropertyInspector key={property.refName} property={property} />
        ) : keyword ? (
          <KeywordInspector key={keyword.refName} keyword={keyword} />
        ) : (
          <GraphInspector />
        )}
      </div>
    </section>
  );
}

interface NodeInspectorProps {
  nodeId: number;
  node: NonNullable<ReturnType<typeof findNode>>;
  definition: ReturnType<typeof useEditorStore.getState>['catalogMap'][string] | undefined;
  graph: ReturnType<typeof useEditorStore.getState>['graph'];
  diagnostics: ReturnType<typeof useEditorStore.getState>['diagnostics'];
}

function NodeInspector({ nodeId, node, definition, graph, diagnostics }: NodeInspectorProps) {
  const setNodeSetting = useEditorStore((state) => state.setNodeSetting);
  const setSlotValue = useEditorStore((state) => state.setSlotValue);
  const setSlotDefault = useEditorStore((state) => state.setSlotDefault);
  const setSlotConcreteType = useEditorStore((state) => state.setSlotConcreteType);
  const inputs = visibleSlots(definition, 'in');
  const outputs = visibleSlots(definition, 'out');
  const settingNames = useMemo(() => {
    const names = new Set(definition?.settings ?? []);
    for (const name of Object.keys(node.settings ?? {})) {
      names.add(name);
    }
    return Array.from(names).sort();
  }, [definition?.settings, node.settings]);
  const precision = typeof node.settings?.precision === 'string' ? node.settings.precision : 'inherit';

  return (
    <div className="inspector-section">
      {diagnostics.length > 0 && (
        <div className="node-diagnostics">
          {diagnostics.map((diagnostic, index) => (
            <div className={`diagnostic-line ${diagnostic.severity}`} key={`${diagnostic.code}-${index}`}>
              <span>{diagnostic.severity}</span>
              <span>{diagnostic.message}</span>
            </div>
          ))}
        </div>
      )}

      <label className="field-row">
        <span>Precision</span>
        <select
          value={precision}
          onChange={(event) => void setNodeSetting(nodeId, 'precision', event.target.value)}
        >
          <option value="inherit">Inherit</option>
          <option value="single">Single</option>
          <option value="half">Half</option>
        </select>
      </label>

      {settingNames.length > 0 && (
        <div className="inspector-subsection">
          <div className="inspector-subtitle">Node settings</div>
          {settingNames.map((name) => (
            <NodeSetting
              key={name}
              nodeId={nodeId}
              name={name}
              value={node.settings?.[name]}
              graph={graph}
            />
          ))}
        </div>
      )}

      <div className="inspector-subsection">
        <div className="inspector-subtitle">Inputs</div>
        {inputs.length === 0 && <div className="empty-row">没有输入</div>}
        {inputs.map((slot) => {
          const state = node.slots[String(slot.id)];
          const connected = state?.mode === 'connected';
          const source = graph ? incomingEdge(graph, nodeId, slot.id) : undefined;
          const sourceNode = source && graph ? findNode(graph, source.from.node) : undefined;
          const sourceDefinition = sourceNode ? useEditorStore.getState().catalogMap[sourceNode.type] : undefined;
          const sourceSlot = sourceDefinition?.slots.find((item) => item.id === source?.from.slot);
          const concrete = slotConcreteType(slot, state);
          return (
            <div className="slot-inspector-row" key={slot.id}>
              <div className="slot-inspector-label">
                <span className="type-dot" style={{ background: typeColor(concrete) }} />
                <span>{slot.displayName}</span>
                <span className="slot-type-label">{typeLabel(concrete)}</span>
              </div>
              {isDynamic(slot.type) && !connected && slot.allowedConcreteTypes.length > 0 && (
                <select
                  className="slot-concrete-select"
                  value={concrete}
                  onChange={(event) => void setSlotConcreteType(nodeId, slot.id, event.target.value)}
                >
                  {slot.allowedConcreteTypes.map((type) => (
                    <option key={type} value={type}>{typeLabel(type)}</option>
                  ))}
                </select>
              )}
              {connected ? (
                <div className="slot-connected-source">
                  {sourceNode ? `${sourceDefinition?.title || sourceNode.type} #${sourceNode.id}` : '已连接'}
                  {sourceSlot ? ` · ${sourceSlot.displayName}` : ''}
                </div>
              ) : slot.supportsLiteral ? (
                <div className="slot-value-editor">
                  <ValueEditor
                    value={effectiveSlotValue(slot, state)}
                    type={isDynamic(slot.type) ? concrete : slot.type}
                    onCommit={(value: JsonValue) => void setSlotValue(nodeId, slot.id, value, concrete)}
                  />
                  <button type="button" className="mini-button" onClick={() => void setSlotDefault(nodeId, slot.id)}>
                    默认
                  </button>
                </div>
              ) : (
                <div className="slot-default-value">{formatValue(slot.default) || '—'}</div>
              )}
            </div>
          );
        })}
      </div>

      <div className="inspector-subsection">
        <div className="inspector-subtitle">Outputs</div>
        {outputs.length === 0 && <div className="empty-row">没有输出</div>}
        {outputs.map((slot) => {
          const state = node.slots[String(slot.id)];
          const concrete = slotConcreteType(slot, state);
          return (
            <div className="slot-inspector-row output" key={slot.id}>
              <div className="slot-inspector-label">
                <span className="type-dot" style={{ background: typeColor(concrete) }} />
                <span>{slot.displayName}</span>
              </div>
              <span className="slot-type-label">{typeLabel(concrete)}</span>
            </div>
          );
        })}
      </div>
    </div>
  );
}

function NodeSetting({ nodeId, name, value, graph }: {
  nodeId: number;
  name: string;
  value: JsonValue | undefined;
  graph: ReturnType<typeof useEditorStore.getState>['graph'];
}) {
  const setNodeSetting = useEditorStore((state) => state.setNodeSetting);
  const properties = graph?.properties ?? [];
  const keywords = graph?.keywords ?? [];
  const propertyLike = name === 'property' || name === 'texture';
  const keywordLike = name === 'keyword';
  const options = propertyLike ? properties : keywordLike ? keywords : [];

  if (options.length > 0) {
    const selected = typeof value === 'string' ? value : '';
    return (
      <label className="field-row">
        <span>{name}</span>
        <select value={selected} onChange={(event) => void setNodeSetting(nodeId, name, event.target.value)}>
          <option value="">—</option>
          {options.map((option) => (
            <option key={option.refName} value={option.refName}>
              {option.name} ({option.refName})
            </option>
          ))}
        </select>
      </label>
    );
  }

  return (
    <label className="field-row">
      <span>{name}</span>
      <ValueEditor
        value={value}
        type={settingValueType(name, value)}
        onCommit={(next) => void setNodeSetting(nodeId, name, next)}
      />
    </label>
  );
}

function settingValueType(name: string, value: JsonValue | undefined): string {
  if (name === 'channel') {
    return 'integer';
  }
  if (name === 'gradient' || isGradientValue(value)) {
    return 'gradient';
  }
  if (typeof value === 'boolean') {
    return 'boolean';
  }
  if (typeof value === 'number') {
    return Number.isInteger(value) ? 'integer' : 'float';
  }
  if (Array.isArray(value)) {
    if (value.length === 4) {
      return 'vector4';
    }
    if (value.length === 3) {
      return 'vector3';
    }
    if (value.length === 2) {
      return 'vector2';
    }
  }
  return 'string';
}

function PropertyInspector({ property }: { property: Property }) {
  const setProperty = useEditorStore((state) => state.setProperty);
  const removeProperty = useEditorStore((state) => state.removeProperty);
  const [name, setName] = useState(property.name);
  useEffect(() => setName(property.name), [property.name]);

  return (
    <div className="inspector-section">
      <div className="inspector-heading">
        <span className="type-dot" style={{ background: typeColor(property.type) }} />
        <div>
          <div className="inspector-heading-title">{property.name}</div>
          <div className="inspector-heading-subtitle">{property.type} · {property.refName}</div>
        </div>
      </div>
      <label className="field-row">
        <span>Name</span>
        <input
          value={name}
          onChange={(event) => setName(event.target.value)}
          onBlur={() => {
            if (name.trim() && name !== property.name) {
              void setProperty(property.refName, 'name', name.trim());
            }
          }}
          onKeyDown={(event) => {
            if (event.key === 'Enter') {
              event.currentTarget.blur();
            }
          }}
        />
      </label>
      <label className="field-row">
        <span>Type</span>
        <input value={property.type} disabled />
      </label>
      <label className="field-row">
        <span>Default</span>
        <ValueEditor
          value={property.default}
          type={property.type}
          onCommit={(value) => void setProperty(property.refName, 'default', value)}
        />
      </label>
      <label className="field-row">
        <span>Category</span>
        <input
          defaultValue={property.category ?? 'Default'}
          onBlur={(event) => {
            const value = event.target.value.trim() || 'Default';
            if (value !== (property.category ?? 'Default')) {
              void setProperty(property.refName, 'category', value);
            }
          }}
        />
      </label>
      <label className="field-row">
        <span>Precision</span>
        <select
          value={property.precision}
          onChange={(event) => void setProperty(property.refName, 'precision', event.target.value)}
        >
          <option value="inherit">Inherit</option>
          <option value="single">Single</option>
          <option value="half">Half</option>
        </select>
      </label>
      <label className="field-row checkbox-row">
        <span>Exposed</span>
        <input
          type="checkbox"
          checked={property.exposed}
          onChange={(event) => void setProperty(property.refName, 'exposed', event.target.checked)}
        />
      </label>
      <button
        type="button"
        className="danger-button"
        onClick={() => {
          if (window.confirm(`删除属性 ${property.name}？引用它的 Property 节点也会被删除。`)) {
            void removeProperty(property.refName);
          }
        }}
      >
        删除属性
      </button>
    </div>
  );
}

function KeywordInspector({ keyword }: { keyword: Keyword }) {
  const removeKeyword = useEditorStore((state) => state.removeKeyword);
  return (
    <div className="inspector-section">
      <div className="inspector-heading">
        <span className="type-dot keyword" />
        <div>
          <div className="inspector-heading-title">{keyword.name}</div>
          <div className="inspector-heading-subtitle">{keyword.type} · {keyword.refName}</div>
        </div>
      </div>
      <label className="field-row">
        <span>Default</span>
        <input value={String(keyword.default)} disabled />
      </label>
      <label className="field-row">
        <span>Definition</span>
        <input value={keyword.definition} disabled />
      </label>
      <label className="field-row">
        <span>Scope</span>
        <input value={keyword.scope} disabled />
      </label>
      <div className="inspector-subsection">
        <div className="inspector-subtitle">Entries</div>
        {(keyword.entries ?? []).map((entry) => (
          <div className="keyword-entry" key={entry.id}>
            <span>{entry.id}</span>
            <span>{entry.displayName}</span>
            <span>{entry.referenceName}</span>
          </div>
        ))}
        {(keyword.entries?.length ?? 0) === 0 && <div className="empty-row">没有枚举项</div>}
      </div>
      <button
        type="button"
        className="danger-button"
        onClick={() => {
          if (window.confirm(`删除关键字 ${keyword.name}？`)) {
            void removeKeyword(keyword.refName);
          }
        }}
      >
        删除关键字
      </button>
    </div>
  );
}

function GraphInspector() {
  const graph = useEditorStore((state) => state.graph);
  const document = useEditorStore((state) => state.document);
  const setTargetSetting = useEditorStore((state) => state.setTargetSetting);
  if (!graph) {
    return <div className="empty-row">没有打开的图</div>;
  }
  return (
    <div className="inspector-section">
      <div className="inspector-heading">
        <span className="type-dot" style={{ background: '#7c8b9d' }} />
        <div>
          <div className="inspector-heading-title">{graph.name || 'Untitled Graph'}</div>
          <div className="inspector-heading-subtitle">Target · {graph.target.id}/{graph.target.subTarget}</div>
        </div>
      </div>
      <div className="inspector-subsection">
        <div className="inspector-subtitle">Target settings</div>
        {Object.entries(graph.target.settings).map(([name, value]) => (
          <label className="field-row" key={name}>
            <span>{name}</span>
            <ValueEditor
              value={value}
              type={settingValueType(name, value)}
              onCommit={(next) => void setTargetSetting(name, next)}
            />
          </label>
        ))}
      </div>
      <div className="inspector-subsection">
        <div className="inspector-subtitle">Available targets</div>
        {(document?.targets ?? []).map((target) => (
          <div className={`target-row ${target.available ? '' : 'disabled'}`} key={target.id}>
            <span>{target.id}</span>
            <span>{target.available ? 'available' : target.reason || 'unavailable'}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
