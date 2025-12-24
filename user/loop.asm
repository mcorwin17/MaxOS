; Runs forever, printing a dot between sleeps. Only a signal ends it -
; that's the point. Ctrl-C should read as "killed by signal 2".

BITS 32
ORG 0x08048000

WRITE equ 3
SLEEP equ 5

_start:
    mov eax, WRITE
    mov ebx, 1
    mov ecx, msg
    mov edx, msg_len
    int 0x80

.forever:
    mov eax, SLEEP
    mov ebx, 150
    int 0x80

    mov eax, WRITE
    mov ebx, 1
    mov ecx, dot
    mov edx, 1
    int 0x80

    jmp .forever

msg:     db "loop: running until someone stops me", 10
msg_len  equ $ - msg
dot:     db "."
