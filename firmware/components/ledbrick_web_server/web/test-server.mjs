#!/usr/bin/env node

// Test server for LEDBrick UI development
// Serves local UI files and proxies API requests to actual device

import express from 'express';
import { createProxyMiddleware } from 'http-proxy-middleware';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Configuration
const DEVICE_IP = process.argv[2] || '192.168.1.100';
const LOCAL_PORT = parseInt(process.argv[3]) || 8080;

if (process.argv.length < 3) {
    console.log('Usage: node test-server.mjs <device-ip> [local-port]');
    console.log('Example: node test-server.mjs 192.168.1.100 8080');
    process.exit(1);
}

console.log(`Starting test server on http://localhost:${LOCAL_PORT}`);
console.log(`Proxying API requests to http://${DEVICE_IP}`);

const app = express();

// API proxy middleware
const apiProxy = createProxyMiddleware({
    target: `http://${DEVICE_IP}`,
    changeOrigin: true,
    logLevel: 'debug',
    onProxyReq: (proxyReq, req, res) => {
        console.log(`[Proxy] ${req.method} ${req.path} -> http://${DEVICE_IP}${req.path}`);
    },
    onError: (err, req, res) => {
        console.error('[Proxy Error]', err);
        res.status(500).send('Proxy Error: ' + err.message);
    }
});

// Proxy all /api/* requests
app.use('/api', apiProxy);

// Serve static files from current directory
app.use(express.static(__dirname));

// Fallback to index.html for SPA routing
app.get('*', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// Start server
app.listen(LOCAL_PORT, () => {
    console.log(`\nTest server running at http://localhost:${LOCAL_PORT}`);
    console.log('\nTo test the UI:');
    console.log(`1. Open http://localhost:${LOCAL_PORT} in your browser`);
    console.log('2. The UI will load from local files');
    console.log(`3. API requests will be proxied to ${DEVICE_IP}`);
    console.log('\nPress Ctrl+C to stop the server');
});