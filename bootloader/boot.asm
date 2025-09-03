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
; =============================================================================

[org 0x7C00]
[bits 16]

; =============================================================================
; Constants and Equates
; =============================================================================

KERNEL_SEGMENT        equ 0x1000        ; Kernel load address
KERNEL_SECTOR_COUNT   equ 0x02          ; Number of sectors to load
BOOT_SECTOR          equ 0x01           ; Boot sector number
STACK_SEGMENT        equ 0x9000         ; Stack segment address
STACK_OFFSET         equ 0x0000         ; Stack offset

; =============================================================================
; Includes
; =============================================================================

%include "bootloader/gdt.asm"           ; Global Descriptor Table
%include "bootloader/switchpm.asm"      ; Protected mode transition
%include "bootloader/ppstring.asm"      ; Protected mode string printing
%include "bootloader/pstring.asm"       ; Real mode string printing

; =============================================================================
; Data Section
; =============================================================================

section .data
    ; Boot messages
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
    cli                             ; Disable interrupts during setup
    mov ax, 0x0000                 ; Clear segment registers
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_SEGMENT          ; Set up stack pointer
    
    ; Clear the screen (Mode 03h: 80x25 text)
    mov ax, 0x0003
    int 0x10
    
    ; Display boot sequence information
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
    call load_kernel_from_disk
    
    ; Check if kernel loading was successful
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
    cli
    
    ; Switch to 32-bit protected mode
    call switch_to_protected_mode
    
    ; This point should never be reached in real mode
    jmp $

; =============================================================================
; Disk Operations
; =============================================================================

load_kernel_from_disk:
    ; =====================================================================
    ; Load kernel from disk sectors 2-3 to memory address 0x1000
    ; =====================================================================
    
    ; Set up disk read parameters
    mov bx, KERNEL_SEGMENT         ; Load address for kernel
    mov ah, 0x02                   ; BIOS function: Read sectors
    mov al, KERNEL_SECTOR_COUNT    ; Number of sectors to read
    mov ch, 0x00                   ; Cylinder 0
    mov dh, 0x00                   ; Head 0
    mov cl, BOOT_SECTOR + 1        ; Starting sector (sector 2)
    
    ; Perform disk read operation
    int 0x13
    
    ; Check for errors (CF set on error)
    jc .error
    
    ; Verify number of sectors read
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
    mov si, msg_error
    call print_string
    
    ; Halt system on error
    cli
    hlt

; =============================================================================
; Utility Functions
; =============================================================================

print_string:
    ; =====================================================================
    ; Print null-terminated string using BIOS interrupt 10h
    ; =====================================================================
    pusha
    
.loop:
    lodsb                           ; Load next character
    test al, al                     ; Check if character is null
    jz .done                        ; If null, we're done
    
    ; Print character using BIOS
    mov ah, 0x0E                    ; BIOS function: Teletype output
    mov bh, 0x00                    ; Page number
    mov bl, 0x07                    ; Color (white on black)
    int 0x10
    
    jmp .loop                       ; Continue with next character
    
.done:
    popa
    ret

; =============================================================================
; Boot Sector Padding
; =============================================================================

; Pad boot sector to exactly 512 bytes
times 510 - ($ - $$) db 0x00

; Boot signature (required by BIOS)
dw 0xAA55