import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import viteCompression from 'vite-plugin-compression';
import { visualizer } from 'rollup-plugin-visualizer';

export default defineConfig(({ mode }) => ({
  plugins: [
    react(),
    viteCompression({
      algorithm: 'gzip',
      ext: '.gz',
    }),
    mode === 'analyze' && visualizer({
      open: true,
      filename: 'dist/stats.html',
      gzipSize: true,
      brotliSize: true,
    }),
  ].filter(Boolean),
  
  build: {
    target: 'es2020',
    minify: mode === 'debug' ? false : 'terser',
    terserOptions: {
      compress: {
        drop_console: true,
        drop_debugger: true,
        pure_funcs: ['console.log', 'console.info', 'console.debug', 'console.warn'],
        passes: 3,  // Multiple passes for better compression
        unsafe: true,  // More aggressive optimizations
        unsafe_comps: true,
        unsafe_math: true,
        unsafe_proto: true,
        unsafe_regexp: true,
        unsafe_undefined: true,
        unused: true,
        dead_code: true,
      },
      mangle: {
        toplevel: true,  // Mangle top level names
        properties: {
          regex: /^_/,  // Mangle properties starting with _
        },
      },
      format: {
        comments: false,
        ascii_only: true,  // Better for embedded systems
      },
    },
    rollupOptions: {
      output: {
        // Single bundle for embedded use
        manualChunks: undefined,
        entryFileNames: 'app.js',
        chunkFileNames: 'app.js',
        assetFileNames: (assetInfo) => {
          if (assetInfo.name === 'index.css') return 'app.css';
          return assetInfo.name || 'assets/[name].[ext]';
        },
      },
    },
    // Report compressed size
    reportCompressedSize: true,
    // Inline assets smaller than 4kb
    assetsInlineLimit: 4096,
  },
  
  server: {
    port: 3000,
    proxy: {
      '/api': {
        target: process.env.DEVICE_IP || 'http://192.168.1.100',
        changeOrigin: true,
      },
      '/switch': {
        target: process.env.DEVICE_IP || 'http://192.168.1.100',
        changeOrigin: true,
      },
      '/number': {
        target: process.env.DEVICE_IP || 'http://192.168.1.100',
        changeOrigin: true,
      },
    },
  },
  
  preview: {
    port: 3000,
    proxy: {
      '/api': {
        target: process.env.LEDBRICK_IP ? `http://${process.env.LEDBRICK_IP}` : 'http://192.168.1.196',
        changeOrigin: true,
      },
      '/switch': {
        target: process.env.LEDBRICK_IP ? `http://${process.env.LEDBRICK_IP}` : 'http://192.168.1.196',
        changeOrigin: true,
      },
      '/number': {
        target: process.env.LEDBRICK_IP ? `http://${process.env.LEDBRICK_IP}` : 'http://192.168.1.196',
        changeOrigin: true,
      },
    },
  },
}));