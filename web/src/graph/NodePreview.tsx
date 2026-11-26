import { useEffect, useRef, useState } from 'react';
import { api } from '../api/client';

const previewCache = new Map<string, string>();

interface NodePreviewProps {
  nodeId: number;
  revision: number;
  enabled: boolean;
}

export function NodePreview({ nodeId, revision, enabled }: NodePreviewProps) {
  const hostRef = useRef<HTMLDivElement | null>(null);
  const [visible, setVisible] = useState(false);
  const [url, setUrl] = useState('');
  const [failed, setFailed] = useState(false);
  const key = `${nodeId}:${revision}`;

  useEffect(() => {
    const cached = previewCache.get(key);
    setUrl(cached ?? '');
    setFailed(false);
    if (cached) {
      return;
    }
    const element = hostRef.current;
    if (!element || typeof IntersectionObserver === 'undefined') {
      setVisible(true);
      return;
    }
    const observer = new IntersectionObserver((entries) => {
      if (entries.some((entry) => entry.isIntersecting)) {
        setVisible(true);
        observer.disconnect();
      }
    }, { rootMargin: '120px' });
    observer.observe(element);
    return () => observer.disconnect();
  }, [key]);

  useEffect(() => {
    if (!enabled || !visible || !nodeId || previewCache.has(key)) {
      return;
    }
    let cancelled = false;
    let timer = 0;
    let attempts = 0;

    const load = async () => {
      try {
        const result = await api.nodePreview(nodeId, revision, 128);
        if (cancelled) {
          return;
        }
        if (result === 'pending') {
          attempts += 1;
          if (attempts < 40) {
            timer = window.setTimeout(load, 350);
          } else {
            setFailed(true);
          }
          return;
        }
        const objectUrl = URL.createObjectURL(result);
        previewCache.set(key, objectUrl);
        setUrl(objectUrl);
      } catch {
        if (!cancelled) {
          setFailed(true);
        }
      }
    };

    void load();
    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, [enabled, key, nodeId, revision, visible]);


  return (
    <div className="node-preview" ref={hostRef}>
      {url ? (
        <img src={url} alt="" draggable={false} />
      ) : (
        <div className={failed ? 'node-preview-placeholder failed' : 'node-preview-placeholder'}>
          {failed ? '×' : enabled ? '…' : ''}
        </div>
      )}
    </div>
  );
}
