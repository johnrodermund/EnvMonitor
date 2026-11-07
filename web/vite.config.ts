import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import { mockApi } from './mock/plugin';

export default defineConfig({
  base: './',
  plugins: [preact(), mockApi()],
  build: {
    assetsInlineLimit: 0,
    cssCodeSplit: false,
    rollupOptions: {
      output: {
        entryFileNames: 'assets/app.js',
        chunkFileNames: 'assets/[name].js',
        assetFileNames: (info) =>
          info.name && info.name.endsWith('.css') ? 'assets/app.css' : 'assets/[name][extname]',
      },
    },
  },
  test: { environment: 'node' },
});
