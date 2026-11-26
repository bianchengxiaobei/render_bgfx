import { useMemo } from 'react';
import { useEditorStore } from '../state/editor';

const SEVERITY_ORDER = { error: 0, warning: 1, info: 2 } as const;

export function Diagnostics() {
  const diagnostics = useEditorStore((state) => state.diagnostics);
  const compile = useEditorStore((state) => state.compile);
  const setFocusNode = useEditorStore((state) => state.setFocusNode);
  const setSelection = useEditorStore((state) => state.setSelection);

  const ordered = useMemo(() => [...diagnostics].sort((left, right) => {
    const severity = SEVERITY_ORDER[left.severity] - SEVERITY_ORDER[right.severity];
    if (severity !== 0) {
      return severity;
    }
    return (left.nodeId ?? -1) - (right.nodeId ?? -1);
  }), [diagnostics]);

  const errors = diagnostics.filter((item) => item.severity === 'error').length;
  const warnings = diagnostics.filter((item) => item.severity === 'warning').length;

  return (
    <section className="panel diagnostics">
      <div className="panel-title">
        <span>Diagnostics</span>
        <span className="diagnostic-summary">
          <span className="error-count">{errors} errors</span>
          <span className="warning-count">{warnings} warnings</span>
          <span className={`compile-chip ${compile.state}`}>{compile.message}</span>
        </span>
      </div>
      <div className="diagnostic-list">
        {ordered.length === 0 && <div className="empty-row">没有诊断信息</div>}
        {ordered.map((diagnostic, index) => (
          <button
            type="button"
            className={`diagnostic-row ${diagnostic.severity}`}
            key={`${diagnostic.code}-${diagnostic.nodeId ?? 'x'}-${index}`}
            onClick={() => {
              if (diagnostic.nodeId !== undefined) {
                setFocusNode(diagnostic.nodeId);
                setSelection({ nodes: [diagnostic.nodeId], edges: [] });
              }
            }}
          >
            <span className="diagnostic-severity">{diagnostic.severity}</span>
            <span className="diagnostic-code">{diagnostic.code}</span>
            <span className="diagnostic-message">{diagnostic.message}</span>
            <span className="diagnostic-location">
              {diagnostic.nodeId !== undefined ? `node ${diagnostic.nodeId}` : diagnostic.pass || ''}
              {diagnostic.slotId !== undefined ? ` / slot ${diagnostic.slotId}` : ''}
            </span>
          </button>
        ))}
      </div>
    </section>
  );
}
