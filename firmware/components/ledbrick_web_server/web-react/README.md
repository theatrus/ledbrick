# LEDBrick React Web UI

Modern React + TypeScript web interface for the LEDBrick LED controller.

## Features

- **React 18** with TypeScript for type safety
- **Vite** for fast development and optimized builds
- **Chart.js** for schedule visualization
- **Responsive design** for mobile and desktop
- **Real-time updates** via API polling
- **Compressed size**: ~47KB gzipped (includes React framework)

## Development

### Prerequisites

- Node.js 16+ and npm
- LEDBrick device on network or test server

### Quick Start

From the firmware directory:

```bash
# Start development server
make dev

# Enter your device IP when prompted
# Opens browser at http://localhost:3000
```

Or directly in this directory:

```bash
# Install dependencies
npm install

# Start dev server with device proxy
npm run dev:server

# Build production files
npm run build

# Generate C++ web content
npm run generate-cpp
```

### Available Scripts

- `npm run dev` - Start Vite dev server
- `npm run dev:server` - Start dev server with device IP prompt
- `npm run build` - Build production bundles
- `npm run build:analyze` - Build and analyze bundle size
- `npm run generate-cpp` - Generate C++ web_content.cpp
- `npm run size` - Check bundle sizes
- `npm run typecheck` - Run TypeScript type checking

## Architecture

```
web-react/
├── src/
│   ├── api/          # API client
│   ├── components/   # React components
│   ├── hooks/        # Custom React hooks
│   ├── types/        # TypeScript type definitions
│   ├── App.tsx       # Main app component
│   └── main.tsx      # Entry point
├── scripts/
│   ├── generate-web-content.js  # C++ generator
│   └── dev-server.sh           # Dev server with proxy
└── dist/             # Production build output
```

## Build Process

1. **Development**: Vite serves files with hot module replacement
2. **Production Build**: 
   - Vite bundles and minifies all code
   - Outputs: `index.html`, `app.js`, `app.css`
   - Total size: ~47KB gzipped
3. **C++ Generation**:
   - Compresses files with gzip
   - Generates `web_content.cpp` with byte arrays
   - Integrated with ESPHome build

## Size Optimization

The build is optimized for embedded use:

- **Single JS bundle** - No code splitting
- **Aggressive minification** - Terser with multiple passes
- **Gzip compression** - ~70% size reduction
- **Inline critical CSS** - Prevents flash of unstyled content
- **No source maps** - Smaller production build

## API Integration

The app communicates with the LEDBrick device via REST API:

- `/api/schedule` - Get/set schedule
- `/api/status` - Device status and current values
- `/api/presets/*` - Load built-in presets
- `/switch/*` - Control scheduler enable
- `/number/*` - Set PWM scale

## Component Structure

- **App** - Main container, state management
- **Header** - App title
- **ScheduleControls** - Enable/disable, brightness
- **ScheduleTable** - Schedule point editor
- **ScheduleChart** - Visual schedule (TODO)
- **StatusBar** - Current time, sunrise/sunset

## Future Enhancements

- [ ] Complete Chart.js integration
- [ ] Schedule point editor dialog
- [ ] Moon simulation controls
- [ ] Location/astronomical settings
- [ ] Import/export functionality
- [ ] WebSocket for real-time updates

## Troubleshooting

### Build Issues
- Run `npm run clean` to clear caches
- Check Node.js version (16+ required)
- Ensure all dependencies installed

### Dev Server Issues
- Verify device IP is correct
- Check device is on network
- Try direct API access: `http://device-ip/api/status`

### Size Concerns
- Run `npm run build:analyze` to see bundle breakdown
- Consider Preact for 3KB alternative to React
- Enable CDN loading for zero local React size