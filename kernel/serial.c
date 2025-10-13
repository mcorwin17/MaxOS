#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "io.h"

#define COM1 0x3F8

/* 38400 8N1. Divisor 3 off the 115200 base clock. */
void serial_initialize(void) {
    outb(COM1 + 1, 0x00);   /* interrupts off while setting up */
    outb(COM1 + 3, 0x80);   /* DLAB on, ports 0 and 1 become the divisor */
    outb(COM1 + 0, 0x03);   /* divisor low */
    outb(COM1 + 1, 0x00);   /* divisor high */
    outb(COM1 + 3, 0x03);   /* 8 bits, no parity, one stop, DLAB off */
    outb(COM1 + 2, 0xC7);   /* FIFO on, cleared, 14 byte threshold */
    outb(COM1 + 4, 0x0B);   /* RTS/DSR */
}

void serial_write_char(char c) {
    while (!(inb(COM1 + 5) & 0x20)) {
        /* wait for the transmit holding register to drain */
    }
    outb(COM1, (uint8_t)c);
}

void serial_write(const char* str) {
    if (!str) return;

    for (size_t i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '\n') serial_write_char('\r');
        serial_write_char(str[i]);
    }
}


static void write_unsigned(uint32_t value, uint32_t base, int pad) {
    static const char digits[] = "0123456789abcdef";
    char buf[32];
    int i = 0;

    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value > 0) {
            buf[i++] = digits[value % base];
            value /= base;
        }
    }

    while (i < pad) buf[i++] = '0';

    while (i-- > 0) serial_write_char(buf[i]);
}

static void write_signed(int32_t value) {
    if (value < 0) {
        serial_write_char('-');
        /* cast before negating so INT32_MIN doesn't overflow */
        write_unsigned((uint32_t)(-(int64_t)value), 10, 0);
    } else {
        write_unsigned((uint32_t)value, 10, 0);
    }
}

void kprintf(const char* fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    for (size_t i = 0; fmt[i] != '\0'; ++i) {
        if (fmt[i] != '%') {
            if (fmt[i] == '\n') serial_write_char('\r');
            serial_write_char(fmt[i]);
            continue;
        }

        ++i;

        /* optional zero pad, e.g. %08x */
        int pad = 0;
        if (fmt[i] == '0') {
            ++i;
            while (fmt[i] >= '0' && fmt[i] <= '9') {
                pad = pad * 10 + (fmt[i] - '0');
                ++i;
            }
        }

        switch (fmt[i]) {
        case 's': {
            const char* s = __builtin_va_arg(args, const char*);
            serial_write(s ? s : "(null)");
            break;
        }
        case 'c':
            serial_write_char((char)__builtin_va_arg(args, int));
            break;
        case 'd':
            write_signed(__builtin_va_arg(args, int32_t));
            break;
        case 'u':
            write_unsigned(__builtin_va_arg(args, uint32_t), 10, pad);
            break;
        case 'x':
            write_unsigned(__builtin_va_arg(args, uint32_t), 16, pad);
            break;
        case 'p':
            serial_write("0x");
            write_unsigned((uint32_t)(uintptr_t)__builtin_va_arg(args, void*), 16, 8);
            break;
        case '%':
            serial_write_char('%');
            break;
        case '\0':
            --i;    /* trailing %, don't run off the end */
            break;
        default:
            serial_write_char('%');
            serial_write_char(fmt[i]);
            break;
        }
    }

    __builtin_va_end(args);
}
