; print_string_pm - print a string in protected mode.
; No BIOS here, so write straight into the VGA text buffer.
;
; in:  ebx = null-terminated string
; all registers preserved

[bits 32]

VIDEO_MEMORY   equ 0xB8000
GREEN_ON_BLACK equ 0x0A

print_string_pm:
    pusha
    mov edi, VIDEO_MEMORY

.loop:
    mov al, [ebx]
    mov ah, GREEN_ON_BLACK

    test al, al
    jz .done

    mov [edi], ax               ; each cell is char + attribute

    inc ebx
    add edi, 2
    jmp .loop

.done:
    popa
    ret
