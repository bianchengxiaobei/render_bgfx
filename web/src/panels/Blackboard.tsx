import { useMemo, useState } from 'react';
import type { Keyword, Property } from '../api/types';
import { useEditorStore } from '../state/editor';
import { formatValue, typeColor } from '../graph/model';

const PROPERTY_TYPES = [
  'float', 'integer', 'vector2', 'vector3', 'vector4', 'color',
  'texture2d', 'texture2darray', 'texture3d', 'cubemap', 'boolean',
];

const KEYWORD_TYPES = ['boolean', 'enum'] as const;

function uniqueName(prefix: string, existing: string[]): string {
  if (!existing.includes(prefix)) {
    return prefix;
  }
  let index = 2;
  while (existing.includes(`${prefix}${index}`)) {
    index += 1;
  }
  return `${prefix}${index}`;
}

export function Blackboard() {
  const graph = useEditorStore((state) => state.graph);
  const selection = useEditorStore((state) => state.selection);
  const setSelection = useEditorStore((state) => state.setSelection);
  const addProperty = useEditorStore((state) => state.addProperty);
  const removeProperty = useEditorStore((state) => state.removeProperty);
  const renameProperty = useEditorStore((state) => state.renameProperty);
  const addKeyword = useEditorStore((state) => state.addKeyword);
  const removeKeyword = useEditorStore((state) => state.removeKeyword);
  const addNode = useEditorStore((state) => state.addNode);
  const [propertyType, setPropertyType] = useState('float');
  const [keywordType, setKeywordType] = useState<'boolean' | 'enum'>('boolean');
  const [filter, setFilter] = useState('');

  const properties = useMemo(() => {
    const query = filter.trim().toLowerCase();
    const list = graph?.properties ?? [];
    return query
      ? list.filter((item) => item.name.toLowerCase().includes(query) || item.refName.toLowerCase().includes(query))
      : list;
  }, [filter, graph?.properties]);

  const keywords = useMemo(() => {
    const query = filter.trim().toLowerCase();
    const list = graph?.keywords ?? [];
    return query
      ? list.filter((item) => item.name.toLowerCase().includes(query) || item.refName.toLowerCase().includes(query))
      : list;
  }, [filter, graph?.keywords]);

  const categories = useMemo(() => {
    const grouped = new Map<string, Property[]>();
    for (const property of properties) {
      const category = property.category || 'Default';
      const list = grouped.get(category) ?? [];
      list.push(property);
      grouped.set(category, list);
    }
    return Array.from(grouped.entries()).sort(([left], [right]) => left.localeCompare(right));
  }, [properties]);

  const createProperty = () => {
    const names = (graph?.properties ?? []).map((property) => property.refName);
    const base = uniqueName('_NewProperty', names);
    void addProperty(base, propertyType);
  };

  const createKeyword = () => {
    const names = (graph?.keywords ?? []).map((keyword) => keyword.refName);
    const base = uniqueName('_NewKeyword', names);
    void addKeyword(base, keywordType);
  };

  return (
    <aside className="panel blackboard">
      <div className="panel-title">
        <span>Blackboard</span>
        <span className="panel-count">{graph?.properties.length ?? 0} / {graph?.keywords.length ?? 0}</span>
      </div>
      <input
        className="panel-filter"
        placeholder="过滤属性/关键字"
        value={filter}
        onChange={(event) => setFilter(event.target.value)}
      />

      <div className="blackboard-add">
        <select value={propertyType} onChange={(event) => setPropertyType(event.target.value)}>
          {PROPERTY_TYPES.map((type) => <option key={type} value={type}>{type}</option>)}
        </select>
        <button type="button" onClick={createProperty}>属性</button>
      </div>

      <div className="blackboard-scroll">
        {categories.map(([category, items]) => (
          <section className="blackboard-category" key={category}>
            <div className="blackboard-category-title">{category}</div>
            {items.map((property) => (
              <PropertyRow
                key={property.refName}
                property={property}
                selected={selection.property === property.refName}
                onSelect={() => setSelection({ nodes: [], edges: [], property: property.refName })}
                onRemove={() => {
                  const affected = graph?.nodes.filter((node) =>
                    node.type === 'Property' && node.settings.property === property.refName).length ?? 0;
                  if (affected > 0 && !window.confirm(`删除属性会影响 ${affected} 个 Property 节点，继续？`)) {
                    return;
                  }
                  void removeProperty(property.refName);
                }}
                onRename={(name) => {
                  if (name.trim() && name !== property.name) {
                    void renameProperty(property.refName, name.trim());
                  }
                }}
                onDrag={(event) => {
                  event.dataTransfer.setData('application/x-sg-property', property.refName);
                  event.dataTransfer.effectAllowed = 'copy';
                }}
                onAddToGraph={() => {
                  const node = graph?.nodes.find((item) => item.type === 'Property' && item.settings.property === property.refName);
                  if (node) {
                    setSelection({ nodes: [node.id], edges: [] });
                  } else {
                    void addNode('Property', { x: 120, y: 120 }, { property: property.refName });
                  }
                }}
              />
            ))}
          </section>
        ))}

        <section className="blackboard-category">
          <div className="blackboard-category-title">
            Keywords
            <div className="blackboard-keyword-add">
              <select value={keywordType} onChange={(event) => setKeywordType(event.target.value as 'boolean' | 'enum')}>
                {KEYWORD_TYPES.map((type) => <option key={type} value={type}>{type}</option>)}
              </select>
              <button type="button" onClick={createKeyword}>+</button>
            </div>
          </div>
          {keywords.map((keyword) => (
            <KeywordRow
              key={keyword.refName}
              keyword={keyword}
              selected={selection.keyword === keyword.refName}
              onSelect={() => setSelection({ nodes: [], edges: [], keyword: keyword.refName })}
              onRemove={() => {
                if (window.confirm(`删除关键字 ${keyword.name}？`)) {
                  void removeKeyword(keyword.refName);
                }
              }}
              onDrag={(event) => {
                event.dataTransfer.setData('application/x-sg-keyword', keyword.refName);
                event.dataTransfer.effectAllowed = 'copy';
              }}
              onAddToGraph={() => {
                const node = graph?.nodes.find((item) => item.type === 'Keyword' && item.settings.keyword === keyword.refName);
                if (node) {
                  setSelection({ nodes: [node.id], edges: [] });
                } else {
                  void addNode('Keyword', { x: 120, y: 120 }, { keyword: keyword.refName });
                }
              }}
            />
          ))}
          {keywords.length === 0 && <div className="empty-row">暂无关键字</div>}
        </section>

        {properties.length === 0 && <div className="empty-row">暂无属性，先添加一个。</div>}
      </div>
    </aside>
  );
}

interface PropertyRowProps {
  property: Property;
  selected: boolean;
  onSelect: () => void;
  onRemove: () => void;
  onRename: (name: string) => void;
  onDrag: (event: React.DragEvent) => void;
  onAddToGraph: () => void;
}

function PropertyRow({ property, selected, onSelect, onRemove, onRename, onDrag, onAddToGraph }: PropertyRowProps) {
  const [editing, setEditing] = useState(false);
  const [name, setName] = useState(property.name);
  const color = typeColor(property.type);

  return (
    <div
      className={`blackboard-row ${selected ? 'selected' : ''}`}
      draggable
      onDragStart={onDrag}
      onClick={onSelect}
      onDoubleClick={(event) => {
        event.stopPropagation();
        setName(property.name);
        setEditing(true);
      }}
      title={`${property.name} (${property.type}) ${property.refName}`}
    >
      <span className="type-dot" style={{ background: color }} />
      <div className="blackboard-row-main">
        {editing ? (
          <input
            autoFocus
            value={name}
            onChange={(event) => setName(event.target.value)}
            onBlur={() => {
              setEditing(false);
              onRename(name);
            }}
            onKeyDown={(event) => {
              if (event.key === 'Enter') {
                event.currentTarget.blur();
              }
              if (event.key === 'Escape') {
                setName(property.name);
                setEditing(false);
              }
            }}
            onClick={(event) => event.stopPropagation()}
          />
        ) : (
          <span className="blackboard-row-name">{property.name}</span>
        )}
        <span className="blackboard-row-type">{property.type}</span>
      </div>
      <span className="blackboard-row-value">{formatValue(property.default)}</span>
      <button
        type="button"
        className="row-action"
        title="选中画布中的 Property 节点"
        onClick={(event) => {
          event.stopPropagation();
          onAddToGraph();
        }}
      >
        +
      </button>
      <button
        type="button"
        className="row-action danger"
        title="删除属性"
        onClick={(event) => {
          event.stopPropagation();
          onRemove();
        }}
      >
        ×
      </button>
    </div>
  );
}

interface KeywordRowProps {
  keyword: Keyword;
  selected: boolean;
  onSelect: () => void;
  onRemove: () => void;
  onDrag: (event: React.DragEvent) => void;
  onAddToGraph: () => void;
}

function KeywordRow({ keyword, selected, onSelect, onRemove, onDrag, onAddToGraph }: KeywordRowProps) {
  return (
    <div
      className={`blackboard-row ${selected ? 'selected' : ''}`}
      draggable
      onDragStart={onDrag}
      onClick={onSelect}
      title={`${keyword.name} (${keyword.type})`}
    >
      <span className="type-dot keyword" />
      <div className="blackboard-row-main">
        <span className="blackboard-row-name">{keyword.name}</span>
        <span className="blackboard-row-type">{keyword.type}</span>
      </div>
      <span className="blackboard-row-value">{keyword.entries?.length ?? 0} entries</span>
      <button
        type="button"
        className="row-action"
        title="选中或创建 Keyword 节点"
        onClick={(event) => {
          event.stopPropagation();
          onAddToGraph();
        }}
      >
        +
      </button>
      <button
        type="button"
        className="row-action danger"
        title="删除关键字"
        onClick={(event) => {
          event.stopPropagation();
          onRemove();
        }}
      >
        ×
      </button>
    </div>
  );
}
