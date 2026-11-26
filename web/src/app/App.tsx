import { useEffect, useRef, useState } from 'react';
import { ReactFlowProvider } from '@xyflow/react';
import { useEditorStore } from '../state/editor';
import { Toolbar } from './Toolbar';
import { GraphCanvas } from '../graph/GraphCanvas';
import { Blackboard } from '../panels/Blackboard';
import { Inspector } from '../panels/Inspector';
import { Preview } from '../panels/Preview';
import { Diagnostics } from '../panels/Diagnostics';
import { SearchPalette } from '../panels/SearchPalette';

export function App() {
  const status = useEditorStore((state) => state.status);
  const error = useEditorStore((state) => state.error);
  const token = useEditorStore((state) => state.token);
  const toast = useEditorStore((state) => state.toast);
  const initialize = useEditorStore((state) => state.initialize);
  const disconnectSSE = useEditorStore((state) => state.disconnectSSE);
  const setToken = useEditorStore((state) => state.setToken);
  const clearToast = useEditorStore((state) => state.clearToast);
  const [tokenDraft, setTokenDraft] = useState(token);
  const started = useRef(false);

  useEffect(() => {
    if (started.current) {
      return;
    }
    started.current = true;
    void initialize();
    return () => disconnectSSE();
  }, [disconnectSSE, initialize]);

  if (status === 'error' && !token) {
    return (
      <div className="token-gate">
        <div className="token-card">
          <div className="brand-mark large">SG</div>
          <h1>Shader Graph Editor</h1>
          <p>输入宿主启动日志中的一次性 token。也可以直接打开带 <code>?token=</code> 的 URL。</p>
          <form onSubmit={(event) => {
            event.preventDefault();
            if (tokenDraft.trim()) {
              setToken(tokenDraft.trim());
              void initialize();
            }
          }}>
            <input
              autoFocus
              value={tokenDraft}
              onChange={(event) => setTokenDraft(event.target.value)}
              placeholder="32 位 hex token"
            />
            <button type="submit">连接宿主</button>
          </form>
          {error && <div className="gate-error">{error}</div>}
        </div>
      </div>
    );
  }

  if (status === 'loading' || status === 'idle') {
    return <div className="loading-screen"><div className="spinner" />正在连接 Shader Graph 宿主…</div>;
  }

  if (status === 'error') {
    return (
      <div className="loading-screen error-screen">
        <strong>连接失败</strong>
        <span>{error}</span>
        <button type="button" onClick={() => void initialize()}>重试</button>
      </div>
    );
  }

  return (
    <div className="app-shell">
      <Toolbar />
      <div className="workspace">
        <Blackboard />
        <ReactFlowProvider>
          <GraphCanvas />
        </ReactFlowProvider>
        <div className="right-column">
          <Inspector />
          <Preview />
        </div>
      </div>
      <Diagnostics />
      <SearchPalette />
      {toast && (
        <button type="button" className={`toast ${toast.kind}`} onClick={clearToast}>
          {toast.text}
        </button>
      )}
    </div>
  );
}
