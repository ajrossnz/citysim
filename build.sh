#!/bin/bash
# Build script for CitySim DOS game
# Requires Open Watcom installed

echo "CitySim Build Script"
echo "===================="

# Check if Open Watcom is available
if ! command -v wcc386 &> /dev/null; then
    echo "Error: Open Watcom compiler (wcc386) not found in PATH"
    echo "Please install Open Watcom and ensure it's in your PATH"
    exit 1
fi

echo "Cleaning previous build..."
wmake clean 2>/dev/null

echo "Building CitySim..."
wmake

if [ $? -eq 0 ]; then
    echo ""
    echo "Build successful!"
    echo "Output: citysim.exe"
    echo ""
    echo "IMPORTANT: You need DOS4GW.EXE to run this game. To get DOS4GW.EXE, run: ./get_dos4gw.sh"
    echo ""
    echo "Then test in DOSBox:"
    echo "  dos4gw citysim.exe"
else
    echo ""
    echo "Build failed. Please check error messages above."
    exit 1
fi
