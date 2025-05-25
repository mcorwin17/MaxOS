# --------------------------------------------------
# MaxOS Project - x86 Bootloader and Kernel
# Description: A minimal educational OS designed for learning x86 architecture
# --------------------------------------------------

## MaxOS: Simple x86 Bootloader and Kernel
About:
This project is a basic x86 bootloader that loads a simple kernel from memory and displays characters to the screen using direct memory access to VGA text mode.

<!-- Overview of current functionality implemented in MaxOS -->
What It Does:
• Loads the kernel directly from disk to memory using assembly.
• Switches from the BIOS real mode into protected mode.
• The kernel writes characters to the screen via the VGA text buffer.
• Basic helper functions for printing single characters.
• Early groundwork for future kernel expansion.

<!-- Summary of implemented technical features -->
Current Features:
• Simple bootloader in assembly (Switches to 32-bit protected mode and loads kernel).
• Kernel written in C.
• Direct memory manipulation at 0xB8000 (VGA text mode).
• clear_screen() function to reset the screen.
• print_char() function for output.

<!-- Features planned for future development -->
Planned Features:
• Basic keyboard input.
• Text cursor movement.
• Filesystem loading.

<!-- Instructions to build and run MaxOS -->
How to run:

Download NASM
Download QEMU
Download a cross-compiler (https://wiki.osdev.org/GCC_Cross-Compiler)
Then run using the command:
sh run.sh
