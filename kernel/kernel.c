/*
 * MaxOS kernel.
 *
 * Entered from the bootloader at 0x1000, already in 32-bit protected mode with
 * a flat GDT. Freestanding: no libc, no runtime, nothing set up for us.
 */

#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "idt.h"
#include "panic.h"

/* VGA text mode */
#define VIDEO_MEMORY_ADDRESS    0xB8000
#define SCREEN_WIDTH           80
#define SCREEN_HEIGHT          25
#define CHARACTERS_PER_SCREEN  (SCREEN_WIDTH * SCREEN_HEIGHT)

#define COLOR_BLACK            0x00
#define COLOR_BLUE             0x01
#define COLOR_GREEN            0x02
#define COLOR_CYAN             0x03
#define COLOR_RED              0x04
#define COLOR_MAGENTA          0x05
#define COLOR_BROWN            0x06
#define COLOR_LIGHT_GRAY       0x07
#define COLOR_DARK_GRAY        0x08
#define COLOR_LIGHT_BLUE       0x09
#define COLOR_LIGHT_GREEN      0x0A
#define COLOR_LIGHT_CYAN       0x0B
#define COLOR_LIGHT_RED        0x0C
#define COLOR_LIGHT_MAGENTA    0x0D
#define COLOR_YELLOW           0x0E
#define COLOR_WHITE            0x0F

#define DEFAULT_FOREGROUND     COLOR_WHITE
#define DEFAULT_BACKGROUND     COLOR_BLACK
#define DEFAULT_ATTRIBUTE      ((DEFAULT_BACKGROUND << 4) | DEFAULT_FOREGROUND)

static struct {
    uint8_t x;
    uint8_t y;
} cursor_position = {0, 0};

void kernel_main(void);
void system_initialize(void);
void clear_screen(void);
void set_cursor_position(uint8_t x, uint8_t y);
void print_character(char c);
void print_string(const char* str);
void print_colored_string(const char* str, uint8_t color);
void print_system_banner(void);
void print_system_information(void);
void print_status_message(void);
void scroll_screen(void);
void delay_milliseconds(uint32_t ms);
uint32_t get_system_uptime(void);


#ifdef TEST_FAULT
/* Deliberately fault, to check the IDT actually reports things.
 *
 * These are all inline asm on purpose. Writing `1 / zero` in C and hoping is
 * useless: division by zero is undefined behaviour, so at -O2 the compiler is
 * entitled to delete the whole thing, and it does. Marking the operands
 * volatile doesn't help either - it forces the loads, not the divide.
 *
 * No page fault test here. There's no paging yet, so a wild pointer just
 * writes to whatever physical memory it names and nothing objects. */
static void trigger_test_fault(void) {
#if TEST_FAULT == 0
    kprintf("about to divide by zero\n");
    __asm__ volatile(
        "xor %%edx, %%edx\n\t"
        "mov $1, %%eax\n\t"
        "xor %%ecx, %%ecx\n\t"
        "div %%ecx"
        : : : "eax", "ecx", "edx");
#elif TEST_FAULT == 6
    kprintf("about to execute ud2\n");
    __asm__ volatile("ud2");
#elif TEST_FAULT == 13
    kprintf("about to load a garbage segment selector\n");
    /* 0x50 is way past the end of a three entry GDT */
    __asm__ volatile(
        "mov $0x50, %%eax\n\t"
        "mov %%eax, %%ds"
        : : : "eax");
#else
    kprintf("no test for vector %d\n", TEST_FAULT);
#endif
}
#endif


void _start(void) {
    kernel_main();

    while (1) {
        __asm__ volatile("hlt");
    }
}

void kernel_main(void) {
    system_initialize();
    kprintf("kernel_main: entered\n");

    print_system_banner();
    print_system_information();
    print_status_message();

#ifdef TEST_FAULT
    trigger_test_fault();
    kprintf("BUG: still running after the fault\n");
#endif

    kprintf("kernel_main: init done, halting\n");
}

void system_initialize(void) {
    serial_initialize();
    kprintf("\nMaxOS 0.1\n");

    idt_initialize();
    kprintf("idt: 32 exception handlers installed\n");

    clear_screen();
    set_cursor_position(0, 0);
}


void clear_screen(void) {
    volatile uint16_t* video_memory = (volatile uint16_t*)VIDEO_MEMORY_ADDRESS;

    for (size_t i = 0; i < CHARACTERS_PER_SCREEN; ++i) {
        video_memory[i] = (' ' | (DEFAULT_ATTRIBUTE << 8));
    }

    cursor_position.x = 0;
    cursor_position.y = 0;
}

void set_cursor_position(uint8_t x, uint8_t y) {
    if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH - 1;
    if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;

    cursor_position.x = x;
    cursor_position.y = y;
}

/* Each cell is two bytes: character in the low byte, attribute in the high. */
void print_character(char c) {
    volatile uint16_t* video_memory = (volatile uint16_t*)VIDEO_MEMORY_ADDRESS;

    if (c == '\n') {
        cursor_position.x = 0;
        cursor_position.y++;

        if (cursor_position.y >= SCREEN_HEIGHT) {
            scroll_screen();
            cursor_position.y = SCREEN_HEIGHT - 1;
        }
        return;
    }

    if (c == '\r') {
        cursor_position.x = 0;
        return;
    }

    if (c == '\t') {
        cursor_position.x = (cursor_position.x + 8) & 0xF8;
        if (cursor_position.x >= SCREEN_WIDTH) {
            cursor_position.x = 0;
            cursor_position.y++;
        }
        return;
    }

    size_t offset = cursor_position.y * SCREEN_WIDTH + cursor_position.x;
    video_memory[offset] = (c | (DEFAULT_ATTRIBUTE << 8));

    cursor_position.x++;
    if (cursor_position.x >= SCREEN_WIDTH) {
        cursor_position.x = 0;
        cursor_position.y++;

        if (cursor_position.y >= SCREEN_HEIGHT) {
            scroll_screen();
            cursor_position.y = SCREEN_HEIGHT - 1;
        }
    }
}

void print_string(const char* str) {
    if (!str) return;

    for (size_t i = 0; str[i] != '\0'; ++i) {
        print_character(str[i]);
    }
}

void print_colored_string(const char* str, uint8_t color) {
    if (!str) return;

    volatile uint16_t* video_memory = (volatile uint16_t*)VIDEO_MEMORY_ADDRESS;

    for (size_t i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '\n') {
            print_character('\n');
        } else {
            size_t offset = cursor_position.y * SCREEN_WIDTH + cursor_position.x;
            video_memory[offset] = (str[i] | (color << 8));

            cursor_position.x++;
            if (cursor_position.x >= SCREEN_WIDTH) {
                cursor_position.x = 0;
                cursor_position.y++;
                if (cursor_position.y >= SCREEN_HEIGHT) {
                    scroll_screen();
                    cursor_position.y = SCREEN_HEIGHT - 1;
                }
            }
        }
    }
}

void scroll_screen(void) {
    volatile uint16_t* video_memory = (volatile uint16_t*)VIDEO_MEMORY_ADDRESS;

    for (size_t i = 0; i < (SCREEN_HEIGHT - 1) * SCREEN_WIDTH; ++i) {
        video_memory[i] = video_memory[i + SCREEN_WIDTH];
    }

    size_t bottom_line_start = (SCREEN_HEIGHT - 1) * SCREEN_WIDTH;
    for (size_t i = 0; i < SCREEN_WIDTH; ++i) {
        video_memory[bottom_line_start + i] = (' ' | (DEFAULT_ATTRIBUTE << 8));
    }
}


void print_system_banner(void) {
    set_cursor_position(0, 2);

    const char* logo[] = {
        "  __  __       _  ___   ___ ",
        " |  \\/  |     / \\/ __\\ / __\\",
        " | \\  / |    / _ \\__ \\ / /   ",
        " | |\\/| |   / ___ \\__// /___ ",
        " |_|  |_|  /_/   \\_\\/_____| "
    };

    for (int i = 0; i < 5; ++i) {
        set_cursor_position(25, 2 + i);
        print_colored_string(logo[i], COLOR_CYAN);
        delay_milliseconds(100);
    }

    set_cursor_position(0, 8);
    print_colored_string("MaxOS 0.1", COLOR_YELLOW);
}

void print_system_information(void) {
    set_cursor_position(0, 12);
    print_colored_string("System Information:", COLOR_LIGHT_GREEN);

    set_cursor_position(2, 13);
    print_string("Architecture: x86 (32-bit protected mode)");

    set_cursor_position(2, 14);
    print_string("Memory Model: Flat memory model with segmentation");

    set_cursor_position(2, 15);
    print_string("Video Mode: VGA text mode (80x25, 16 colors)");

    set_cursor_position(2, 16);
    print_string("Boot Method: BIOS bootloader with kernel loading");
}

/* No prompt here on purpose. There's no PIC and no keyboard driver, so
 * nothing can read a keystroke - printing a "> " would just be a lie. */
void print_status_message(void) {
    set_cursor_position(0, 20);
    print_colored_string("System Status: Ready", COLOR_LIGHT_GREEN);

    set_cursor_position(0, 21);
    print_colored_string("No input driver yet - halting after init", COLOR_LIGHT_GRAY);
}


/* Busy-wait. Wildly inaccurate and machine-dependent - goes away once the PIT
 * is wired up. */
void delay_milliseconds(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 10000; ++i) {
        __asm__ volatile("nop");
    }
}

uint32_t get_system_uptime(void) {
    return 0;   /* needs the PIT */
}
