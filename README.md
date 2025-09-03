# MaxOS - Educational Operating System

A functional x86 operating system developed from scratch to demonstrate fundamental computer architecture concepts and low-level system programming. This project implements a complete boot process, kernel initialization, and basic system services in a single floppy disk image.

## Project Overview

MaxOS is designed as an educational platform for understanding how operating systems work at the hardware level. It demonstrates the complete boot sequence from BIOS initialization through kernel execution, including real-to-protected mode transitions, memory management setup, and basic system services.

## Technical Architecture

### Bootloader (Assembly)
- **BIOS Integration**: Standard 512-byte boot sector with proper BIOS calls
- **Memory Management**: Stack initialization and segment register setup
- **Disk I/O**: BIOS interrupt 13h for kernel loading from floppy
- **Protected Mode Transition**: GDT setup and CPU mode switching
- **Kernel Loading**: Loads kernel from disk sectors 2-3 to memory address 0x1000

### Kernel (C)
- **Freestanding Environment**: No standard library dependencies
- **Memory Layout**: Direct video memory access at 0xB8000
- **Screen Management**: 80x25 text mode with cursor control and scrolling
- **Graphics System**: VGA text mode with 16-color support and animations
- **System Services**: Basic I/O, timing, and status display

### Build System
- **Cross-Compilation**: NASM for assembly, GCC for C with freestanding flags
- **Linking**: Custom linker script for proper memory layout
- **Image Creation**: DD commands for floppy disk image generation
- **Testing**: QEMU integration for virtual machine testing

## System Components

### Boot Process
1. **BIOS Boot**: Loads bootloader from sector 1 to 0x7C00
2. **Initialization**: Sets up stack, clears screen, displays status
3. **Kernel Loading**: Reads kernel from disk using BIOS interrupts
4. **Mode Switch**: Sets up GDT and switches to 32-bit protected mode
5. **Kernel Jump**: Transfers control to kernel at 0x1000

### Memory Layout
- **Bootloader**: 0x7C00 - 0x7DFF (512 bytes)
- **Kernel**: 0x1000 - 0x1FFF (4KB)
- **Video Memory**: 0xB8000 - 0xBFFFF (VGA text mode)
- **Stack**: 0x9000 (grows downward)

### Video System
- **Resolution**: 80x25 characters (2000 total positions)
- **Color Support**: 16 foreground colors, 8 background colors
- **Memory Format**: 2 bytes per character (ASCII + attributes)
- **Scrolling**: Basic screen scrolling implementation

## Development Environment

### Prerequisites
- **Assembler**: NASM 2.13+ for x86 assembly
- **Compiler**: GCC 7+ with freestanding C support
- **Emulator**: QEMU 4.0+ for testing
- **Build Tools**: Make, DD, standard Unix utilities

### Build Commands
```bash
# Complete build
make

# Run in QEMU
make qemu

# Clean build artifacts
make clean

# Show available targets
make help
```

### Compilation Flags
```bash
# Assembly (bootloader)
nasm -f bin bootloader/boot.asm -o bin/boot.bin

# C (kernel) - freestanding environment
gcc -ffreestanding -c kernel/kernel.c -o build/kernel.o

# Linker script ensures proper memory layout
```

## Code Structure

### Bootloader Files
```
bootloader/
├── boot.asm          # Main bootloader (512 bytes)
├── gdt.asm           # Global Descriptor Table setup
├── switchpm.asm      # Protected mode transition
├── ppstring.asm      # Protected mode string printing
├── pstring.asm       # Real mode string printing
├── println.asm       # Newline utility
└── load_kernel.asm   # Kernel loading routine
```

### Kernel Files
```
kernel/
├── kernel.c          # Main kernel implementation
└── link.ld           # Linker script for memory layout
```

### Build Artifacts
```
build/
├── kernel.o          # Compiled kernel object
└── (other build files)

bin/
├── boot.bin          # Bootloader binary
├── kernel.bin        # Kernel binary
└── (other binaries)
```

## Technical Implementation Details

### Protected Mode Setup
- **GDT Structure**: Code and data segments with proper privilege levels
- **Segment Registers**: CS, DS, ES, SS initialization
- **Control Registers**: CR0 bit manipulation for mode switching
- **Far Jump**: Required for entering protected mode

### Video Memory Access
- **Direct Memory**: No BIOS calls in protected mode
- **Attribute Bytes**: Color and formatting information
- **Cursor Positioning**: X,Y coordinate system with bounds checking
- **Screen Scrolling**: Memory copy operations for text scrolling

### System Services
- **String Printing**: Multiple color support with positioning
- **Screen Clearing**: Efficient memory zeroing
- **Animation System**: Delayed text appearance and color cycling
- **Status Display**: System information and welcome messages

## Testing and Debugging

### QEMU Integration
- **Floppy Boot**: -fda flag for floppy disk emulation
- **Debug Output**: Console output for development feedback
- **Memory Inspection**: Debugger integration for low-level debugging
- **Performance**: Fast emulation for rapid development cycles

### Development Workflow
1. **Code Changes**: Modify assembly or C source
2. **Build**: Run make to create new floppy image
3. **Test**: Launch in QEMU for immediate feedback
4. **Debug**: Use QEMU debugger for low-level issues
5. **Iterate**: Repeat until functionality is correct

## Educational Value

This project demonstrates:
- **Computer Architecture**: Boot process, memory layout, CPU modes
- **Assembly Programming**: x86 real and protected mode programming
- **System Programming**: Low-level I/O, memory management, interrupts
- **Build Systems**: Cross-compilation, linking, and image creation
- **Operating Systems**: Kernel design, system services, hardware abstraction

## Future Enhancements

### Core System
- [ ] Keyboard input handling and command processing
- [ ] Basic file system for floppy disk access
- [ ] Memory management with paging
- [ ] Interrupt handling and timer services
- [ ] Multi-tasking with process scheduling

### User Interface
- [ ] Simple shell with basic commands
- [ ] Text editor for file modification
- [ ] System monitoring and status display
- [ ] Configuration file support
- [ ] Help system and documentation

### Hardware Support
- [ ] Additional storage device support
- [ ] Network interface integration
- [ ] Sound card and audio output
- [ ] Graphics mode switching
- [ ] USB device support

## Performance Characteristics

- **Boot Time**: <1 second from BIOS to kernel execution
- **Memory Usage**: ~5KB total (bootloader + kernel)
- **Image Size**: 1.44MB floppy disk format
- **Startup Speed**: Immediate execution after mode switch

## System Requirements

### Development
- **CPU**: x86-64 compatible processor
- **Memory**: 4GB+ RAM for development tools
- **Storage**: 100MB+ free space for source and builds
- **OS**: Linux, macOS, or Windows with WSL

### Execution
- **CPU**: x86 compatible (32-bit protected mode)
- **Memory**: 64KB+ RAM (minimal requirements)
- **Storage**: 1.44MB floppy disk or equivalent
- **Display**: VGA text mode compatible

## Resources and References

### Documentation
- [OSDev Wiki](https://wiki.osdev.org/) - Comprehensive OS development guide
- [x86 Assembly Guide](https://www.cs.virginia.edu/~evans/cs216/guides/x86.html) - Assembly reference
- [QEMU Documentation](https://qemu.readthedocs.io/) - Emulator usage guide

### Standards
- **BIOS Specification**: Phoenix/AMI BIOS standards
- **VGA Specification**: IBM VGA hardware documentation
- **x86 Architecture**: Intel/AMD processor manuals
- **ELF Format**: Executable and linking format specification

## License

MIT License - see LICENSE file for details.

## Author

Maxwell Corwin - maxwellcorwin13@gmail.com

---

*Built for educational purposes and computer science learning. Demonstrates fundamental operating system concepts and low-level programming techniques.*

