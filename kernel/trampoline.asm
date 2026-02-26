; AP startup trampoline.
;
; A SIPI points an application processor at a page below 1MB and it arrives
; here in real mode, everything undefined. This code walks it up to where
; the boot CPU already lives: protected mode, paging on, kernel stack, C.
;
; Assembled flat at 0x8000 (SIPI vector 0x08) and copied there at runtime.
; The BSP patches the mailbox fields below before each SIPI - one AP at a
; time, so one mailbox is enough.

ORG 0x8000
BITS 16

ap_start:
    cli
    xor ax, ax
    mov ds, ax

    lgdt [tramp_gdt_desc]

    mov eax, cr0
    or  eax, 1                  ; PE
    mov cr0, eax

    jmp 0x08:ap_pm

BITS 32
ap_pm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov eax, [tramp_cr3]        ; the kernel's page directory
    mov cr3, eax

    mov eax, cr0
    or  eax, 0x80000000         ; PG
    mov cr0, eax

    mov esp, [tramp_stack]      ; this AP's own kernel stack

    mov eax, [tramp_entry]
    call eax                    ; ap_main - never returns

.hang:
    hlt
    jmp .hang

align 8
; Minimal flat GDT, only good enough to reach the kernel's real one.
tramp_gdt:
    dq 0
    dq 0x00CF9A000000FFFF       ; code
    dq 0x00CF92000000FFFF       ; data
tramp_gdt_desc:
    dw 23
    dd tramp_gdt

; Mailbox, patched by the BSP. Offsets are found by scanning for the magic,
; so nothing here is position-fragile.
align 4
tramp_magic:  dd 0x54524D50    ; 'PMRT'
tramp_cr3:    dd 0
tramp_stack:  dd 0
tramp_entry:  dd 0
