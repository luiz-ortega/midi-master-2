#!/bin/bash
set -e

echo "Building MidiMaster2..."
echo ""

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring CMake..."
cmake ..

# Build
echo ""
echo "Building..."
make

echo ""
echo "Build complete! Run with: ./build/MidiMaster2"


