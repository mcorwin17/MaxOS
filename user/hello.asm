; First thing to ever run in ring 3 here.
; Prints, then tries to make the kernel echo back its own heap - which has
; to come back -1, or write() isn't checking pointers.

BITS 32
ORG 0x08048000

EXIT  equ 1
WRITE equ 3

_start:
    mov eax, WRITE
    mov ebx, 1
    mov ecx, msg
    mov edx, msg_len
    int 0x80

    ; write(1, <kernel heap>, 16) - the kernel must refuse
    mov eax, WRITE
    mov ebx, 1
    mov ecx, 0xd0000000
    mov edx, 16
    int 0x80

    cmp eax, -1
    jne .bug

    mov eax, WRITE
    mov ebx, 1
    mov ecx, ok_msg
    mov edx, ok_len
    int 0x80
    jmp .done

.bug:
    mov eax, WRITE
    mov ebx, 1
    mov ecx, bug_msg
    mov edx, bug_len
    int 0x80

.done:
    mov eax, EXIT
    mov ebx, 42
    int 0x80

msg:     db "hello from ring 3", 10
msg_len  equ $ - msg
ok_msg:  db "bad write rejected", 10
ok_len   equ $ - ok_msg
bug_msg: db "BUG: kernel accepted a kernel pointer", 10
bug_len  equ $ - bug_msg
