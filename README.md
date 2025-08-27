# MaxOS

A simple educational operating system I built from scratch to learn about x86 architecture and OS development. This project demonstrates the fundamentals of how operating systems work at a low level.

## What it does

- **Bootloader**: Custom x86 bootloader written in assembly that loads the kernel
- **Protected Mode**: Switches from 16-bit real mode to 32-bit protected mode
- **Kernel**: Basic C kernel with screen management and text output
- **VGA Text Mode**: Direct video memory access for displaying text
- **Build System**: Simple Makefile for building and testing

## Quick Start

### Prerequisites

- **macOS**: `brew install nasm qemu`
- **Linux**: Install `nasm`, `gcc`, and `qemu-system-i386`
- **Windows**: Use WSL or install the tools manually

### Build and Run

```bash
# Clone the repository
git clone https://github.com/mcorwin17/MaxOS.git
cd MaxOS

# Build the OS
make

# Run in QEMU
make qemu
```

## Project Structure

```
MaxOS/
├── bootloader/          # Assembly bootloader code
│   ├── boot.asm        # Main bootloader
│   ├── gdt.asm         # Global Descriptor Table
│   ├── switchpm.asm    # Protected mode switching
│   └── ppstring.asm    # Protected mode string printing
├── kernel/             # C kernel code
│   ├── kernel.c        # Main kernel
│   └── link.ld         # Linker script
├── build/              # Build artifacts
├── bin/                # Binary outputs
├── Makefile            # Build system
├── test_os.sh          # Test script
└── README.md           # This file
```

## Build Commands

```bash
make          # Build complete floppy image
make qemu     # Run in QEMU
make clean    # Clean build artifacts
make help     # Show all available targets
```

## Technical Details

### Bootloader
- 512 bytes (boot sector)
- x86 Assembly (NASM)
- Loads kernel from disk and switches to protected mode

### Kernel
- ~680 bytes
- C (freestanding)
- Screen management and text output

### Memory Layout
- Bootloader: 0x7C00 (BIOS standard)
- Kernel: 0x1000 (loaded by bootloader)
- Video Memory: 0xB8000 (VGA text mode)

## What I learned

This project taught me:
- How computers boot and load operating systems
- Low-level x86 assembly programming
- CPU mode switching and memory protection
- Basic kernel structure and functionality
- Using QEMU for OS development and testing

## Future Improvements

Some ideas for expanding this project:
- Add keyboard input handling
- Implement a simple shell
- Add file system support
- Create a basic text editor
- Add memory management
- Implement multitasking

## Resources

- [OSDev Wiki](https://wiki.osdev.org/) - Great resource for OS development
- [x86 Assembly Guide](https://www.cs.virginia.edu/~evans/cs216/guides/x86.html)
- [QEMU Documentation](https://qemu.readthedocs.io/)

## License

MIT License - see [LICENSE](LICENSE) file for details.

---

Built for learning and education. Happy coding!

