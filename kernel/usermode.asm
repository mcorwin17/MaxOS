; void enter_user_mode(uint32_t eip, uint32_t esp)
;
; The only way down to ring 3 is an iret with user selectors in the frame.
; Push ss/esp/eflags/cs/eip and iret; the CPU drops privilege and loads the
; user stack as part of the return.
;
; IF is set in the pushed eflags on purpose. Forget it and user code runs
; with interrupts off: no timer, no preemption, and no way back into the
; kernel except a syscall.

[bits 32]

global enter_user_mode

USER_CODE equ 0x1B          ; 0x18 | RPL 3
USER_DATA equ 0x23          ; 0x20 | RPL 3

enter_user_mode:
    mov ecx, [esp + 4]      ; eip
    mov edx, [esp + 8]      ; esp

    ; Data segments first. The pushes below go through SS, which is still
    ; the kernel's, so this is safe to do early.
    mov ax, USER_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push USER_DATA          ; ss
    push edx                ; esp
    push 0x202              ; eflags: IF | the always-set bit 1
    push USER_CODE          ; cs
    push ecx                ; eip
    iretd
