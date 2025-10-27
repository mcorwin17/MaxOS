#include <stdint.h>
#include <stddef.h>

#include "console.h"
#include "serial.h"
#include "vga.h"

#define BUFFER_SIZE 128     /* power of two, so the wrap is a mask */

static volatile char     buffer[BUFFER_SIZE];
static volatile uint32_t head = 0;   /* producer, interrupt context */
static volatile uint32_t tail = 0;   /* consumer, thread context */

void console_push(char c) {
    uint32_t next = (head + 1) & (BUFFER_SIZE - 1);

    if (next == tail) return;   /* full, drop it */

    buffer[head] = c;
    head = next;
}

int console_has_input(void) {
    return head != tail;
}

char console_getchar(void) {
    while (head == tail) {
        /* hlt until an interrupt arrives, rather than spinning a core */
        __asm__ volatile("hlt");
    }

    char c = buffer[tail];
    tail = (tail + 1) & (BUFFER_SIZE - 1);
    return c;
}

void console_putchar(char c) {
    if (c == '\b') {
        /* Rubbing out is three characters on a terminal but one operation on
         * the screen, so the two paths differ here. */
        serial_write("\b \b");
        backspace_character();
        return;
    }

    if (c == '\n') serial_write_char('\r');
    serial_write_char(c);
    print_character(c);
}

void console_write(const char* str) {
    if (!str) return;

    for (size_t i = 0; str[i] != '\0'; ++i) {
        console_putchar(str[i]);
    }
}
