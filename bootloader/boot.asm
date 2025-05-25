; --------------------------------------------------
; MaxOS Bootloader
; Author: Maxwell Corwin
; Last Updated: 02/10/2025
; Description: Loads the kernel into memory, enters
;              protected mode, and begins execution.
; --------------------------------------------------

[org 0x7C00]
KERNEL_SEGMENT equ 0x1000

; -------------------------------
; Includes (for Protected Mode)
; -------------------------------
%include "bootloader/gdt.asm"
%include "bootloader/switch_to_pm.asm"
%include "bootloader/print_string_pm.asm"
%include "bootloader/print_string.asm"

; -------------------------------
; Data
; -------------------------------
error_msg: db "Error when reading disk!", 0
msg_pm_mode: db "Welcome to MaxOS", 0

; -------------------------------
; Initial Setup
; -------------------------------

    ; Initialize stack
    mov bp, 0x9000
    mov sp, bp

    ; Clear the screen (Mode 03h: 80x25 text)
    mov ax, 0x03
    int 0x10

    ; Load the kernel from disk
    call read_disk

    ; Enter 32-bit protected mode
    call switch_to_pm

; -------------------------------
; Disk Reading Routine
; -------------------------------
read_disk:
    mov bx, KERNEL_SEGMENT   ; Load address for the kernel

    mov ah, 0x02             ; BIOS function: Read sectors
    mov al, 0x01             ; Read 1 sector
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