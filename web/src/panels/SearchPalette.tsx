import { useEffect, useMemo, useRef, useState } from 'react';
import { useEditorStore } from '../state/editor';

export function SearchPalette() {
  const open = useEditorStore((state) => state.searchOpen);
  const position = useEditorStore((state) => state.searchPosition);
  const catalog = useEditorStore((state) => state.catalog);
  const graph = useEditorStore((state) => state.graph);
  const close = useEditorStore((state) => state.closeSearch);
  const addNode = useEditorStore((state) => state.addNode);
  const [query, setQuery] = useState('');
  const [selected, setSelected] = useState(0);
  const inputRef = useRef<HTMLInputElement | null>(null);

  const target = graph?.target.id ?? '';
  const entries = useMemo(() => {
    const normalized = query.trim().toLowerCase();
    const targetFiltered = catalog.filter((node) => {
      if (graph?.kind === 'subgraph' && !node.allowInSubGraph) {
        return false;
      }
      if (graph?.kind !== 'subgraph' && !node.allowInMainGraph) {
        return false;
      }
      if (node.targets.length > 0 && target && !node.targets.includes(target)) {
        return false;
      }
      return true;
    });
    const list = normalized
      ? targetFiltered.filter((node) =>
          node.title.toLowerCase().includes(normalized) ||
          node.type.toLowerCase().includes(normalized) ||
          node.category.toLowerCase().includes(normalized) ||
          node.synonyms.some((synonym) => synonym.toLowerCase().includes(normalized)))
      : targetFiltered;
    return list.slice(0, 80);
  }, [catalog, graph?.kind, query, target]);

  useEffect(() => {
    if (!open) {
      return;
    }
    setQuery('');
    setSelected(0);
    window.setTimeout(() => inputRef.current?.focus(), 0);
  }, [open]);

  useEffect(() => {
    setSelected((current) => Math.min(current, Math.max(0, entries.length - 1)));
  }, [entries.length]);

  if (!open) {
    return null;
  }

  const choose = (index: number) => {
    const entry = entries[index];
    if (!entry) {
      return;
    }
    void addNode(entry.type, position);
    close();
  };

  return (
    <div className="palette-backdrop" onMouseDown={(event) => {
      if (event.target === event.currentTarget) {
        close();
      }
    }}>
      <div className="search-palette">
        <input
          ref={inputRef}
          className="search-input"
          placeholder="搜索节点（标题、分类、同义词）"
          value={query}
          onChange={(event) => {
            setQuery(event.target.value);
            setSelected(0);
          }}
          onKeyDown={(event) => {
            if (event.key === 'ArrowDown') {
              event.preventDefault();
              setSelected((current) => Math.min(entries.length - 1, current + 1));
            } else if (event.key === 'ArrowUp') {
              event.preventDefault();
              setSelected((current) => Math.max(0, current - 1));
            } else if (event.key === 'Enter') {
              event.preventDefault();
              choose(selected);
            } else if (event.key === 'Escape') {
              event.preventDefault();
              close();
            }
          }}
        />
        <div className="search-results">
          {entries.map((entry, index) => (
            <button
              type="button"
              key={entry.type}
              className={`search-result ${index === selected ? 'selected' : ''}`}
              onMouseEnter={() => setSelected(index)}
              onClick={() => choose(index)}
            >
              <span className="search-result-title">{entry.title}</span>
              <span className="search-result-category">{entry.category}</span>
              {entry.blockNode && <span className="search-badge">Block</span>}
              {entry.hasPreview && <span className="search-badge preview">Preview</span>}
            </button>
          ))}
          {entries.length === 0 && <div className="empty-row">没有匹配节点</div>}
        </div>
        <div className="search-hint">↑↓ 选择 · Enter 创建 · Esc 关闭</div>
      </div>
    </div>
  );
}
