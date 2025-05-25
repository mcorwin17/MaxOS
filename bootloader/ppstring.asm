; --------------------------------------------------
; Function: print_string_pm
; Description: Prints a null-terminated string to screen in protected mode
; Assumes: EBX points to the string, uses EDI to access video memory
; --------------------------------------------------
[bits 32]

; Define constants for video memory and color attributes
VIDEO_MEMORY equ 0xb8000
GREEN_ON_BLACK equ 0x0A

; Main loop to read and print characters
print_string_pm_loop:
    mov al, [ebx]          ; Load character from string
    mov ah, GREEN_ON_BLACK ; Set text color

    cmp al, 0              ; Check for null terminator
    je print_string_pm_done ; Exit loop if null terminator is found

    mov [edi], ax          ; Write character and attribute to video memory

    add ebx, 1             ; Move to next character in string
    add edi, 2             ; Advance video memory pointer (2 bytes per character cell)

    jmp print_string_pm_loop

; Save registers and initialize video memory pointer
pusha
mov edi, VIDEO_MEMORY

; Restore registers and return
print_string_pm_done:
    popa
    ret