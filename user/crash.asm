; Reads kernel memory from ring 3. The PMM bitmap at 1M is present but
; kernel-only, so this is a protection violation, not a missing page.
; The kernel should kill this process and carry on - not panic.

BITS 32
ORG 0x08048000

WRITE equ 3

_start:
    mov eax, WRITE
    mov ebx, 1
    mov ecx, msg
    mov edx, msg_len
    int 0x80

    mov eax, [0x00100000]       ; goodbye

    ; never reached - if it is, protection isn't working
    mov eax, WRITE
    mov ebx, 1
    mov ecx, bug_msg
    mov edx, bug_len
    int 0x80

.hang:
    jmp .hang

msg:     db "crash: touching kernel memory from ring 3", 10
msg_len  equ $ - msg
bug_msg: db "BUG: read kernel memory from ring 3", 10
bug_len  equ $ - bug_msg
