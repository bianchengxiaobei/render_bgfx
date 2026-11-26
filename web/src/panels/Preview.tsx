import { useEffect, useMemo, useState } from 'react';
import { imageUrl } from '../api/client';
import type { PreviewBackground, PreviewChannel, PreviewMesh, PreviewOptions } from '../api/types';
import { useEditorStore } from '../state/editor';

const DEFAULT_OPTIONS: PreviewOptions = {
  mesh: 'sphere',
  size: 512,
  background: 'checker',
  channel: 'rgb',
  yaw: 0,
  pitch: 0,
  distance: 0,
};

export function Preview() {
  const graph = useEditorStore((state) => state.graph);
  const revision = useEditorStore((state) => state.revision);
  const previewEpoch = useEditorStore((state) => state.previewEpoch);
  const capabilities = useEditorStore((state) => state.document?.capabilities);
  const compile = useEditorStore((state) => state.compile);
  const showPreviews = useEditorStore((state) => state.showPreviews);
  const setShowPreviews = useEditorStore((state) => state.setShowPreviews);
  const [options, setOptions] = useState<PreviewOptions>(DEFAULT_OPTIONS);
  const [mode, setMode] = useState<'stream' | 'frame'>('stream');
  const [nonce, setNonce] = useState(0);
  const [failed, setFailed] = useState(false);
  const [visible, setVisible] = useState(() => document.visibilityState !== 'hidden');

  useEffect(() => {
    const onVisibility = () => setVisible(document.visibilityState !== 'hidden');
    document.addEventListener('visibilitychange', onVisibility);
    return () => document.removeEventListener('visibilitychange', onVisibility);
  }, []);

  useEffect(() => {
    if (mode !== 'frame' || !visible) {
      return;
    }
    const timer = window.setInterval(() => setNonce((value) => value + 1), 800);
    return () => window.clearInterval(timer);
  }, [mode, visible]);

  useEffect(() => {
    setFailed(false);
    setNonce((value) => value + 1);
  }, [previewEpoch, revision]);

  const params = useMemo(() => ({
    mesh: options.mesh,
    size: options.size,
    background: options.background,
    channel: options.channel,
    yaw: options.yaw,
    pitch: options.pitch,
    distance: options.distance || undefined,
    rev: revision,
    fps: 10,
    vt: `${previewEpoch}-${nonce}`,
  }), [nonce, options, previewEpoch, revision]);

  const source = mode === 'stream'
    ? imageUrl('/api/preview/stream', params)
    : imageUrl('/api/preview.jpg', params);
  const enabled = graph !== null && capabilities?.preview !== false;
  const ready = compile.state === 'succeeded';

  const update = <K extends keyof PreviewOptions>(key: K, value: PreviewOptions[K]) => {
    setOptions((current) => ({ ...current, [key]: value }));
    setNonce((current) => current + 1);
    setFailed(false);
  };

  return (
    <section className="panel preview-panel">
      <div className="panel-title">
        <span>Main Preview</span>
        <span className={`preview-mode ${mode}`}>{mode === 'stream' ? 'MJPEG' : 'frame'}</span>
      </div>

      <div className="preview-controls">
        <label>
          <span>Mesh</span>
          <select value={options.mesh} onChange={(event) => update('mesh', event.target.value as PreviewMesh)}>
            {(['sphere', 'plane', 'cube', 'quad'] as PreviewMesh[]).map((mesh) => (
              <option key={mesh} value={mesh}>{mesh}</option>
            ))}
          </select>
        </label>
        <label>
          <span>Size</span>
          <select
            value={options.size}
            onChange={(event) => update('size', Number(event.target.value))}
          >
            {[256, 512, 1024].map((size) => <option key={size} value={size}>{size}²</option>)}
          </select>
        </label>
        <label>
          <span>BG</span>
          <select
            value={options.background}
            onChange={(event) => update('background', event.target.value as PreviewBackground)}
          >
            {(['checker', 'black', 'gray', 'sky'] as PreviewBackground[]).map((background) => (
              <option key={background} value={background}>{background}</option>
            ))}
          </select>
        </label>
        <label>
          <span>Channel</span>
          <select value={options.channel} onChange={(event) => update('channel', event.target.value as PreviewChannel)}>
            {(['rgb', 'r', 'g', 'b', 'a'] as PreviewChannel[]).map((channel) => (
              <option key={channel} value={channel}>{channel}</option>
            ))}
          </select>
        </label>
      </div>

      <div className="preview-stage">
        {!visible ? (
          <div className="preview-placeholder">标签页隐藏，预览已暂停</div>
        ) : !enabled ? (
          <div className="preview-placeholder">预览不可用</div>
        ) : !ready ? (
          <div className="preview-placeholder">暂无预览帧，请先编译</div>
        ) : failed && mode === 'frame' ? (
          <div className="preview-placeholder">暂无预览帧，请先编译</div>
        ) : (
          <img
            key={source}
            src={source}
            alt="Shader preview"
            draggable={false}
            onError={() => {
              if (mode === 'stream') {
                setMode('frame');
                setFailed(false);
              } else {
                setFailed(true);
              }
            }}
          />
        )}
        <div className="preview-status">
          <span>r{revision}</span>
          <span className={compile.state}>{compile.message}</span>
        </div>
      </div>

      <div className="preview-orbit">
        <label>
          <span>Yaw {Math.round(options.yaw)}°</span>
          <input
            type="range"
            min={-180}
            max={180}
            step={1}
            value={options.yaw}
            onChange={(event) => update('yaw', Number(event.target.value))}
          />
        </label>
        <label>
          <span>Pitch {Math.round(options.pitch)}°</span>
          <input
            type="range"
            min={-85}
            max={85}
            step={1}
            value={options.pitch}
            onChange={(event) => update('pitch', Number(event.target.value))}
          />
        </label>
        <label>
          <span>Distance {options.distance ? options.distance.toFixed(1) : 'auto'}</span>
          <input
            type="range"
            min={0}
            max={10}
            step={0.1}
            value={options.distance}
            onChange={(event) => update('distance', Number(event.target.value))}
          />
        </label>
      </div>

      <label className="preview-toggle">
        <input
          type="checkbox"
          checked={showPreviews}
          onChange={(event) => setShowPreviews(event.target.checked)}
        />
        节点预览缩略图
      </label>
    </section>
  );
}
