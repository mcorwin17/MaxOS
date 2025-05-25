# --------------------------------------------------
# File: build.sh
# Description: Sets up environment variables for building MaxOS
# Target: i686-elf cross-compiler setup
# --------------------------------------------------
#/bin/bash
# Path where the cross-compiler is installed
export PREFIX="$HOME/opt/cross"
# Target architecture for compilation
export TARGET=i686-elf
# Add cross-compiler binaries to system PATH
export PATH="$PREFIX/bin:$PATH"