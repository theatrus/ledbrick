# LEDBrick Firmware - Claude Development Notes

## Building the Project

To build and test the LEDBrick firmware:

1. Enter the firmware directory:
   ```bash
   cd firmware
   ```

2. For ESPHome compilation:
   ```bash
   uv run esphome compile
   ```

3. For astronomical calculator unit tests:
   ```bash
   ./build_and_test.sh
   ```

## Project Structure

- `components/ledbrick_scheduler/` - Main ESPHome custom component
- `astronomical_calculator.h/cpp` - Standalone C++ astronomical calculator (no ESPHome dependencies)  
- `test_astronomical.cpp` - Unit test suite for the astronomical calculator
- `build_and_test.sh` - Build script for standalone unit tests

## Development Notes

- The astronomical calculator is a standalone C++ library that can be tested independently
- All astronomical calculations (sun/moon position, rise/set times, intensity) are handled by the standalone calculator
- The ESPHome component delegates to the standalone calculator for all astronomical functions
- Unit tests ensure astronomical calculations are correct before ESPHome integration