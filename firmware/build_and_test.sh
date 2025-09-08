#\!/bin/bash

# LEDBrick Astronomical Calculator - Build and Test Script
# This script compiles the standalone C++ astronomical calculator and runs unit tests

set -e  # Exit on any error

echo "=== LEDBrick Astronomical Calculator Build & Test ==="
echo

# Configuration
CXX=${CXX:-g++}
CXXFLAGS="-std=c++11 -Wall -Wextra -O2"
SOURCE_FILES="components/ledbrick_scheduler/astronomical_calculator.cpp test_astronomical.cpp"
OUTPUT_BINARY="test_astronomical"

# Clean previous build
echo "🧹 Cleaning previous build..."
rm -f "$OUTPUT_BINARY"

# Compile
echo "🔨 Compiling astronomical calculator..."
echo "Compiler: $CXX"
echo "Flags: $CXXFLAGS"
echo "Sources: $SOURCE_FILES"
echo

$CXX $CXXFLAGS $SOURCE_FILES -o "$OUTPUT_BINARY" -lm

if [ $? -eq 0 ]; then
    echo "✅ Compilation successful\!"
else
    echo "❌ Compilation failed\!"
    exit 1
fi

echo

# Run tests
echo "🧪 Running unit tests..."
echo

./"$OUTPUT_BINARY"

# Check test results
if [ $? -eq 0 ]; then
    echo
    echo "🎉 All tests completed\!"
    echo "✅ Astronomical calculator is ready for integration."
else
    echo
    echo "❌ Tests failed\!"
    exit 1
fi

# Optional: Run with different compiler if available
if command -v clang++ > /dev/null 2>&1 && [ "$CXX" \!= "clang++" ]; then
    echo
    echo "🔄 Also testing with clang++..."
    clang++ $CXXFLAGS $SOURCE_FILES -o "${OUTPUT_BINARY}_clang" -lm
    
    if [ $? -eq 0 ]; then
        echo "✅ Clang++ compilation successful\!"
        ./"${OUTPUT_BINARY}_clang"
        rm -f "${OUTPUT_BINARY}_clang"
    fi
fi

# Cleanup
echo
echo "🧹 Cleaning up test binary..."
rm -f "$OUTPUT_BINARY"

echo "🚀 Build and test cycle complete\!"
