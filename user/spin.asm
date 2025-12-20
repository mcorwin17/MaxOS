; Preemption check. Burns CPU in ring 3 with no syscalls between two writes;
; if the kernel manages to print its marker during the burn, the timer
; preempted user code and brought it back alive.

BITS 32
ORG 0x08048000

EXIT  equ 1
WRITE equ 3

_start:
    mov eax, WRITE
    mov ebx, 1
    mov ecx, enter_msg
    mov edx, enter_len
    int 0x80

    ; ~1s of nothing at qemu speed, no syscalls, no way to yield
    mov ecx, 150000000
.burn:
    dec ecx
    jnz .burn

    mov eax, WRITE
    mov ebx, 1
    mov ecx, done_msg
    mov edx, done_len
    int 0x80

    mov eax, EXIT
    mov ebx, 7
    int 0x80

enter_msg: db "spin: burning in ring 3", 10
enter_len  equ $ - enter_msg
done_msg:  db "spin: done burning", 10
done_len   equ $ - done_msg
