import { useEffect, useState } from 'react';
import {
  Link,
  NavLink,
  Route,
  Routes,
  useLocation,
  useNavigate,
  useSearchParams,
} from 'react-router-dom';
import IndexPage from './pages/IndexPage';
import ProgramPage from './pages/ProgramPage';
import VariantPage from './pages/VariantPage';
import DiffPage from './pages/DiffPage';
import UploadPage from './pages/UploadPage';
import type { ReportDataset } from './types';
import { useUpload } from './upload';

function DatasetLink({ dataset, label }: { dataset: ReportDataset; label: string }) {
  const [searchParams] = useSearchParams();
  const location = useLocation();
  const current = searchParams.get('dataset') === 'reference' ? 'reference' : 'reports';
  const active =
    location.pathname !== '/diff' &&
    location.pathname !== '/upload' &&
    current === dataset;
  return (
    <Link
      to={`/?dataset=${dataset}`}
      className={active ? 'nav-link active' : 'nav-link'}
    >
      {label}
    </Link>
  );
}

function NotFoundPage() {
  return (
    <section className="panel empty-state">
      <p className="eyebrow">404</p>
      <h1>Page not found</h1>
      <p>The report browser route does not exist.</p>
      <Link className="button" to="/">
        Back to reports
      </Link>
    </section>
  );
}

export default function App() {
  const navigate = useNavigate();
  const { loadFile, loadSource } = useUpload();
  const [dragging, setDragging] = useState(false);

  useEffect(() => {
    const hasFiles = (event: DragEvent) =>
      Array.from(event.dataTransfer?.types ?? []).includes('Files');
    const onDragOver = (event: DragEvent) => {
      if (!hasFiles(event)) {
        return;
      }
      event.preventDefault();
      if (event.dataTransfer) {
        event.dataTransfer.dropEffect = 'copy';
      }
      setDragging(true);
    };
    const onDragLeave = (event: DragEvent) => {
      if (event.relatedTarget === null) {
        setDragging(false);
      }
    };
    const onDrop = async (event: DragEvent) => {
      if (!hasFiles(event)) {
        return;
      }
      event.preventDefault();
      setDragging(false);
      const file = event.dataTransfer?.files?.[0];
      if (!file) {
        return;
      }
      if (
        file.name.toLowerCase().endsWith('.json') ||
        file.type === 'application/json'
      ) {
        if (await loadFile(file)) {
          navigate('/upload');
        }
      } else {
        await loadSource(file);
        navigate('/upload');
      }
    };
    const reset = () => setDragging(false);
    window.addEventListener('dragover', onDragOver);
    window.addEventListener('dragleave', onDragLeave);
    window.addEventListener('drop', onDrop);
    window.addEventListener('dragend', reset);
    return () => {
      window.removeEventListener('dragover', onDragOver);
      window.removeEventListener('dragleave', onDragLeave);
      window.removeEventListener('drop', onDrop);
      window.removeEventListener('dragend', reset);
    };
  }, [loadFile, loadSource, navigate]);

  return (
    <div className="app-shell">
      <header className="topbar">
        <Link className="brand" to="/">
          <span className="brand-mark">HBG</span>
          <span>
            <strong>Shader Analysis</strong>
            <small>DXBC / SPIR-V report browser</small>
          </span>
        </Link>
        <nav className="topnav" aria-label="Datasets">
          <DatasetLink dataset="reports" label="Reports" />
          <DatasetLink dataset="reference" label="Reference" />
          <NavLink
            to="/diff"
            className={({ isActive }) => (isActive ? 'nav-link active' : 'nav-link')}
          >
            Diff
          </NavLink>
          <NavLink
            to="/upload"
            className={({ isActive }) => (isActive ? 'nav-link active' : 'nav-link')}
          >
            Upload
          </NavLink>
        </nav>
      </header>

      <main className="page">
        <Routes>
          <Route path="/" element={<IndexPage />} />
          <Route path="/program" element={<ProgramPage />} />
          <Route path="/variant" element={<VariantPage />} />
          <Route path="/diff" element={<DiffPage />} />
          <Route path="/upload" element={<UploadPage />} />
          <Route path="*" element={<NotFoundPage />} />
        </Routes>
      </main>

      {dragging && (
        <div className="drop-overlay" aria-hidden="true">
          <div className="drop-overlay-card">
            <strong>Drop report JSON or shader source</strong>
            <span>Parsed locally; nothing is uploaded to a server. Load a detail before its source file.</span>
          </div>
        </div>
      )}

      <footer className="footer">
        <span>Local reports served by tools/shader_analysis/serve.mjs</span>
        <span>Static estimates; not a GPU occupancy model.</span>
      </footer>
    </div>
  );
}
