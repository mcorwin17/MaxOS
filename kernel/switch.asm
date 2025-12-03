; void context_switch(uint32_t* save_esp_here, uint32_t load_esp)
;
; Saves the callee-saved registers on the current stack, parks esp in the
; outgoing thread, loads the incoming one's esp and pops its registers back.
; The ret at the end returns into whatever the incoming thread was doing when
; it was switched away - which is a genuinely strange thing to watch work in
; a debugger the first time.
;
; Only the callee-saved set needs preserving: everything else is already the
; caller's problem across a function call, and a switch is a function call.

[bits 32]

global context_switch

context_switch:
    push ebx
    push esi
    push edi
    push ebp

    ; after four pushes the arguments have moved up 16 bytes
    mov eax, [esp + 20]     ; save_esp_here
    mov [eax], esp

    mov esp, [esp + 24]     ; load_esp

    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
