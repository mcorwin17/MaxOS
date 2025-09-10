; MaxOS boot sector
; Loads the kernel off the floppy, switches to protected mode, jumps to it.

[org 0x7C00]
[bits 16]

KERNEL_OFFSET        equ 0x1000
KERNEL_SECTOR_COUNT  equ 32        ; 16 KB, kernel is ~5 KB
KERNEL_START_SECTOR  equ 0x02      ; sector 1 is this code
STACK_TOP            equ 0x9000
DISK_RETRY_COUNT     equ 3

; Entry has to be the first thing in the file. nasm -f bin emits in assembly
; order and the BIOS jumps straight to 0x7C00, so the includes go at the bottom.
_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP
    sti

    mov [boot_drive], dl        ; BIOS hands us the drive number here

    mov ax, 0x0003              ; 80x25 text
    int 0x10

    mov si, msg_boot
    call print_string

    call load_kernel
    jc  disk_error

    mov si, msg_ok
    call print_string

    cli
    call switch_to_protected_mode
    jmp $                       ; not reached


; Read the kernel to 0x0000:0x1000. CF set on failure.
load_kernel:
    mov si, msg_load
    call print_string

    mov cx, DISK_RETRY_COUNT

.attempt:
    push cx                     ; int 13h clobbers cx

    xor ah, ah                  ; reset controller first, floppies are flaky
    mov dl, [boot_drive]
    int 0x13

    xor ax, ax
    mov es, ax
    mov bx, KERNEL_OFFSET

    mov ah, 0x02
    mov al, KERNEL_SECTOR_COUNT
    mov ch, 0x00                ; cylinder 0
    mov dh, 0x00                ; head 0
    mov cl, KERNEL_START_SECTOR ; sectors are 1-based
    mov dl, [boot_drive]
    int 0x13

    pop cx
    jc .retry
    cmp al, KERNEL_SECTOR_COUNT ; al = sectors actually read
    jne .retry

    clc
    ret

.retry:
    loop .attempt
    stc
    ret


disk_error:
    mov si, msg_err
    call print_string
    cli
.hang:
    hlt
    jmp .hang


boot_drive: db 0

; keep these short, they compete with code for 512 bytes
msg_boot:   db "MaxOS", 0x0D, 0x0A, 0
msg_load:   db "Loading kernel...", 0x0D, 0x0A, 0
msg_ok:     db "OK", 0x0D, 0x0A, 0
msg_err:    db "Disk read failed", 0x0D, 0x0A, 0


; Order matters: switchpm.asm ends in [bits 32], so only 32-bit code after it.
%include "bootloader/pstring.asm"
%include "bootloader/gdt.asm"
%include "bootloader/switchpm.asm"
%include "bootloader/ppstring.asm"

%if ($ - $$) > 510
  %error "boot sector too big, needs a second stage"
%endif

times 510 - ($ - $$) db 0x00
dw 0xAA55
