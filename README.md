# LEDBrick Project

[![Build and Test](https://github.com/theatrus/ledbrick/actions/workflows/esphome-build.yml/badge.svg)](https://github.com/theatrus/ledbrick/actions/workflows/esphome-build.yml)
[![Unit Tests](https://github.com/theatrus/ledbrick/actions/workflows/unit-tests.yml/badge.svg)](https://github.com/theatrus/ledbrick/actions/workflows/unit-tests.yml)
[![Publish Firmware](https://github.com/theatrus/ledbrick/actions/workflows/publish.yml/badge.svg)](https://github.com/theatrus/ledbrick/actions/workflows/publish.yml)

Open Hardware design for professional aquarium LED lighting systems.

## LEDBrick Plus - Modern ESP32-S3 Based Controller

The **LEDBrick Plus** is a modern, high-performance 8-channel LED driver designed for aquarium lighting applications. Built around the ESP32-S3 microcontroller and ESPHome firmware, it offers professional-grade control with smart home integration.

<img src="static/ledbrick-plus-pcb-top.png" alt="LEDBrick Plus PCB - Top View" width="500">

*Professional 85×85mm PCB with ESP32-S3 microcontroller and 8-channel TPS922053 LED drivers*

<img src="static/ledbrick-plus-pcb-assembled.jpg" alt="LEDBrick Plus PCB - Assembled" width="500">

*LEDBrick Plus assembled board showing component placement and professional finish*

### 🚀 Quick Start

**Install firmware directly in your browser:**
👉 **[LEDBrick Plus Web Installer](https://theatrus.github.io/ledbrick/)**

*No downloads or software required - uses WebSerial to flash firmware directly via USB.*

### ⚡ Key Specifications

- **8 Independent LED Channels** with individual PWM and current control
- **High Current Capability**: Up to 2A per channel (scalable design)
- **Wide Voltage Range**: Supports up to 60V input
- **ESP32-S3 Microcontroller**: Dual-core with Wi-Fi connectivity
- **Advanced PWM**: 39kHz flicker-free operation (17 total PWM channels)
- **Precision Current Control**: TPS922053 LED driver chips with 0-2A regulation
- **Environmental Monitoring**: INA228 power monitoring, I2C temperature sensors
- **Fan Control**: 4-wire fan control with RPM monitoring
- **Status Indication**: WS2812 RGB LED with customizable effects
- **Temperature Expansion**: 1-Wire bus for DS18B20 temperature sensors
- **Home Assistant Integration**: Native API with automatic discovery
- **Professional Form Factor**: 85×85mm board designed for marine environments

### 🏠 Smart Home Integration

- **ESPHome Native**: Full Home Assistant integration out of the box
- **Web Interface**: Built-in configuration and monitoring
- **OTA Updates**: Wireless firmware updates
- **MQTT Compatible**: Optional MQTT broker support
- **RESTful API**: HTTP API for custom integrations

### 🎛️ Advanced Control Features

- **Individual Channel Control**: Independent PWM brightness and current limiting
- **Smart Current Limiting**: Configurable per-channel maximum current with real-time enforcement
- **Programmable Schedules**: Sunrise/sunset simulation and custom lighting programs
- **Temperature Monitoring**: I2C sensor integration for thermal management
- **Power Monitoring**: Real-time current and voltage monitoring via INA228

### 🔧 Technical Details

**Microcontroller**: ESP32-S3 (Dual-core, 240MHz, Wi-Fi)
**LED Drivers**: 8× TPS922053 constant current drivers
**PWM Frequency**: 39kHz (MCPWM) + 16kHz (LEDC)
**Current Range**: 0-2A per channel (1A default limit)
**Input Voltage**: 12-60V DC
**Communication**: Wi-Fi, USB-C, I2C expansion
**Dimensions**: 85×85mm

For detailed forum discussion and community support, see: [Reef2Reef LEDBrick Thread](https://www.reef2reef.com/threads/ledbrick-diy-led-pendant-with-pucks.243746/)

---

## LEDBrick Classic - Original Design

The original LEDBrick design consists of two main components for DIY aquarium LED lighting.

### Emitter Board

The emitter board is a custom metal-core PCBA designed to hold 8 channels of LEDs from Cree (XP footprint), Osram (SSL and Square), Luxeon (UV and Rebel footprints).

![Classic PWM Board](ledbrick-classic/pwm/board.png)

[Design files and gerber files](emitter-board/)

### Bluetooth Low Energy PWM Board (Legacy)

An 8 channel 24VIN + 12VOUT Fan LED dimmer board powered by a Nordic nRF51 Bluetooth Low Energy microcontroller.

[Firmware is available for both Keil and GCC compilers](ledbrick-classic/firmware/nordic_nrf51/app/ble_peripheral/ledbrick_pwm/)
[Design files and gerber files](ledbrick-classic/pwm/0.2/)

For legacy documentation and original design details, see the [ReefCentral thread](http://www.reefcentral.com/forums/showthread.php?t=2477205).

---

## Development

### Building Firmware Locally

```bash
# Install uv (Python package manager)
curl -LsSf https://astral.sh/uv/install.sh | sh

# Clone and build
git clone https://github.com/theatrus/ledbrick.git
cd ledbrick/firmware

# Quick build (runs tests, builds web UI, compiles firmware)
make esphome

# Individual components
make test           # Run all unit tests
make web            # Build React web interface
make web-debug      # Build non-minified web UI (for debugging)
make dev            # Start development server (set LEDBRICK_IP=device-ip)
make preview        # Serve minified build locally (debug minification)
make esphome-debug  # Build firmware with non-minified web UI
make clean-all      # Clean all build artifacts
make help           # Show all available targets
```

### Firmware Features

#### Advanced LED Control
- **8-Channel PWM Control**: Independent brightness and current limiting per channel
- **Smart Current Limiting**: Real-time enforcement of per-channel maximum current settings
- **Astronomical Scheduling**: Sunrise/sunset simulation with location-based timing
- **Time Projection**: Map remote reef locations to local timezone (e.g., Tahiti sunrise at 6AM Pacific)
- **Moon Phase Simulation**: Configurable moon phase effects with intensity scaling
- **Manual Override**: Direct channel control when scheduler is disabled

#### Web Interface
- **React-based UI**: Modern, responsive interface for all device settings
- **Real-time Status**: Live monitoring of channels, sensors, and system status
- **Channel Configuration**: Set names, colors, and maximum current per channel
- **Schedule Management**: Visual schedule editor with interpolation and presets
- **Location Settings**: Built-in reef location presets for accurate astronomical timing
- **Sensor Monitoring**: Display of power consumption, temperature, and fan status
- **Schedule Chart Modes**: Toggle between PWM % and Current (mA) display views
- **Debug Support**: Source maps and non-minified builds for troubleshooting

#### Sensor Integration
- **Power Monitoring**: INA228 current/voltage sensor with real-time display
- **Temperature Sensors**: DS18B20 1-wire sensors for thermal monitoring
- **Fan Control**: PWM speed control with RPM monitoring and on/off state
- **Status Bar**: Responsive layout showing all sensor data and channel status

#### Temperature Control System
- **PID Controller**: Professional-grade temperature regulation with anti-windup protection
- **Multi-Sensor Support**: Average multiple temperature sensors for accurate readings
- **Emergency Thermal Protection**: Automatic shutdown with configurable thresholds
- **Fan Curve Management**: Dynamic fan speed based on temperature with customizable curve
- **Real-time Monitoring**: Temperature status and PID performance metrics in web UI
- **Anti-Oscillation Logic**: Fan maintains minimum speed within 10°C below setpoint

#### Smart Home Integration
- **Home Assistant**: Native ESPHome integration with automatic discovery
- **RESTful API**: Complete HTTP API for schedule management and control
- **OTA Updates**: Wireless firmware updates via ESPHome
- **MQTT Support**: Optional MQTT broker integration

### Development Workflow

#### Testing and Quality Assurance
```bash
# Run unit tests for core algorithms
make test                    # All tests
make test-astro             # Astronomical calculator only
make test-scheduler         # LED scheduler only
make test-clang             # Cross-compiler validation

# Web development
LEDBRICK_IP=192.168.1.xxx make dev    # Development server with device proxy
make web                              # Production build
```

#### Architecture
- **Standalone Components**: Core algorithms (astronomical calculator, LED scheduler) are pure C++ with comprehensive unit tests
- **ESPHome Integration**: Thin wrapper components that integrate with ESPHome ecosystem
- **React Frontend**: TypeScript/React web interface with responsive design
- **Clean Separation**: Core logic can be developed and tested independently of hardware

#### Common Issues and Solutions
- **Compilation Errors**: Run `make clean-all` then `make esphome` for clean rebuild
- **Sensor Configuration**: Ensure sensor IDs match ESPHome configuration exactly
- **Web Interface Debugging**: Use `LEDBRICK_IP=device-ip make dev` for development server
- **API Testing**: Access endpoints directly at `http://device-ip/api/status` etc.

### GitHub Actions

- **Continuous Integration**: Automatic build testing on firmware changes
- **Unit Test Validation**: Standalone component testing with multiple compilers  
- **Web Publishing**: Automated deployment of web installer to GitHub Pages
- **Release Management**: Tagged releases automatically build and publish firmware

## License

Open Source Hardware - see individual component licenses for details.