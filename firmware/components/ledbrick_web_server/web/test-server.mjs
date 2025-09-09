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
    timeout: 5000,
    proxyTimeout: 5000,
    onProxyReq: (proxyReq, req, res) => {
        console.log(`[Proxy Request] ${req.method} ${req.path} -> http://${DEVICE_IP}${req.path}`);
        console.log('[Request Headers]', req.headers);
    },
    onProxyRes: (proxyRes, req, res) => {
        console.log(`[Proxy Response] ${proxyRes.statusCode} from ${req.path}`);
    },
    onError: (err, req, res) => {
        console.error('[Proxy Error]', err);
        console.error(`Failed to proxy ${req.path} to http://${DEVICE_IP}`);
        res.status(502).json({ 
            error: 'Proxy Error', 
            message: err.message,
            target: `http://${DEVICE_IP}`,
            path: req.path
        });
    }
});

// Health check endpoint
app.get('/health', async (req, res) => {
    try {
        const testUrl = `http://${DEVICE_IP}/api/status`;
        console.log(`[Health Check] Testing connection to ${testUrl}`);
        
        const response = await fetch(testUrl, { 
            method: 'GET',
            timeout: 3000 
        }).catch(err => {
            console.error('[Health Check Failed]', err.message);
            throw err;
        });
        
        res.json({
            status: 'ok',
            device: DEVICE_IP,
            deviceReachable: response.ok,
            deviceStatus: response.status
        });
    } catch (error) {
        res.status(500).json({
            status: 'error',
            device: DEVICE_IP,
            deviceReachable: false,
            error: error.message
        });
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
app.listen(LOCAL_PORT, async () => {
    console.log(`\nTest server running at http://localhost:${LOCAL_PORT}`);
    console.log('\nTo test the UI:');
    console.log(`1. Open http://localhost:${LOCAL_PORT} in your browser`);
    console.log('2. The UI will load from local files');
    console.log(`3. API requests will be proxied to ${DEVICE_IP}`);
    console.log('\nPress Ctrl+C to stop the server');
    
    // Test device connectivity
    console.log(`\nTesting connection to device at ${DEVICE_IP}...`);
    try {
        const response = await fetch(`http://${DEVICE_IP}/api/status`, { 
            method: 'GET',
            timeout: 3000 
        });
        if (response.ok) {
            console.log('✓ Device is reachable!');
        } else {
            console.log(`✗ Device responded with status ${response.status}`);
        }
    } catch (error) {
        console.log(`✗ Could not connect to device: ${error.message}`);
        console.log('Make sure the device IP is correct and the device is on the network.');
    }
});