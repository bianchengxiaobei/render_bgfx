import React from 'react';
import ReactDOM from 'react-dom/client';
import { BrowserRouter } from 'react-router-dom';
import App from './App';
import { UploadProvider } from './upload';
import './styles.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <BrowserRouter basename="/web">
      <UploadProvider>
        <App />
      </UploadProvider>
    </BrowserRouter>
  </React.StrictMode>,
);
