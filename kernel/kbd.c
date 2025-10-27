#include <stdint.h>

#include "kbd.h"
#include "console.h"
#include "idt.h"
#include "pic.h"
#include "io.h"

#define KBD_DATA    0x60
#define KBD_STATUS  0x64

#define SC_RELEASE  0x80    /* set on the make code when a key comes back up */
#define SC_EXTENDED 0xE0

#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_CTRL     0x1D
#define SC_CAPS     0x3A

static int shift_down = 0;
static int ctrl_down  = 0;
static int caps_on    = 0;
static int extended   = 0;

/* Scancode set 1, unshifted. 0 means "nothing printable". */
static const char keymap[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6',
    '7',  '8', '9', '0', '-', '=', '\b','\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o',  'p', '[', ']', '\n', 0,  'a', 's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0,   '\\','z', 'x', 'c', 'v',
    'b',  'n', 'm', ',', '.', '/', 0,   '*',
    0,    ' ', 0,   0,   0,   0,   0,   0,
};

static const char keymap_shift[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^',
    '&',  '*', '(', ')', '_', '+', '\b','\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O',  'P', '{', '}', '\n', 0,  'A', 'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"',  '~', 0,   '|', 'Z', 'X', 'C', 'V',
    'B',  'N', 'M', '<', '>', '?', 0,   '*',
    0,    ' ', 0,   0,   0,   0,   0,   0,
};

static int is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* Interrupt context. Read the byte, translate, push, leave. Anything more
 * involved belongs in the consumer. */
static void kbd_on_irq(struct registers* r) {
    (void)r;

    uint8_t code = inb(KBD_DATA);

    if (code == SC_EXTENDED) {
        extended = 1;
        return;
    }

    if (code & SC_RELEASE) {
        uint8_t made = code & ~SC_RELEASE;
        if (made == SC_LSHIFT || made == SC_RSHIFT) shift_down = 0;
        if (made == SC_CTRL)                        ctrl_down  = 0;
        extended = 0;
        return;
    }

    if (code == SC_LSHIFT || code == SC_RSHIFT) { shift_down = 1; return; }
    if (code == SC_CTRL)                        { ctrl_down  = 1; return; }
    if (code == SC_CAPS)                        { caps_on = !caps_on; return; }

    if (extended) {
        /* Arrow keys and friends land here. Nothing wants them yet. */
        extended = 0;
        return;
    }

    if (code >= 128) return;

    char c = shift_down ? keymap_shift[code] : keymap[code];
    if (c == 0) return;

    /* Caps flips letters only, and flips back if shift is also held. */
    if (caps_on && is_letter(c)) {
        c = shift_down ? (char)(c + 32) : (char)(c - 32);
    }

    if (ctrl_down && is_letter(c)) {
        c = (char)(c & 0x1F);       /* ctrl-a is 1, ctrl-c is 3, and so on */
    }

    console_push(c);
}

void kbd_initialize(void) {
    /* Drain anything the BIOS left in the output buffer, or the controller
     * won't raise a fresh interrupt. */
    while (inb(KBD_STATUS) & 0x01) {
        (void)inb(KBD_DATA);
    }

    irq_install_handler(1, kbd_on_irq);
    pic_unmask(1);
}
