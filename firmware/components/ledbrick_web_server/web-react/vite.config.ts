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
    sourcemap: mode === 'debug' ? 'inline' : true,  // Enable source maps
    terserOptions: {
      compress: {
        drop_console: true,
        drop_debugger: true,
        pure_funcs: ['console.log', 'console.info', 'console.debug', 'console.warn'],
        passes: 2,  // Reduced passes
        // Disable unsafe optimizations that might break array access
        unsafe: false,
        unsafe_comps: false,
        unsafe_math: false,
        unsafe_proto: false,
        unsafe_regexp: false,
        unsafe_undefined: false,
        unused: true,
        dead_code: true,
        keep_fargs: true,  // Keep function arguments to prevent issues
        keep_fnames: false,  // But still mangle function names
      },
      mangle: {
        toplevel: false,  // Don't mangle top level to help debugging
        properties: false,  // Disable property mangling - this can break object access
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