#include <stdint.h>
#include <stddef.h>

#include "console.h"
#include "serial.h"
#include "vga.h"
#include "signal.h"
#include "fb.h"

#define BUFFER_SIZE 128     /* power of two, so the wrap is a mask */

static volatile char     buffer[BUFFER_SIZE];
static volatile uint32_t head = 0;   /* producer, interrupt context */
static volatile uint32_t tail = 0;   /* consumer, thread context */

static struct process* volatile foreground;

void console_set_foreground(struct process* p)   { foreground = p; }

void console_clear_foreground(struct process* p) {
    if (foreground == p) foreground = 0;
}

void console_push(char c) {
    /* The beginning of a line discipline: Ctrl-C isn't input, it's a
     * statement about the foreground process. signal_send is IRQ-safe -
     * a bit set and a state flip, nothing more. */
    if (c == 0x03 && foreground) {
        signal_send(foreground, SIGINT);
        return;
    }

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
        fb_console_putchar('\b');
        return;
    }

    if (c == '\n') serial_write_char('\r');
    serial_write_char(c);

    /* Once the framebuffer console is up it replaces VGA text mode - the
     * card is in a graphics mode and 0xB8000 isn't a text buffer any more. */
    if (fb_console_active()) {
        fb_console_putchar(c);
    } else {
        print_character(c);
    }
}

void console_write(const char* str) {
    if (!str) return;

    for (size_t i = 0; str[i] != '\0'; ++i) {
        console_putchar(str[i]);
    }
}

void write_decimal_console(uint32_t value) {
    char buf[12];
    int i = 0;

    if (value == 0) { console_putchar('0'); return; }

    while (value > 0) { buf[i++] = (char)('0' + (value % 10)); value /= 10; }
    while (i-- > 0) console_putchar(buf[i]);
}
