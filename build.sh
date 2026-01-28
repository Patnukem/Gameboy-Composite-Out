#!/bin/bash
#
# Build script for DMG to Composite Video Converter
# 
# Usage:
#   ./build.sh          - Build the project
#   ./build.sh clean    - Clean build directory
#   ./build.sh flash    - Build and attempt to flash (macOS only)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_status() {
    echo -e "${GREEN}==>${NC} $1"
}

echo_warning() {
    echo -e "${YELLOW}Warning:${NC} $1"
}

echo_error() {
    echo -e "${RED}Error:${NC} $1"
}

# Check for PICO_SDK_PATH
check_sdk() {
    if [ -z "${PICO_SDK_PATH}" ]; then
        echo_warning "PICO_SDK_PATH not set"
        
        # Try common locations
        COMMON_PATHS=(
            "${HOME}/pico-sdk"
            "${HOME}/pico/pico-sdk"
            "/opt/pico-sdk"
            "${HOME}/Developer/pico-sdk"
        )
        
        for path in "${COMMON_PATHS[@]}"; do
            if [ -d "$path" ]; then
                export PICO_SDK_PATH="$path"
                echo_status "Found Pico SDK at: ${PICO_SDK_PATH}"
                break
            fi
        done
        
        if [ -z "${PICO_SDK_PATH}" ]; then
            echo_error "Could not find Pico SDK. Please set PICO_SDK_PATH environment variable."
            echo ""
            echo "To install the Pico SDK:"
            echo "  git clone https://github.com/raspberrypi/pico-sdk.git"
            echo "  cd pico-sdk"
            echo "  git submodule update --init"
            echo "  export PICO_SDK_PATH=\$(pwd)"
            exit 1
        fi
    else
        echo_status "Using Pico SDK at: ${PICO_SDK_PATH}"
    fi
}

# Clean build directory
clean() {
    echo_status "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
    echo_status "Clean complete"
}

# Build the project
build() {
    check_sdk
    
    echo_status "Creating build directory..."
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    
    echo_status "Running CMake..."
    cmake ..
    
    echo_status "Building..."
    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    
    if [ -f "${BUILD_DIR}/dmg_composite.uf2" ]; then
        echo ""
        echo_status "Build successful!"
        echo ""
        echo "Output files:"
        echo "  ${BUILD_DIR}/dmg_composite.uf2  (for drag-and-drop flashing)"
        echo "  ${BUILD_DIR}/dmg_composite.elf  (for debugging)"
        echo ""
        echo "To flash:"
        echo "  1. Hold BOOTSEL button on Pico"
        echo "  2. Connect USB while holding BOOTSEL"
        echo "  3. Release BOOTSEL"
        echo "  4. Copy dmg_composite.uf2 to the RPI-RP2 drive"
    else
        echo_error "Build failed - UF2 file not created"
        exit 1
    fi
}

# Flash to Pico (macOS)
flash() {
    build
    
    # Look for mounted Pico
    PICO_MOUNT="/Volumes/RPI-RP2"
    
    if [ -d "${PICO_MOUNT}" ]; then
        echo_status "Found Pico at ${PICO_MOUNT}, flashing..."
        cp "${BUILD_DIR}/dmg_composite.uf2" "${PICO_MOUNT}/"
        echo_status "Flash complete! Pico will reboot automatically."
    else
        echo_warning "Pico not found in BOOTSEL mode"
        echo ""
        echo "To flash manually:"
        echo "  1. Hold BOOTSEL button on Pico"
        echo "  2. Connect USB while holding BOOTSEL"
        echo "  3. Release BOOTSEL"
        echo "  4. Run: cp ${BUILD_DIR}/dmg_composite.uf2 /Volumes/RPI-RP2/"
    fi
}

# Main
case "${1:-build}" in
    clean)
        clean
        ;;
    flash)
        flash
        ;;
    build|"")
        build
        ;;
    *)
        echo "Usage: $0 [clean|build|flash]"
        exit 1
        ;;
esac
