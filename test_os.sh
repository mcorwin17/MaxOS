#!/bin/bash
# Test script for MaxOS - Enhanced Version

echo "🚀 MaxOS Test Script"
echo "==================="

# Check if dependencies are installed
echo "Checking dependencies..."
if ! command -v nasm >/dev/null 2>&1; then
    echo "❌ NASM not found. Install with: brew install nasm"
    exit 1
fi

if ! command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "❌ QEMU not found. Install with: brew install qemu"
    exit 1
fi

echo "✅ Dependencies found!"

# Build the OS
echo ""
echo "🔨 Building MaxOS..."
make clean && make

if [ $? -ne 0 ]; then
    echo "❌ Build failed!"
    exit 1
fi

echo "✅ Build successful!"

# Show build info
echo ""
echo "📊 Build Information:"
make info

echo ""
echo "🎮 Starting MaxOS in QEMU..."
echo "You should see a black screen with MaxOS welcome message"
echo "Press Ctrl+C to exit QEMU"
echo ""

# Run QEMU with cocoa display (works best on macOS)
qemu-system-i386 -fda floppy.img -boot a -display cocoa -m 16
