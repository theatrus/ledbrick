# LEDBrick Firmware - Claude Development Notes

## Important: Line Endings
**ALWAYS use LF (Unix) line endings, not CRLF (Windows) line endings for all files.**

## Building the Project

To build and test the LEDBrick firmware:

1. Enter the firmware directory:
   ```bash
   cd firmware
   ```

2. Run all tests (default):
   ```bash
   make
   # or
   make test
   ```

3. Individual test suites:
   ```bash
   make test-astro      # Test astronomical calculator only
   make test-scheduler  # Test LED scheduler only
   ```

4. Cross-compiler testing:
   ```bash
   make test-clang      # Test with clang++ instead of g++
   ```

5. Build React web UI:
   ```bash
   make web             # Build web UI and generate C++
   ```

6. ESPHome compilation (includes web build):
   ```bash
   make esphome         # Builds web UI then compiles ESPHome
   ```

7. Upload firmware to device:
   ```bash
   uv run esphome run ledbrick-plus.yaml --device=COM3
   # Replace COM3 with your actual device port
   ```

8. Clean build artifacts:
   ```bash
   make clean           # Clean unit test builds
   make clean-all       # Clean everything including ESPHome builds
   ```

## Web Development

### Frontend Development
The web interface is built with React + TypeScript + Vite:

1. Development server (with device proxy):
   ```bash
   LEDBRICK_IP=192.168.1.xxx make dev
   ```

2. Build for production:
   ```bash
   make web             # Builds and generates C++ automatically
   ```

3. Setup development environment:
   ```bash
   make setup           # Verify uv, g++, and other dependencies
   ```

### Makefile Targets
- `make` or `make test` - Run all unit tests
- `make test-astro` - Test astronomical calculator only
- `make test-scheduler` - Test LED scheduler only
- `make test-clang` - Run tests with clang++ compiler
- `make web` - Build React web UI and generate C++
- `make dev` - Start React development server with device proxy
- `make esphome` - Build web UI then compile ESPHome firmware
- `make clean` - Clean unit test builds
- `make clean-all` - Clean everything including ESPHome builds
- `make help` - Show all available targets

### Testing Web Interface
- Set `LEDBRICK_IP` environment variable for development server proxy
- The web server runs on port 80 by default
- API endpoints are available at `/api/*`
- Use browser dev tools for debugging React frontend

## Project Structure

### Standalone Components (No ESPHome Dependencies)
- `astronomical_calculator.h/cpp` - Standalone C++ astronomical calculator with timezone support
- `scheduler.h/cpp` - Standalone C++ LED scheduler with interpolation, presets, and serialization
- `test_astronomical.cpp` - Unit test suite for the astronomical calculator  
- `test_scheduler.cpp` - Unit test suite for the LED scheduler
- `test_framework.h` - Common test framework for consistent testing across components
- `Makefile` - Consolidated build system for all unit tests and cross-compiler testing

### ESPHome Integration
- `components/ledbrick_scheduler/` - Main ESPHome custom component (wrapper around standalone components)
- `components/ledbrick_web_server/` - ESP-IDF web server component with React frontend
- `ledbrick-plus.yaml` - ESPHome configuration file
- `packages/` - Reusable configuration packages for channels, web server, etc.

## Architecture

The project follows a clean separation between core algorithms and ESPHome integration:

### Standalone Components
1. **AstronomicalCalculator** - Pure C++ astronomical calculations
   - Sun/moon position calculations with spherical trigonometry
   - Sunrise/sunset time calculations with timezone support
   - Intensity calculations with atmospheric effects
   - Time projection for timezone mapping
   - Comprehensive unit tests covering edge cases

2. **LEDScheduler** - Pure C++ scheduling logic
   - Schedule point management with interpolation
   - Built-in presets (sunrise/sunset, full spectrum, simple)
   - Binary serialization for efficient storage
   - JSON export for debugging and configuration
   - Dynamic channel count support
   - Comprehensive unit tests covering all functionality

### ESPHome Integration Layer
- **LEDBrickScheduler** - ESPHome component that:
  - Wraps standalone components
  - Handles ESPHome-specific concerns (GPIO, preferences, logging)
  - Manages persistent storage in flash
  - Provides Home Assistant integration
  - Uses projected astronomical calculations for accurate scheduling

- **LEDBrickWebServer** - ESP-IDF web server component that:
  - Provides standalone web interface with React frontend
  - REST API for schedule management and control
  - Real-time status monitoring with sensor integration
  - Responsive design for mobile and desktop
  - Supports manual channel control when scheduler disabled

## Web Interface Features

### Core Functionality
- **Schedule Management**: Create, edit, and delete schedule points with PWM and current values
- **Channel Configuration**: Configure channel names, colors, and maximum current limits
- **Location Settings**: Set latitude/longitude with reef location presets
- **Time Shift/Projection**: Map remote astronomical times to local timezone
- **Moon Simulation**: Configure moon phase effects on lighting
- **Manual Control**: Direct channel control when scheduler is disabled

### Recent Enhancements
- **Responsive Status Bar**: LED channels wrap first on smaller screens
- **Sensor Integration**: Real-time display of fan speed, temperature, and power sensors
- **Modal Improvements**: Settings modal with proper z-index and auto-close functionality
- **Channel Colors**: Schedule table uses actual channel colors as background tones
- **Manual Channel Control**: Click channels in status bar for direct PWM/current control
- **Debug Mode**: Non-minified builds for easier troubleshooting

### API Endpoints
- `GET /api/status` - System status with sensor data
- `GET /api/schedule` - Current schedule configuration
- `POST /api/schedule` - Update schedule configuration
- `POST /api/point` - Add/update schedule point
- `POST /api/location` - Update location settings
- `POST /api/time-shift` - Update time shift settings
- `POST /api/moon-simulation` - Update moon simulation settings
- `POST /api/scheduler/enable|disable` - Control scheduler state
- `POST /api/pwm-scale` - Set global PWM scaling
- `POST /api/channel-control` - Manual channel control
- `POST /api/channel-configs` - Update channel configurations

## Hardware Integration

### Sensor Support
- **INA228**: Current/voltage monitoring (I2C address 0x40)
- **Fan Speed**: RPM monitoring via pulse counter (GPIO35)
- **Fan Control**: PWM control (GPIO37) with enable switch (GPIO36)
- **Temperature**: DS18B20 sensors on 1-wire bus (GPIO41)

### Channel Configuration
- **8 LED Channels**: PWM control with current limiting
- **Current Control**: Analog outputs for constant current regulation
- **Status LED**: WS2812 RGB LED for system status indication

## Common Issues and Solutions

### Compilation Issues
- **Astronomical times error**: Use `rise_minutes`/`set_minutes` instead of `sunrise`/`sunset`
- **Sensor not found**: Ensure sensor IDs match configuration (use explicit IDs)
- **Web content missing**: Run `npm run build` and `npm run generate-cpp`

### Runtime Issues
- **JSON parsing errors**: Check for extra quotes or malformed JSON in API responses
- **Modal positioning**: Ensure proper z-index and DOM structure
- **Refresh on control changes**: Remove unnecessary onUpdate() calls

### Debugging
- **Frontend errors**: Use `LEDBRICK_IP=device-ip make dev` for development server
- **API testing**: Access endpoints directly at `http://device-ip/api/*`
- **Serialization issues**: Check channel_configs vector sizing in deserialize()
- **Build issues**: Run `make clean-all` then `make esphome` for clean rebuild

## Development Notes

- All core algorithms are standalone C++ with no ESPHome dependencies
- Unit tests ensure correctness before ESPHome integration
- Clean separation allows testing and development of algorithms independently
- ESPHome component acts as a thin integration layer
- Both astronomical and scheduler components support serialization
- Timezone handling is proper with PST/UTC conversion for accurate sun calculations
- Singapore sunrise can be projected to appear at 10 AM Pacific time using time projection
- Frontend uses React + TypeScript with Vite for modern development experience
- CSS uses responsive design with flexbox and media queries
- State management avoids full page refreshes for better UX