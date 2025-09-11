; Flat GDT. Both descriptors cover the whole 4 GB with base 0, so segmentation
; is effectively off and only the type/privilege bits matter.
;
; Code before data on purpose, so the selectors come out 0x08 / 0x10 like
; everyone else's. They're computed as offsets, so reordering changes them.

gdt_start:
    dq 0x0                      ; null descriptor

gdt_code:                       ; 0x08
    dw 0xFFFF                   ; limit 0:15
    dw 0x0000                   ; base 0:15
    db 0x00                     ; base 16:23
    db 10011010b                ; present, ring 0, code, exec, readable
    db 11001111b                ; 4K granularity, 32-bit, limit 16:19
    db 0x00                     ; base 24:31

gdt_data:                       ; 0x10
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b                ; present, ring 0, data, writable
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; limit is size-1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start
