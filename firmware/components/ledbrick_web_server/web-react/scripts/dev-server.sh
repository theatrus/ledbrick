#!/bin/bash

# Development server script for LEDBrick React UI

echo "🚀 LEDBrick Development Server"
echo "=============================="
echo

# Get device IP - check environment variable first
if [ -n "$LEDBRICK_IP" ]; then
    DEVICE_IP=$LEDBRICK_IP
    echo "Using device IP from environment: $DEVICE_IP"
else
    read -p "Enter device IP address (default: 192.168.1.100): " DEVICE_IP
    DEVICE_IP=${DEVICE_IP:-192.168.1.100}
fi

# Validate IP format (basic check)
if [[ ! $DEVICE_IP =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
    echo "❌ Invalid IP address format"
    exit 1
fi

echo
echo "📡 Proxying to device at: http://$DEVICE_IP"
echo

# Test device connectivity
echo "🔍 Testing device connection..."
if curl -s --max-time 2 "http://$DEVICE_IP/api/status" > /dev/null; then
    echo "✅ Device is reachable!"
else
    echo "⚠️  Warning: Could not reach device at $DEVICE_IP"
    echo "   Make sure the device is on and connected to the network."
    read -p "Continue anyway? (y/N): " CONTINUE
    if [[ ! $CONTINUE =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

echo
echo "🌐 Starting development server..."
echo "   Local URL: http://localhost:3000"
echo "   Device API: http://$DEVICE_IP"
echo
echo "Press Ctrl+C to stop the server"
echo

# Start Vite with device IP
DEVICE_IP="http://$DEVICE_IP" npm run dev