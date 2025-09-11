; print_string - real mode, via BIOS teletype.
; in:  si = null-terminated string
; all registers preserved

[bits 16]

print_string:
    pusha

.loop:
    lodsb
    test al, al
    jz .done

    mov ah, 0x0E
    mov bh, 0x00                ; page, BIOS reads this
    mov bl, 0x07
    int 0x10

    jmp .loop

.done:
    popa
    ret
