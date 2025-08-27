; --------------------------------------------------
; MaxOS Bootloader - Cool Version
; Author: Maxwell Corwin
; Last Updated: 02/10/2025
; Description: Enhanced bootloader with cool status messages
; --------------------------------------------------

[org 0x7C00]
KERNEL_SEGMENT equ 0x1000

; -------------------------------
; Includes (for Protected Mode)
; -------------------------------
%include "bootloader/gdt.asm"
%include "bootloader/switchpm.asm"
%include "bootloader/ppstring.asm"
%include "bootloader/pstring.asm"

; -------------------------------
; Data
; -------------------------------
error_msg: db "ERROR: Disk read failed!", 0
msg_pm_mode: db "Welcome to MaxOS", 0
loading_msg: db "Loading MaxOS kernel...", 0
success_msg: db "Kernel loaded successfully!", 0
switching_msg: db "Switching to protected mode...", 0
booting_msg: db "MaxOS Bootloader v2.0", 0
init_msg: db "Initializing system...", 0

; -------------------------------
; Initial Setup
; -------------------------------

    ; Initialize stack
    mov bp, 0x9000
    mov sp, bp

    ; Clear the screen (Mode 03h: 80x25 text)
    mov ax, 0x03
    int 0x10

    ; Print booting message
    mov si, booting_msg
    call print_string
    
    ; Print newline
    mov ah, 0x0e
    mov al, 0x0d
    int 0x10
    mov al, 0x0a
    int 0x10

    ; Print initialization message
    mov si, init_msg
    call print_string
    
    ; Print newline
    mov ah, 0x0e
    mov al, 0x0d
    int 0x10
    mov al, 0x0a
    int 0x10

    ; Print loading message
    mov si, loading_msg
    call print_string

    ; Load the kernel from disk
    call read_disk

    ; Print success message
    mov si, success_msg
    call print_string
    
    ; Print newline
    mov ah, 0x0e
    mov al, 0x0d
    int 0x10
    mov al, 0x0a
    int 0x10

    ; Print switching message
    mov si, switching_msg
    call print_string

    ; Enter 32-bit protected mode
    call switch_to_pm

; -------------------------------
; Disk Reading Routine
; -------------------------------
read_disk:
    mov bx, KERNEL_SEGMENT   ; Load address for the kernel

    mov ah, 0x02             ; BIOS function: Read sectors
    mov al, 0x02             ; Read 2 sectors (1024 bytes)
    mov ch, 0x00             ; Cylinder 0
    mov dh, 0x00             ; Head 0
    mov cl, 0x02             ; Sector 2 (boot sector is 1)

    mov dl, 0x00             ; Floppy drive 0

    int 0x13                 ; Call BIOS disk service
    jc disk_error            ; Jump to error handler on failure
    ret

; -------------------------------
; Disk Read Error Handler
; -------------------------------
disk_error:
    mov si, error_msg
    call print_string
    cli                      ; Disable interrupts
.halt:
    hlt                      ; Halt CPU
    jmp .halt                ; Infinite loop

; -------------------------------
; 32-bit Protected Mode Entry
; -------------------------------
[bits 32]

BEGIN_PM:
    ; Uncomment to display welcome message in PM
    ; mov ebx, msg_pm_mode
    ; call print_string_pm

    call KERNEL_SEGMENT      ; Jump to kernel entry point
    jmp $                    ; Hang if kernel returns

; -------------------------------
; Boot Sector Padding & Signature
; -------------------------------
times 510-($-$$) db 0
dw 0xAA55                   ; Boot signature