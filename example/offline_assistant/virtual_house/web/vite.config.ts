import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// The page is served by house.py from web/dist; `npm run dev` proxies the API to a running house.
export default defineConfig({
  base: './',
  plugins: [react()],
  build: { chunkSizeWarningLimit: 1000 },
  server: { proxy: { '/api': 'http://127.0.0.1:8124' } },
});
