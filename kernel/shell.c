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
#include "thread.h"
#include "process.h"
#include "signal.h"
#include "ata.h"
#include "bcache.h"
#include "vfs.h"
#include "fat16.h"
#include "net.h"
#include "pci.h"
#include "fb.h"
#include "ac97.h"
#include "speaker.h"

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
static void cmd_ps(const char* args);
static void cmd_run(const char* args);
static void cmd_kill(const char* args);
static void cmd_disk(const char* args);
static void cmd_dtest(const char* args);
static void cmd_ls(const char* args);
static void cmd_cat(const char* args);
static void cmd_cksum(const char* args);
static void cmd_mkfs(const char* args);
static void cmd_net(const char* args);
static void cmd_ping(const char* args);
static void cmd_pci(const char* args);
static void cmd_ip(const char* args);
static void cmd_gfx(const char* args);
static void cmd_gcon(const char* args);
static void cmd_play(const char* args);
static void cmd_beep(const char* args);
static void cmd_sleep(const char* args);
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
    { "ps",     cmd_ps,     "list threads" },
    { "run",    cmd_run,    "run an embedded user program" },
    { "kill",   cmd_kill,   "send a signal: kill <pid> [sig]" },
    { "disk",   cmd_disk,   "drive identify and cache stats" },
    { "dtest",  cmd_dtest,  "write/flush/verify a disk sector" },
    { "ls",     cmd_ls,     "list a directory" },
    { "cat",    cmd_cat,    "print a file" },
    { "cksum",  cmd_cksum,  "length and byte sum of a file" },
    { "mkfs",   cmd_mkfs,   "format the raw disk as FAT16 (destroys it)" },
    { "net",    cmd_net,    "link state and packet counts" },
    { "ping",   cmd_ping,   "ping the gateway (or a.b.c.d)" },
    { "ip",     cmd_ip,     "set our address: ip a.b.c.d" },
    { "gfx",    cmd_gfx,    "draw the framebuffer test pattern" },
    { "play",   cmd_play,   "AC97: play a tone, or the demo with no args" },
    { "beep",   cmd_beep,   "PC speaker beep" },
    { "gcon",   cmd_gcon,   "move the console onto the framebuffer" },
    { "pci",    cmd_pci,    "list PCI devices" },
    { "sleep",  cmd_sleep,  "block this thread for N ms" },
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

static void cmd_ps(const char* args) {
    (void)args;
    console_write("  id pid name             state    ticks\n");
    thread_dump_console();
}

static void cmd_run(const char* args) {
    if (args[0] == '\0') {
        console_write("  run <name>\n");
        return;
    }

    uint32_t frames_before = pmm_free_frames();

    int pid = process_spawn(args);
    if (pid < 0) {
        console_write("  no such program: ");
        console_write(args);
        console_putchar('\n');
        return;
    }

    /* While it runs, Ctrl-C belongs to it. */
    console_set_foreground(process_find((uint32_t)pid));

    /* If it's still going after 200ms it's compute-bound, and this print
     * is the proof the kernel got the CPU back from ring 3. */
    thread_sleep_ms(200);
    if (process_is_live((uint32_t)pid)) {
        kprintf("kernel: still scheduling while pid %d runs\n", pid);
    }

    /* Reap until it's our pid that comes back - strays first. */
    for (;;) {
        uint32_t reaped = 0;
        int code = process_wait(&reaped);

        if (code == -1 && reaped == 0) {
            console_write("  nothing to wait for\n");
            return;
        }

        if (reaped != (uint32_t)pid) continue;

        console_write("  pid ");
        write_number((uint32_t)pid);

        if (code >= 128) {
            /* Unix convention: 128+sig. Ctrl-C reads as signal 2, a user
             * fault as signal 11. */
            console_write(" killed by signal ");
            write_number((uint32_t)(code - 128));
        } else {
            console_write(" exited with ");
            write_number((uint32_t)code);
        }
        console_putchar('\n');

        uint32_t frames_after = pmm_free_frames();
        console_write("  frame delta ");
        if (frames_after >= frames_before) {
            write_number(frames_after - frames_before);
        } else {
            console_putchar('-');
            write_number(frames_before - frames_after);
        }
        console_putchar('\n');
        return;
    }
}

static uint32_t parse_number(const char* s) {
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (uint32_t)(*s - '0'); ++s; }
    return n;
}

static void cmd_kill(const char* args) {
    uint32_t pid = parse_number(args);

    /* Optional signal after the pid; default SIGINT. */
    while (*args >= '0' && *args <= '9') ++args;
    args = skip_spaces(args);
    uint32_t sig = (*args) ? parse_number(args) : 2;

    struct process* p = process_find(pid);
    if (!p) {
        console_write("  no such pid\n");
        return;
    }

    signal_send(p, (int)sig);
    console_write("  sent\n");
}

static void cmd_disk(const char* args) {
    (void)args;

    if (!ata_present()) {
        console_write("  no disk attached\n");
        return;
    }

    console_write("  model   ");
    console_write(ata_model());
    console_putchar('\n');

    console_write("  size    ");
    write_number(ata_sector_count());
    console_write(" sectors (");
    write_number(ata_sector_count() / 2048);
    console_write(" MB)\n");

    console_write("  cache   ");
    write_number(bcache_hits());
    console_write(" hits, ");
    write_number(bcache_misses());
    console_write(" misses\n");
}

static void cmd_dtest(const char* args) {
    (void)args;
    bcache_selftest();
    console_write("  dtest done\n");
}

static void ls_emit(const char* name, uint32_t size, int is_dir, void* ctx) {
    (void)ctx;
    console_write("  ");
    console_write(name);

    uint32_t n = 0;
    while (name[n]) ++n;
    for (uint32_t pad = n; pad < 14; ++pad) console_putchar(' ');

    if (is_dir) {
        console_write("<dir>\n");
    } else {
        write_number(size);
        console_putchar('\n');
    }
}

static void cmd_ls(const char* args) {
    const char* path = (args[0] == '\0') ? "/" : args;

    if (vfs_list(path, ls_emit, 0) != 0) {
        console_write("  can't list ");
        console_write(path);
        console_putchar('\n');
    }
}

static void cmd_cat(const char* args) {
    if (args[0] == '\0') {
        console_write("  cat <path>\n");
        return;
    }

    char chunk[128];
    uint32_t off = 0;

    for (;;) {
        int got = vfs_read(args, off, chunk, sizeof(chunk));
        if (got < 0) {
            console_write("  can't read ");
            console_write(args);
            console_putchar('\n');
            return;
        }
        if (got == 0) return;

        for (int i = 0; i < got; ++i) console_putchar(chunk[i]);
        off += (uint32_t)got;
    }
}

static void cmd_cksum(const char* args) {
    if (args[0] == '\0') {
        console_write("  cksum <path>\n");
        return;
    }

    uint8_t chunk[512];
    uint32_t off = 0, sum = 0;

    for (;;) {
        int got = vfs_read(args, off, chunk, sizeof(chunk));
        if (got < 0) {
            console_write("  can't read ");
            console_write(args);
            console_putchar('\n');
            return;
        }
        if (got == 0) break;

        for (int i = 0; i < got; ++i) sum += chunk[i];
        off += (uint32_t)got;
    }

    console_write("  ");
    write_number(off);
    console_write(" bytes, sum ");
    write_number(sum);
    console_putchar('\n');
}

static void cmd_mkfs(const char* args) {
    (void)args;

    if (fat16_format() == 0) {
        console_write("  formatted and mounted\n");
    } else {
        console_write("  mkfs failed\n");
    }
}

static void cmd_net(const char* args) {
    (void)args;

    if (!net_up()) { console_write("  no network card\n"); return; }
    net_stats();
}

static void cmd_pci(const char* args) {
    (void)args;
    pci_dump();
}

/* "10.0.2.2" -> packed address. Returns 0 if it doesn't parse. */
static uint32_t parse_ip(const char* s) {
    uint32_t octet[4] = { 0, 0, 0, 0 };

    for (int i = 0; i < 4; ++i) {
        if (*s < '0' || *s > '9') return 0;

        uint32_t v = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (uint32_t)(*s - '0'); ++s; }
        if (v > 255) return 0;
        octet[i] = v;

        if (i < 3) {
            if (*s != '.') return 0;
            ++s;
        }
    }

    return (octet[0] << 24) | (octet[1] << 16) | (octet[2] << 8) | octet[3];
}

static void cmd_play(const char* args) {
    if (!ac97_present()) { console_write("  no sound card\n"); return; }

    if (!args[0]) {
        console_write("  playing the demo\n");
        ac97_demo();
        console_write("  done\n");
        return;
    }

    uint32_t hz = parse_number(args);
    if (hz < 20 || hz > 20000) { console_write("  play [20-20000]\n"); return; }

    console_write("  tone ");
    write_decimal_console(hz);
    console_write(" Hz\n");

    ac97_tone(hz, 1000, 60);
    ac97_drain();
}

static void cmd_beep(const char* args) {
    uint32_t hz = args[0] ? parse_number(args) : 880;
    if (hz < 20 || hz > 20000) hz = 880;

    speaker_beep(hz, 300);
    console_write("  beeped\n");
}

static void cmd_gfx(const char* args) {
    (void)args;

    if (!fb_present()) { console_write("  no framebuffer\n"); return; }

    fb_console_enable(0);       /* the demo owns the screen */
    fb_demo();
    console_write("  drew the test pattern\n");
}

static void cmd_gcon(const char* args) {
    (void)args;

    if (!fb_present()) { console_write("  no framebuffer\n"); return; }

    fb_console_enable(1);
    console_write("console is on the framebuffer now\n");
    console_write("1024x768, 128x48 cells, 8x16 glyphs\n");
}

static void cmd_ip(const char* args) {
    if (!args[0]) { console_write("  ip <a.b.c.d>\n"); return; }

    uint32_t ip = parse_ip(args);
    if (!ip) { console_write("  ip <a.b.c.d>\n"); return; }

    net_set_ip(ip);
    console_write("  ok\n");
}

static void cmd_ping(const char* args) {
    if (!net_up()) { console_write("  no network card\n"); return; }

    uint32_t target = NET_GATEWAY_IP;
    if (args[0]) {
        target = parse_ip(args);
        if (!target) { console_write("  ping <a.b.c.d>\n"); return; }
    }

    uint32_t before = net_ping_replies();

    /* Four, a second apart, like everyone else's ping. The first may be
     * lost to ARP resolution - that's the retry doing its job, not a
     * failure. */
    for (int i = 0; i < 4; ++i) {
        net_ping(target);
        thread_sleep_ms(300);
    }
    thread_sleep_ms(300);

    console_write("  sent 4, got ");
    write_decimal_console(net_ping_replies() - before);
    console_write(" replies\n");
}

static void cmd_sleep(const char* args) {
    uint32_t ms = parse_number(args);
    if (ms == 0) ms = 1000;

    uint32_t before = pit_uptime_ms();
    thread_sleep_ms(ms);
    uint32_t after = pit_uptime_ms();

    console_write("  slept ");
    write_number(after - before);
    console_write("ms of ");
    write_number(ms);
    console_write("ms requested\n");
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
