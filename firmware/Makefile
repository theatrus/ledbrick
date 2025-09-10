# LEDBrick Firmware Test Suite
# Builds and runs unit tests for standalone components

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
SCHEDULER_SOURCES := $(COMPONENT_DIR)/scheduler.cpp $(COMPONENT_DIR)/cJSON.c
PID_SOURCES := $(COMPONENT_DIR)/pid_controller.cpp
TEMP_CONTROL_SOURCES := $(COMPONENT_DIR)/temperature_control.cpp $(COMPONENT_DIR)/cJSON.c
TEST_FRAMEWORK := test_framework.h

# Test executables
ASTRO_TEST := $(BUILD_DIR)/test_astronomical
SCHEDULER_TEST := $(BUILD_DIR)/test_scheduler
PID_TEST := $(BUILD_DIR)/test_pid_controller
TEMP_CONTROL_TEST := $(BUILD_DIR)/test_temperature_control
ALL_TESTS := $(ASTRO_TEST) $(SCHEDULER_TEST) $(PID_TEST) $(TEMP_CONTROL_TEST)

# Phony targets
.PHONY: all test test-astro test-scheduler test-pid test-temperature clean help setup esphome web dev

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

# Cross-compiler testing
test-clang: CXX := clang++
test-clang: $(ALL_TESTS)
	@echo "🔄 Running tests with clang++..."
	@$(ASTRO_TEST) > /dev/null && echo "✅ Astronomical tests passed with clang++"
	@$(SCHEDULER_TEST) > /dev/null && echo "✅ Scheduler tests passed with clang++"

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
	uv run esphome compile ledbrick-plus.yaml

# Build React web UI
web:
	@echo "🌐 Building React web UI..."
	@cd components/ledbrick_web_server/web-react && \
		npm install --silent && \
		npm run build && \
		npm run generate-cpp
	@echo "✅ Web UI built and C++ generated"

# Development server for React UI
dev:
	@cd components/ledbrick_web_server/web-react && LEDBRICK_IP=$(LEDBRICK_IP) npm run dev:server

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
	@echo "  test-clang      - Run tests with clang++ compiler"
	@echo "  test-verbose    - Run tests with verbose output"
	@echo "  web             - Build React web UI"
	@echo "  dev             - Start React development server"
	@echo "  esphome         - Compile ESPHome firmware (includes web build)"
	@echo "  setup           - Verify development environment"
	@echo "  clean           - Clean build artifacts"
	@echo "  clean-all       - Clean all build artifacts including ESPHome"
	@echo "  help            - Show this help message"
	@echo
	@echo "Examples:"
	@echo "  make            - Run all tests"
	@echo "  make test-astro - Test astronomical calculator only"
	@echo "  make esphome    - Build ESPHome firmware"
	@echo "  make clean test - Clean and run all tests"