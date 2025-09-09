# LEDBrick Web UI Development

This directory contains the web UI for the LEDBrick controller and a development server for local testing.

## Files

- `index.html` - Main UI page
- `ledbrick-ui.js` - UI controller logic
- `ledbrick-api.js` - API client library
- `style.css` - UI styling
- `test-server.mjs` - Development server for local testing
- `package.json` - Node.js dependencies

## Development Setup

The test server allows you to develop the UI locally while proxying API requests to a real LEDBrick device.

### Prerequisites

- Node.js 14 or later
- npm or yarn
- A LEDBrick device on your network

### Installation

```bash
# Install dependencies
npm install
```

### Running the Development Server

```bash
# Start server with device IP
npm start 192.168.1.100

# Or specify both IP and port
node test-server.mjs 192.168.1.100 3000
```

The development server will:
1. Serve the UI files from the local directory
2. Proxy all `/api/*` requests to the specified device IP
3. Automatically reload when you refresh the browser

### Usage

1. Find your LEDBrick device IP address (check your router or use device discovery)
2. Start the development server: `npm start <device-ip>`
3. Open http://localhost:8080 in your browser
4. Edit the UI files - changes will be reflected when you refresh the browser

### Testing Without a Device

If you don't have a physical device, you can still test the UI by:
1. Running the development server without a valid IP
2. The UI will load but API calls will fail
3. You can still test UI layout and interactions

## UI Architecture

The web UI uses vanilla JavaScript with no framework dependencies for maximum compatibility.

### Key Components

- **Schedule Editor**: Dual PWM/Current controls for each channel
- **Schedule Visualization**: Chart.js-based schedule graph
- **Moon Simulation**: Independent PWM and current controls for moonlight
- **Location Settings**: Astronomical calculations with timezone support
- **Channel Configuration**: Per-channel color and max current settings

### API Endpoints

All API endpoints are prefixed with `/api/`:

- `GET /api/status` - Get current scheduler status
- `GET /api/schedule` - Get complete schedule
- `POST /api/schedule` - Update schedule
- `POST /api/moon_simulation` - Update moon settings
- `POST /api/location` - Update location settings
- `GET /api/presets/<name>` - Load preset schedule

## Production Deployment

The UI files are embedded in the ESP32 firmware and served directly by the device. No build process is required - the files are used as-is.