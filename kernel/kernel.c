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
#include "pic.h"
#include "pit.h"
#include "vga.h"
#include "kbd.h"
#include "console.h"
#include "shell.h"
#include "pmm.h"
#include "gdt.h"
#include "vmm.h"

/* VGA text mode */
#define VIDEO_MEMORY_ADDRESS    0xB8000
#define SCREEN_WIDTH           80
#define SCREEN_HEIGHT          25
#define CHARACTERS_PER_SCREEN  (SCREEN_WIDTH * SCREEN_HEIGHT)

#define DEFAULT_FOREGROUND     COLOR_WHITE
#define DEFAULT_BACKGROUND     COLOR_BLACK
#define DEFAULT_ATTRIBUTE      ((DEFAULT_BACKGROUND << 4) | DEFAULT_FOREGROUND)

static struct {
    uint8_t x;
    uint8_t y;
} cursor_position = {0, 0};

void kernel_main(void);
void system_initialize(void);
void print_system_banner(void);
void print_system_information(void);
void scroll_screen(void);
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
#elif TEST_FAULT == 14
    kprintf("about to write to an unmapped address\n");
    /* Only faults now that paging is on. Before that this quietly scribbled
     * on whatever physical memory happened to be there. */
    *(volatile uint32_t*)0x40000000 = 1;
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

#ifdef TEST_FAULT
    trigger_test_fault();
    kprintf("BUG: still running after the fault\n");
#endif

    /* Prove the tick is real rather than trusting that it is. */
    uint32_t before = pit_ticks();
    sleep_ms(500);
    uint32_t after = pit_ticks();
    kprintf("timer: %u ticks over a 500ms sleep, uptime %ums\n",
            after - before, get_system_uptime());

    kprintf("kernel_main: init done, starting shell\n");
    shell_run();
}

void system_initialize(void) {
    serial_initialize();
    kprintf("\nMaxOS 0.1\n");

    /* First, so we stop running on the bootloader's descriptors. */
    gdt_initialize();
    kprintf("gdt: kernel descriptors loaded\n");

    idt_initialize();
    kprintf("idt: exceptions + IRQs installed, PIC remapped to 0x%02x\n",
            PIC_VECTOR_BASE);

    pmm_dump_map();
    pmm_initialize();
    pmm_selftest();

    vmm_initialize();
    vmm_selftest();

    pit_initialize(PIT_FREQUENCY_HZ);
    kprintf("pit: %uHz on IRQ0\n", PIT_FREQUENCY_HZ);

    kbd_initialize();
    kprintf("kbd: PS/2 on IRQ1\n");

    serial_enable_input();
    kprintf("serial: input on IRQ4, shell is drivable over COM1\n");

    __asm__ volatile("sti");
    kprintf("interrupts enabled\n");

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
        sleep_ms(100);
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

/* Step back one cell and blank it. Stops at the start of a line rather than
 * wrapping to the end of the previous one, which is wrong but is what the
 * shell's line editing expects for now. */
void backspace_character(void) {
    if (cursor_position.x == 0) return;

    cursor_position.x--;

    volatile uint16_t* video_memory = (volatile uint16_t*)VIDEO_MEMORY_ADDRESS;
    size_t offset = cursor_position.y * SCREEN_WIDTH + cursor_position.x;
    video_memory[offset] = (' ' | (DEFAULT_ATTRIBUTE << 8));
}


uint32_t get_system_uptime(void) {
    return pit_uptime_ms();
}
