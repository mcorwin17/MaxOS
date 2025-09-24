#!/bin/bash
# Build and boot MaxOS.
set -euo pipefail

TARGET=i686-elf

missing=0
for tool in nasm "${TARGET}-gcc" "${TARGET}-ld" qemu-system-i386; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "missing: $tool"
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    echo
    echo "Need the cross toolchain, not host gcc. On Debian/Ubuntu:"
    echo "  apt install nasm qemu-system-x86"
    echo "  then build i686-elf gcc/binutils and put it on PATH (see build.sh)"
    exit 1
fi

make clean
make

echo
echo "Booting, ctrl-C to quit."
qemu-system-i386 -fda floppy.img -boot a -m 16
