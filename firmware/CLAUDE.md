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

5. ESPHome compilation:
   ```bash
   make esphome
   # or
   uv run esphome compile
   ```

6. Clean build artifacts:
   ```bash
   make clean           # Clean unit test builds
   make clean-all       # Clean everything including ESPHome builds
   ```

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
- `ledbrick-plus.yaml` - ESPHome configuration file

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

## Development Notes

- All core algorithms are standalone C++ with no ESPHome dependencies
- Unit tests ensure correctness before ESPHome integration
- Clean separation allows testing and development of algorithms independently
- ESPHome component acts as a thin integration layer
- Both astronomical and scheduler components support serialization
- Timezone handling is proper with PST/UTC conversion for accurate sun calculations
- Singapore sunrise can be projected to appear at 10 AM Pacific time using time projection