import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

const proxyTarget = 'http://127.0.0.1:8787';

export default defineConfig({
  base: '/web/',
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/shader_analysis': { target: proxyTarget, changeOrigin: false },
      '/shaders': { target: proxyTarget, changeOrigin: false },
    },
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
});
