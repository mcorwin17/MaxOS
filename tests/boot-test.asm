; Stand-in kernel that proves the boot path works end to end.
;
; Gets assembled to a flat binary and dropped in where the real kernel goes,
; so booting it exercises everything: the boot sector entry, the int 13h load,
; the GDT, the jump to protected mode, and the handoff to 0x1000.
;
; Writes to COM1 so the result is readable from outside the VM, then hits
; isa-debug-exit so qemu quits by itself instead of hanging a CI run.

[bits 32]
[org 0x1000]

COM1  equ 0x3F8
VIDEO equ 0xB8000
ATTR  equ 0x0A
EXIT  equ 0xF4

_start:
    call serial_init

    mov esi, msg
    call serial_puts

    mov esi, msg
    call vga_puts

    ; qemu exits with (value << 1) | 1, so this gives exit code 1
    mov al, 0
    mov dx, EXIT
    out dx, al

.hang:
    cli
    hlt
    jmp .hang


serial_init:
    mov dx, COM1+1
    mov al, 0x00
    out dx, al              ; interrupts off
    mov dx, COM1+3
    mov al, 0x80
    out dx, al              ; DLAB on
    mov dx, COM1+0
    mov al, 0x03
    out dx, al              ; divisor lo, 38400
    mov dx, COM1+1
    mov al, 0x00
    out dx, al              ; divisor hi
    mov dx, COM1+3
    mov al, 0x03
    out dx, al              ; 8N1, DLAB off
    mov dx, COM1+2
    mov al, 0xC7
    out dx, al              ; FIFO
    mov dx, COM1+4
    mov al, 0x0B
    out dx, al
    ret

serial_putc:                ; al = char
    push eax
    mov ah, al
.wait:
    mov dx, COM1+5
    in al, dx
    test al, 0x20           ; holding register empty?
    jz .wait
    mov al, ah
    mov dx, COM1
    out dx, al
    pop eax
    ret

serial_puts:                ; esi = string
    push eax
.loop:
    lodsb
    test al, al
    jz .done
    call serial_putc
    jmp .loop
.done:
    mov al, 0x0D
    call serial_putc
    mov al, 0x0A
    call serial_putc
    pop eax
    ret

vga_puts:                   ; esi = string
    mov edi, VIDEO
    mov ah, ATTR
.loop:
    lodsb
    test al, al
    jz .done
    mov [edi], ax
    add edi, 2
    jmp .loop
.done:
    ret


msg: db "kernel reached 0x1000, boot path works", 0

times 2048 - ($ - $$) db 0
