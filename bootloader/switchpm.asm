; --------------------------------------------------
; File: switch_to_pm.asm
; Description: Switches CPU from real mode to protected mode
; --------------------------------------------------

switch_to_pm:
    ; Disable interrupts during mode switch
    cli
    ; Load Global Descriptor Table
    lgdt [gdt_descriptor]

    ; Read CR0 (control register)
    mov eax, cr0
    ; Set the PE (Protection Enable) bit
    or eax, 1
    ; Write back to CR0 to enable protected mode
    mov cr0, eax

    ; Far jump to flush pipeline and switch to 32-bit mode
    jmp CODE_SEG:init_pm

[bits 32]
init_pm:
    ; Set up segment registers for protected mode
    mov ax, DATA_SEG
    mov ds, ax    ; Data Segment
    mov ss, ax    ; Stack Segment
    mov es, ax    ; Extra Segment
    mov fs, ax    ; FS Segment
    mov gs, ax    ; GS Segment

    ; Initialize base pointer
    mov ebp, 0x90000
    ; Initialize stack pointer
    mov esp, ebp

    ; Jump to main protected mode code
    call BEGIN_PM