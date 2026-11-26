import { useEffect } from 'react';
import { useEditorStore } from '../state/editor';

export function Toolbar() {
  const graph = useEditorStore((state) => state.graph);
  const documentPath = useEditorStore((state) => state.document?.path ?? '');
  const dirty = useEditorStore((state) => state.dirty);
  const revision = useEditorStore((state) => state.revision);
  const compile = useEditorStore((state) => state.compile);
  const inFlight = useEditorStore((state) => state.inFlight);
  const session = useEditorStore((state) => state.session);
  const save = useEditorStore((state) => state.save);
  const saveAs = useEditorStore((state) => state.saveAs);
  const open = useEditorStore((state) => state.open);
  const compileAction = useEditorStore((state) => state.runCompile);
  const undo = useEditorStore((state) => state.undo);
  const redo = useEditorStore((state) => state.redo);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' ||
          target.tagName === 'SELECT' || target.isContentEditable)) {
        return;
      }
      if (!(event.ctrlKey || event.metaKey)) {
        return;
      }
      if (event.key.toLowerCase() === 's') {
        event.preventDefault();
        void save();
      } else if (event.key.toLowerCase() === 'b') {
        event.preventDefault();
        void compileAction();
      } else if (event.key.toLowerCase() === 'z') {
        event.preventDefault();
        if (event.shiftKey) {
          void redo();
        } else {
          void undo();
        }
      } else if (event.key.toLowerCase() === 'y') {
        event.preventDefault();
        void redo();
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [compileAction, redo, save, undo]);

  const openDocument = () => {
    const path = window.prompt('打开图（相对 --sg-root）', documentPath);
    if (path) {
      void open(path);
    }
  };

  const saveDocumentAs = () => {
    const path = window.prompt('另存为（相对 --sg-root）', documentPath);
    if (path) {
      void saveAs(path);
    }
  };

  return (
    <header className="toolbar">
      <div className="toolbar-brand">
        <span className="brand-mark">SG</span>
        <div>
          <div className="brand-title">Shader Graph</div>
          <div className="brand-subtitle">native editor</div>
        </div>
      </div>

      <div className="toolbar-document">
        <span className="document-name" title={documentPath}>{documentPath || '未打开文档'}</span>
        {dirty && <span className="dirty-dot" title="有未保存修改" />}
        <span className="revision">r{revision}</span>
        {graph && <span className="target-chip">{graph.target.id}</span>}
      </div>

      <div className="toolbar-actions">
        <button type="button" onClick={openDocument} disabled={inFlight}>打开</button>
        <button type="button" onClick={() => void save()} disabled={inFlight || !graph}>保存</button>
        <button type="button" onClick={saveDocumentAs} disabled={inFlight || !graph}>另存为</button>
        <span className="toolbar-separator" />
        <button
          type="button"
          onClick={() => void undo()}
          disabled={inFlight || session?.capabilities.undo === false}
          title="撤销 (Ctrl+Z)"
        >
          撤销
        </button>
        <button
          type="button"
          onClick={() => void redo()}
          disabled={inFlight || session?.capabilities.redo === false}
          title="重做 (Ctrl+Shift+Z)"
        >
          重做
        </button>
        <span className="toolbar-separator" />
        <button
          type="button"
          className="primary"
          onClick={() => void compileAction()}
          disabled={inFlight || !graph || compile.state === 'queued' || compile.state === 'running'}
          title="编译预览 (Ctrl+B)"
        >
          {compile.state === 'queued' || compile.state === 'running' ? '编译中…' : '编译'}
        </button>
        <span className={`compile-state ${compile.state}`}>{compile.message}</span>
      </div>
    </header>
  );
}
