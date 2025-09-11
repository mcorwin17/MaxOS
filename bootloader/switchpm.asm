; Real mode -> 32-bit protected mode.
; Ends in [bits 32], so don't include 16-bit code after this file.

[bits 16]

switch_to_protected_mode:
    cli                         ; real-mode IVT is about to be meaningless
    lgdt [gdt_descriptor]

    mov eax, cr0
    or  eax, 0x1                ; PE
    mov cr0, eax

    ; far jump to load CS with a pm selector and flush the prefetch queue
    jmp CODE_SEG:init_pm

[bits 32]

init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    jmp KERNEL_OFFSET           ; kernel never returns
