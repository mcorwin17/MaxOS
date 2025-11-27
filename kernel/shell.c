#include <stdint.h>
#include <stddef.h>

#include "shell.h"
#include "console.h"
#include "serial.h"
#include "vga.h"
#include "pit.h"
#include "panic.h"
#include "io.h"
#include "pmm.h"
#include "heap.h"
#include "vma.h"

#define LINE_MAX 128

/* No libc, so these live here until there's a string.c worth having. */
static int streq(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static const char* skip_spaces(const char* s) {
    while (*s == ' ') ++s;
    return s;
}

/* Splits at the first space: returns the argument tail, and nul-terminates
 * the command in place. */
static const char* split_command(char* line) {
    char* p = line;
    while (*p && *p != ' ') ++p;

    if (*p == '\0') return p;   /* no args, point at the terminator */

    *p = '\0';
    return skip_spaces(p + 1);
}


static void cmd_help(const char* args);
static void cmd_clear(const char* args);
static void cmd_uptime(const char* args);
static void cmd_ticks(const char* args);
static void cmd_echo(const char* args);
static void cmd_mem(const char* args);
static void cmd_heap(const char* args);
static void cmd_vm(const char* args);
static void cmd_reboot(const char* args);
static void cmd_panic(const char* args);

struct command {
    const char* name;
    void (*run)(const char* args);
    const char* summary;
};

static const struct command commands[] = {
    { "help",   cmd_help,   "list commands" },
    { "clear",  cmd_clear,  "clear the screen" },
    { "uptime", cmd_uptime, "milliseconds since boot" },
    { "ticks",  cmd_ticks,  "raw timer tick count" },
    { "echo",   cmd_echo,   "print the rest of the line" },
    { "mem",    cmd_mem,    "physical frame usage" },
    { "heap",   cmd_heap,   "kernel heap usage" },
    { "vm",     cmd_vm,     "reserved regions and resident pages" },
    { "reboot", cmd_reboot, "reset via the keyboard controller" },
    { "panic",  cmd_panic,  "deliberately panic, to see the handler" },
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

/* Small enough that a real printf isn't worth it yet. */
static void write_number(uint32_t value) {
    char buf[12];
    int i = 0;

    if (value == 0) {
        console_putchar('0');
        return;
    }

    while (value > 0) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i-- > 0) console_putchar(buf[i]);
}

static void cmd_help(const char* args) {
    (void)args;
    for (size_t i = 0; i < COMMAND_COUNT; ++i) {
        console_write("  ");
        console_write(commands[i].name);

        /* pad to a column so the summaries line up */
        size_t n = 0;
        while (commands[i].name[n]) ++n;
        for (size_t pad = n; pad < 8; ++pad) console_putchar(' ');

        console_write(commands[i].summary);
        console_putchar('\n');
    }
}

static void cmd_clear(const char* args) {
    (void)args;
    clear_screen();
    set_cursor_position(0, 0);
}

static void cmd_uptime(const char* args) {
    (void)args;
    write_number(pit_uptime_ms());
    console_write("ms\n");
}

static void cmd_ticks(const char* args) {
    (void)args;
    write_number(pit_ticks());
    console_putchar('\n');
}

static void cmd_echo(const char* args) {
    console_write(args);
    console_putchar('\n');
}

static void cmd_mem(const char* args) {
    (void)args;

    uint32_t total = pmm_total_frames();
    uint32_t free  = pmm_free_frames();

    console_write("  total   ");
    write_number(total);
    console_write(" frames (");
    write_number(total / 256);          /* 256 frames to the megabyte */
    console_write(" MB addressable)\n");

    console_write("  free    ");
    write_number(free);
    console_write(" frames (");
    write_number(free / 256);
    console_write(" MB)\n");

    console_write("  used    ");
    write_number(total - free);
    console_write(" frames\n");

    console_write("  usable  ");
    write_number(pmm_usable_bytes() / (1024 * 1024));
    console_write(" MB reported by the BIOS\n");
}

static void cmd_heap(const char* args) {
    (void)args;

    console_write("  size    ");
    write_number(heap_size() / 1024);
    console_write(" KB mapped at 0xd0000000\n");

    console_write("  live    ");
    write_number(heap_live_allocations());
    console_write(" allocations, ");
    write_number(heap_live_bytes());
    console_write(" bytes\n");
}

static void cmd_vm(const char* args) {
    (void)args;

    console_write("  regions  ");
    write_number(vma_region_count());
    console_putchar('\n');

    console_write("  resident ");
    write_number(vma_resident_pages());
    console_write(" pages faulted in on demand\n");
}

static void cmd_reboot(const char* args) {
    (void)args;
    console_write("rebooting\n");

    /* Pulse the 8042 reset line. */
    while (inb(0x64) & 0x02) { }
    outb(0x64, 0xFE);

    /* If that didn't take, fall back to a triple fault by loading a null IDT
     * and interrupting. */
    for (;;) __asm__ volatile("hlt");
}

static void cmd_panic(const char* args) {
    (void)args;
    panic("panic requested from the shell");
}


static void run_line(char* line) {
    const char* args = split_command(line);

    if (line[0] == '\0') return;

    for (size_t i = 0; i < COMMAND_COUNT; ++i) {
        if (streq(line, commands[i].name)) {
            commands[i].run(args);
            return;
        }
    }

    console_write("unknown command: ");
    console_write(line);
    console_write("\n  try 'help'\n");
}

void shell_run(void) {
    char line[LINE_MAX];

    console_write("\ntype 'help' for commands\n");

    for (;;) {
        console_write("> ");

        size_t length = 0;
        for (;;) {
            char c = console_getchar();

            if (c == '\n') {
                console_putchar('\n');
                break;
            }

            if (c == '\b' || c == 0x7F) {       /* backspace or delete */
                if (length > 0) {
                    --length;
                    console_putchar('\b');
                }
                continue;
            }

            if (c < 32 || c > 126) continue;    /* ignore the rest for now */

            if (length < LINE_MAX - 1) {
                line[length++] = c;
                console_putchar(c);
            }
        }

        line[length] = '\0';
        run_line(line);
    }
}
