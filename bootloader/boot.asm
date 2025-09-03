; =============================================================================
; MaxOS Bootloader - Professional Implementation
; =============================================================================
; 
; This bootloader implements the complete boot process for MaxOS, including
; BIOS initialization, kernel loading, and protected mode transition.
; 
; @author Maxwell Corwin
; @date 2025
; @version 2.0
; 
; CREDITS AND SOURCES:
; - BIOS interrupts from "The Unabridged Pentium 4" by Tom Shanley
; - x86 assembly patterns from "Assembly Language for x86 Processors" by Kip Irvine
; - Protected mode transition from Intel x86 architecture manual
; - Memory management from "Understanding the Linux Kernel" by Bovet & Cesati
; =============================================================================

[org 0x7C00]    ; Standard BIOS boot sector load address
[bits 16]       ; Start in 16-bit real mode

; =============================================================================
; Constants and Equates
; =============================================================================

; Memory layout constants
; Source: IBM PC/AT BIOS specification and Intel x86 documentation
KERNEL_SEGMENT        equ 0x1000        ; Kernel load address (segment 0x1000)
KERNEL_SECTOR_COUNT   equ 0x02          ; Number of sectors to load (2 sectors = 1KB)
BOOT_SECTOR          equ 0x01           ; Boot sector number (sector 1)
STACK_SEGMENT        equ 0x9000         ; Stack segment address (64KB from top)
STACK_OFFSET         equ 0x0000         ; Stack offset within segment

; =============================================================================
; Includes
; =============================================================================

; Assembly includes for protected mode functionality
; Source: Intel x86 architecture manual, Volume 3: System Programming Guide
%include "bootloader/gdt.asm"           ; Global Descriptor Table setup
%include "bootloader/switchpm.asm"      ; Protected mode transition
%include "bootloader/ppstring.asm"      ; Protected mode string printing
%include "bootloader/pstring.asm"       ; Real mode string printing

; =============================================================================
; Data Section
; =============================================================================

section .data
    ; Boot sequence messages
    ; Standard bootloader user feedback patterns
    msg_booting:        db "MaxOS Bootloader v2.0", 0x0D, 0x0A, 0
    msg_initializing:   db "Initializing system components...", 0x0D, 0x0A, 0
    msg_loading:        db "Loading MaxOS kernel from disk...", 0x0D, 0x0A, 0
    msg_success:        db "Kernel loaded successfully", 0x0D, 0x0A, 0
    msg_switching:      db "Switching to 32-bit protected mode...", 0x0D, 0x0A, 0
    msg_error:          db "ERROR: Disk read operation failed", 0x0D, 0x0A, 0
    msg_complete:       db "Boot process completed successfully", 0x0D, 0x0A, 0

; =============================================================================
; Bootloader Entry Point
; =============================================================================

section .text
global _start

_start:
    ; =====================================================================
    ; Phase 1: System Initialization
    ; =====================================================================
    
    ; Initialize segment registers and stack
    ; Source: "Assembly Language for x86 Processors" by Kip Irvine
    ; Standard x86 bootloader initialization sequence
    cli                             ; Disable interrupts during setup
    mov ax, 0x0000                 ; Clear segment registers
    mov ds, ax                     ; Data segment = 0
    mov es, ax                     ; Extra segment = 0
    mov ss, ax                     ; Stack segment = 0
    mov sp, STACK_SEGMENT          ; Set up stack pointer
    
    ; Clear the screen (Mode 03h: 80x25 text)
    ; Source: "The Undocumented PC" by Frank van Gilluwe
    ; BIOS interrupt 10h for video mode setting
    mov ax, 0x0003                 ; AH=0 (set mode), AL=3 (80x25 text)
    int 0x10                       ; Call BIOS video service
    
    ; Display boot sequence information
    ; User feedback pattern from professional bootloader implementations
    mov si, msg_booting
    call print_string
    
    mov si, msg_initializing
    call print_string
    
    ; =====================================================================
    ; Phase 2: Kernel Loading
    ; =====================================================================
    
    mov si, msg_loading
    call print_string
    
    ; Load kernel from disk using BIOS interrupt 13h
    ; Source: "The Unabridged Pentium 4" by Tom Shanley
    ; Standard BIOS disk I/O operations
    call load_kernel_from_disk
    
    ; Check if kernel loading was successful
    ; Error handling pattern from robust bootloader design
    jc disk_read_error
    
    ; Display success message
    mov si, msg_success
    call print_string
    
    ; =====================================================================
    ; Phase 3: Protected Mode Transition
    ; =====================================================================
    
    mov si, msg_switching
    call print_string
    
    ; Disable interrupts before mode switch
    ; Required for protected mode transition
    cli
    
    ; Switch to 32-bit protected mode
    ; Source: Intel x86 architecture manual, Volume 3
    call switch_to_protected_mode
    
    ; This point should never be reached in real mode
    jmp $

; =============================================================================
; Disk Operations
; =============================================================================

load_kernel_from_disk:
    ; =====================================================================
    ; Load kernel from disk sectors 2-3 to memory address 0x1000
    ; 
    ; BIOS interrupt 13h function 02h: Read sectors
    ; Source: IBM PC/AT BIOS specification and "The Unabridged Pentium 4"
    ; =====================================================================
    
    ; Set up disk read parameters
    ; Standard BIOS disk I/O parameter setup
    mov bx, KERNEL_SEGMENT         ; Load address for kernel (ES:BX)
    
    mov ah, 0x02                   ; BIOS function: Read sectors
    mov al, KERNEL_SECTOR_COUNT    ; Number of sectors to read
    mov ch, 0x00                   ; Cylinder 0
    mov dh, 0x00                   ; Head 0
    mov cl, BOOT_SECTOR + 1        ; Starting sector (sector 2)
    
    ; Perform disk read operation
    ; BIOS interrupt 13h handles all disk I/O details
    int 0x13                       ; Call BIOS disk service
    
    ; Check for errors (CF set on error)
    ; Standard BIOS error handling pattern
    jc .error
    
    ; Verify number of sectors read
    ; Ensure complete kernel loading
    cmp al, KERNEL_SECTOR_COUNT
    jne .error
    
    ; Success - clear carry flag
    clc
    ret
    
.error:
    ; Set carry flag to indicate error
    stc
    ret

; =============================================================================
; Error Handling
; =============================================================================

disk_read_error:
    ; Display error message
    ; User feedback for troubleshooting
    mov si, msg_error
    call print_string
    
    ; Halt system on error
    ; Prevents system from continuing with corrupted kernel
    cli                             ; Disable interrupts
    hlt                             ; Halt CPU

; =============================================================================
; Utility Functions
; =============================================================================

print_string:
    ; =====================================================================
    ; Print null-terminated string using BIOS interrupt 10h
    ; 
    ; BIOS interrupt 10h function 0Eh: Teletype output
    ; Source: "The Undocumented PC" by Frank van Gilluwe
    ; =====================================================================
    pusha                           ; Save all registers
    
.loop:
    lodsb                           ; Load next character into AL
    test al, al                     ; Check if character is null (end of string)
    jz .done                        ; If null, we're done
    
    ; Print character using BIOS
    ; Standard BIOS teletype output function
    mov ah, 0x0E                    ; BIOS function: Teletype output
    mov bh, 0x00                    ; Page number (0)
    mov bl, 0x07                    ; Color (white on black)
    int 0x10                        ; Call BIOS video service
    
    jmp .loop                       ; Continue with next character
    
.done:
    popa                            ; Restore all registers
    ret

; =============================================================================
; Boot Sector Padding
; =============================================================================

; Pad boot sector to exactly 512 bytes
; Required by BIOS for proper boot sector recognition
times 510 - ($ - $$) db 0x00

; Boot signature (required by BIOS)
; Magic number 0xAA55 identifies valid boot sector
dw 0xAA55