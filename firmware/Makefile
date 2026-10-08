# LEDBrick Firmware Test Suite
# Builds and runs unit tests for standalone components

# ESPHome release the firmware is built against (keep in sync with CI and min_version)
ESPHOME_VERSION := 2026.9.1
ESPHOME := uvx --from esphome==$(ESPHOME_VERSION) esphome

# Compiler settings
CXX := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -O2

# Source directories
COMPONENT_DIR := components/ledbrick_scheduler
INCLUDES := -I. -I$(COMPONENT_DIR)
BUILD_DIR := build
TEST_DIR := tests

# Source files
ASTRO_SOURCES := $(COMPONENT_DIR)/astronomical_calculator.cpp
SCHEDULER_SOURCES := $(COMPONENT_DIR)/scheduler.cpp $(COMPONENT_DIR)/cJSON.c $(COMPONENT_DIR)/led_dimming.cpp $(COMPONENT_DIR)/led_models.cpp
PID_SOURCES := $(COMPONENT_DIR)/pid_controller.cpp
TEMP_CONTROL_SOURCES := $(COMPONENT_DIR)/temperature_control.cpp $(COMPONENT_DIR)/cJSON.c
LED_DIMMING_SOURCES := $(COMPONENT_DIR)/led_dimming.cpp $(COMPONENT_DIR)/led_models.cpp
TEST_FRAMEWORK := test_framework.h

# Test executables
ASTRO_TEST := $(BUILD_DIR)/test_astronomical
SCHEDULER_TEST := $(BUILD_DIR)/test_scheduler
PID_TEST := $(BUILD_DIR)/test_pid_controller
TEMP_CONTROL_TEST := $(BUILD_DIR)/test_temperature_control
LED_DIMMING_TEST := $(BUILD_DIR)/test_led_dimming
ALL_TESTS := $(ASTRO_TEST) $(SCHEDULER_TEST) $(PID_TEST) $(TEMP_CONTROL_TEST) $(LED_DIMMING_TEST)

# Phony targets
.PHONY: all test test-astro test-scheduler test-pid test-temperature test-led clean help setup esphome web dev

# Default target
all: test

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Build astronomical calculator tests
$(ASTRO_TEST): test_astronomical.cpp $(ASTRO_SOURCES) $(TEST_FRAMEWORK) | $(BUILD_DIR)
	@echo "🔨 Building astronomical calculator tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ test_astronomical.cpp $(ASTRO_SOURCES)

# Build scheduler tests  
$(SCHEDULER_TEST): test_scheduler.cpp $(SCHEDULER_SOURCES) $(TEST_FRAMEWORK) | $(BUILD_DIR)
	@echo "🔨 Building scheduler tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ test_scheduler.cpp $(SCHEDULER_SOURCES)

# Build PID controller tests
$(PID_TEST): test_pid_controller.cpp $(PID_SOURCES) $(TEST_FRAMEWORK) | $(BUILD_DIR)
	@echo "🔨 Building PID controller tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ test_pid_controller.cpp $(PID_SOURCES)

# Build temperature control tests
$(TEMP_CONTROL_TEST): test_temperature_control.cpp $(TEMP_CONTROL_SOURCES) $(PID_SOURCES) $(TEST_FRAMEWORK) | $(BUILD_DIR)
	@echo "🔨 Building temperature control tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ test_temperature_control.cpp $(TEMP_CONTROL_SOURCES) $(PID_SOURCES)

# Build LED dimming tests
$(LED_DIMMING_TEST): test_led_dimming.cpp $(LED_DIMMING_SOURCES) $(TEST_FRAMEWORK) | $(BUILD_DIR)
	@echo "🔨 Building LED dimming tests..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ test_led_dimming.cpp $(LED_DIMMING_SOURCES)

# Run all tests
test: $(ALL_TESTS)
	@echo "=== LEDBrick Firmware Test Suite ==="
	@echo
	@echo "🧪 Running astronomical calculator tests..."
	@$(ASTRO_TEST)
	@echo
	@echo "🧪 Running scheduler tests..."
	@$(SCHEDULER_TEST)
	@echo
	@echo "🧪 Running PID controller tests..."
	@$(PID_TEST)
	@echo
	@echo "🧪 Running temperature control tests..."
	@$(TEMP_CONTROL_TEST)
	@echo
	@echo "🧪 Running LED dimming tests..."
	@$(LED_DIMMING_TEST)
	@echo
	@echo "=== ALL TESTS COMPLETED ==="

# Run individual test suites
test-astro: $(ASTRO_TEST)
	@echo "🧪 Running astronomical calculator tests..."
	@$(ASTRO_TEST)

test-scheduler: $(SCHEDULER_TEST)
	@echo "🧪 Running scheduler tests..."
	@$(SCHEDULER_TEST)

test-pid: $(PID_TEST)
	@echo "🧪 Running PID controller tests..."
	@$(PID_TEST)

test-temperature: $(TEMP_CONTROL_TEST)
	@echo "🧪 Running temperature control tests..."
	@$(TEMP_CONTROL_TEST)

test-led: $(LED_DIMMING_TEST)
	@echo "🧪 Running LED dimming tests..."
	@$(LED_DIMMING_TEST)

# Cross-compiler testing
test-clang: CXX := clang++
test-clang: $(ALL_TESTS)
	@echo "🔄 Running tests with clang++..."
	@$(ASTRO_TEST) > /dev/null && echo "✅ Astronomical tests passed with clang++"
	@$(SCHEDULER_TEST) > /dev/null && echo "✅ Scheduler tests passed with clang++"
	@$(LED_DIMMING_TEST) > /dev/null && echo "✅ LED dimming tests passed with clang++"

# Verbose test output
test-verbose: $(ALL_TESTS)
	@echo "=== LEDBrick Firmware Test Suite (Verbose) ==="
	@echo "Compiler: $(CXX)"
	@echo "Flags: $(CXXFLAGS)"
	@echo
	@echo "🧪 Running astronomical calculator tests..."
	$(ASTRO_TEST)
	@echo
	@echo "🧪 Running scheduler tests..."
	$(SCHEDULER_TEST)
	@echo
	@echo "=== ALL TESTS COMPLETED ==="

# ESPHome compilation (depends on web build)
esphome: web
	@echo "🏗️  Compiling ESPHome firmware..."
	$(ESPHOME) compile ledbrick-plus.yaml

# ESPHome compilation with non-minified web UI (for debugging)
esphome-debug: web-debug
	@echo "🐛 Compiling ESPHome firmware (with non-minified web UI)..."
	$(ESPHOME) compile ledbrick-plus.yaml

# Build React web UI
web:
	@echo "🌐 Building React web UI..."
	@cd components/ledbrick_web_server/web-react && \
		npm install --silent && \
		npm run build && \
		npm run generate-cpp
	@echo "✅ Web UI built and C++ generated"

# Build React web UI without minification (for debugging)
web-debug:
	@echo "🐛 Building React web UI (non-minified for debugging)..."
	@cd components/ledbrick_web_server/web-react && \
		npm install --silent && \
		npm run build:debug && \
		npm run generate-cpp
	@echo "✅ Web UI built (debug mode) and C++ generated"

# Development server for React UI
dev:
	@cd components/ledbrick_web_server/web-react && LEDBRICK_IP=$(LEDBRICK_IP) npm run dev:server

# Preview server for minified build (to debug minification issues)
preview: web
	@echo "🔍 Starting preview server for minified build..."
	@echo "   Access at http://localhost:3000"
	@echo "   This serves the production build to help debug minification issues"
	@echo "   Using device at: $${LEDBRICK_IP:-192.168.1.196}"
	@cd components/ledbrick_web_server/web-react && LEDBRICK_IP=$(LEDBRICK_IP) npm run preview

# Setup development environment
setup:
	@echo "📋 Setting up LEDBrick development environment..."
	@if ! command -v uv >/dev/null 2>&1; then \
		echo "❌ uv not found. Please install uv: https://github.com/astral-sh/uv"; \
		exit 1; \
	fi
	@if ! command -v g++ >/dev/null 2>&1; then \
		echo "❌ g++ not found. Please install build tools."; \
		exit 1; \
	fi
	@echo "✅ Development environment ready"

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -f test_astronomical test_scheduler  # Legacy cleanup
	@echo "✅ Clean complete"

# Full clean including ESPHome builds
clean-all: clean
	@echo "🧹 Cleaning ESPHome build artifacts..."
	@rm -rf .esphome/build
	@echo "✅ Full clean complete"

# Help target
help:
	@echo "LEDBrick Firmware Test Suite"
	@echo "============================"
	@echo
	@echo "Available targets:"
	@echo "  all             - Run all tests (default)"
	@echo "  test            - Run all tests"
	@echo "  test-astro      - Run astronomical calculator tests only"
	@echo "  test-scheduler  - Run scheduler tests only"
	@echo "  test-led        - Run LED dimming tests only"
	@echo "  test-clang      - Run tests with clang++ compiler"
	@echo "  test-verbose    - Run tests with verbose output"
	@echo "  web             - Build React web UI"
	@echo "  web-debug       - Build React web UI (non-minified for debugging)"
	@echo "  dev             - Start React development server"
	@echo "  preview         - Serve minified build locally (debug minification)"
	@echo "  esphome         - Compile ESPHome firmware (includes web build)"
	@echo "  esphome-debug   - Compile ESPHome firmware with non-minified web UI"
	@echo "  setup           - Verify development environment"
	@echo "  clean           - Clean build artifacts"
	@echo "  clean-all       - Clean all build artifacts including ESPHome"
	@echo "  help            - Show this help message"
	@echo
	@echo "Examples:"
	@echo "  make            - Run all tests"
	@echo "  make test-astro - Test astronomical calculator only"
	@echo "  make esphome    - Build ESPHome firmware"
	@echo "  make esphome-debug - Build firmware with non-minified web UI"
	@echo "  make clean test - Clean and run all tests"