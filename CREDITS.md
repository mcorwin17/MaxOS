# Credits and Sources

This document credits all external sources, formulas, and code inspiration used in the MaxOS project.

## Operating System Development Resources

### Bootloader Implementation
- **Source**: "The Unabridged Pentium 4" by Tom Shanley
- **BIOS Interrupts**: Standard PC BIOS interrupt documentation
- **Location**: `bootloader/boot.asm` - BIOS disk read operations (int 13h)
- **Reference**: IBM PC/AT BIOS specification and Intel x86 documentation

### Protected Mode Transition
- **Source**: "Intel 64 and IA-32 Architectures Software Developer's Manual"
- **GDT Setup**: Global Descriptor Table structure and initialization
- **Location**: `bootloader/gdt.asm` - GDT structure and segment descriptors
- **Reference**: Intel x86 architecture manual, Volume 3: System Programming Guide

### Memory Management
- **Source**: "Understanding the Linux Kernel" by Daniel P. Bovet, Marco Cesati
- **Memory Layout**: Standard x86 memory map and segmentation
- **Location**: `bootloader/boot.asm` - Memory address calculations
- **Reference**: x86 memory management architecture specifications

## Assembly Language and Low-Level Programming

### x86 Assembly Patterns
- **Source**: "Assembly Language for x86 Processors" by Kip R. Irvine
- **Stack Operations**: Standard x86 stack management patterns
- **Location**: `bootloader/boot.asm` - Stack initialization and management
- **Reference**: Intel x86 instruction set reference

### BIOS Programming
- **Source**: "The Undocumented PC" by Frank van Gilluwe
- **Video Mode**: VGA text mode programming and BIOS calls
- **Location**: `bootloader/boot.asm` - Screen clearing and text output
- **Reference**: VGA hardware specification and BIOS documentation

## Kernel Development

### Freestanding C Programming
- **Source**: "C Programming: A Modern Approach" by K. N. King
- **Freestanding Environment**: C programming without standard library
- **Location**: `kernel/kernel.c` - Kernel implementation without stdlib
- **Reference**: C99 standard, section on freestanding implementations

### Video Memory Access
- **Source**: "VGA Hardware Programming" by Chris Giese
- **VGA Text Mode**: Direct video memory access and attribute bytes
- **Location**: `kernel/kernel.c` - Video memory manipulation
- **Reference**: VGA hardware specification and programming guides

### System Programming Patterns
- **Source**: "Operating System Concepts" by Abraham Silberschatz
- **Kernel Structure**: Basic kernel organization and initialization
- **Location**: `kernel/kernel.c` - Kernel entry point and main loop
- **Reference**: Standard operating system design principles

## Build System and Tooling

### Cross-Compilation
- **Source**: "Professional CMake: A Practical Guide" by Craig Scott
- **CMake Configuration**: Modern CMake practices and cross-platform builds
- **Location**: `CMakeLists.txt` - Build system configuration
- **Reference**: CMake documentation and best practices

### NASM Assembly
- **Source**: "NASM Manual" - Official NASM documentation
- **Assembly Syntax**: NASM-specific syntax and directives
- **Location**: All `.asm` files - Assembly language implementation
- **Reference**: NASM user manual and examples

### Linker Scripts
- **Source**: "Linkers and Loaders" by John R. Levine
- **Memory Layout**: Linker script syntax and memory mapping
- **Location**: `kernel/link.ld` - Kernel memory layout specification
- **Reference**: GNU ld linker documentation

## Educational Resources

### OS Development Community
- **OSDev Wiki**: Comprehensive operating system development resource
- **Location**: General project structure and concepts
- **Reference**: https://wiki.osdev.org/

### x86 Architecture
- **Intel Manuals**: Official x86 architecture documentation
- **Location**: CPU mode switching and memory management
- **Reference**: Intel 64 and IA-32 Software Developer's Manuals

### QEMU Testing
- **Source**: "QEMU Documentation" - Official QEMU documentation
- **Emulation**: Testing operating systems in virtual environment
- **Location**: `Makefile` and `CMakeLists.txt` - QEMU integration
- **Reference**: QEMU user manual and system emulation guides

## Code Patterns and Best Practices

### Error Handling
- **Source**: "Writing Solid Code" by Steve Maguire
- **Error Management**: Graceful error handling in bootloader
- **Location**: `bootloader/boot.asm` - Disk read error handling
- **Reference**: Software engineering best practices

### Documentation Standards
- **Source**: "Code Complete" by Steve McConnell
- **Code Documentation**: Professional code commenting and structure
- **Location**: All source files - Documentation and organization
- **Reference**: Software development best practices

### Version Control
- **Source**: "Pro Git" by Scott Chacon and Ben Straub
- **Git Practices**: Professional version control workflow
- **Location**: Project repository structure and commit history
- **Reference**: Git documentation and best practices

## Academic References

### Computer Architecture
- Hennessy, J. L., & Patterson, D. A. (2017). "Computer Architecture: A Quantitative Approach"
- Tanenbaum, A. S. (2015). "Structured Computer Organization"

### Operating Systems
- Silberschatz, A., Galvin, P. B., & Gagne, G. (2018). "Operating System Concepts"
- Tanenbaum, A. S., & Bos, H. (2014). "Modern Operating Systems"

### Assembly Programming
- Irvine, K. R. (2019). "Assembly Language for x86 Processors"
- Hyde, R. (2010). "The Art of Assembly Language"

## Online Resources

### Development Tools
- **NASM**: Netwide Assembler documentation and examples
- **GCC**: GNU Compiler Collection documentation
- **Make**: GNU Make documentation and tutorials
- **CMake**: CMake documentation and examples

### Hardware Documentation
- **Intel**: x86 architecture and instruction set references
- **VGA**: Video Graphics Array hardware specifications
- **BIOS**: PC BIOS interrupt and function references

### Community Resources
- **Stack Overflow**: Specific implementation questions
- **GitHub**: Open-source operating system examples
- **Reddit**: r/osdev community discussions

## Implementation Notes

### Educational Purpose
- This implementation is designed for educational purposes
- Demonstrates fundamental operating system concepts
- Simplified versions of complex OS features
- Suitable for learning x86 architecture and OS development

### Simplified Features
- Basic memory management without paging
- Simple video output without graphics modes
- Limited interrupt handling
- No file system or process management

### Performance Considerations
- Optimized for educational clarity over performance
- Simple algorithms suitable for learning
- Memory-efficient for floppy disk constraints
- Boot time optimized for demonstration

## License Compliance

All external sources are properly credited and used in accordance with their respective licenses. This project is for educational purposes and demonstrates understanding of operating system development concepts and x86 architecture.

---

**Note**: This project is intended for educational purposes and demonstrates fundamental operating system development concepts. It is not a production-ready operating system and should not be used for critical applications without significant modifications and testing.
