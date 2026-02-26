#include <stdint.h>

#include "pit.h"
#include "pic.h"
#include "idt.h"
#include "io.h"
#include "panic.h"
#include "thread.h"
#include "smp.h"

#define PIT_CHANNEL0   0x40
#define PIT_COMMAND    0x43

/* channel 0, lobyte/hibyte, mode 3 (square wave), binary */
#define PIT_MODE       0x36

#define PIT_BASE_HZ    1193182

static volatile uint32_t ticks = 0;
static uint32_t tick_hz = PIT_FREQUENCY_HZ;

/* The timer runs long before there's a scheduler to drive. */
static int threads_running = 0;

void pit_enable_preemption(void) {
    threads_running = 1;
}

/* Runs in interrupt context: no blocking, no allocating, nothing that takes a
 * lock the rest of the kernel holds. Bump the counter and get out. */
static void pit_on_tick(struct registers* r) {
    (void)r;
    ticks++;

    if (threads_running) {
        thread_tick(ticks);

        /* The PIC only ever interrupts the boot CPU, so the other CPUs
         * would run whatever they picked forever. The tick becomes their
         * preemption too, by proxy. */
        lapic_broadcast_resched();
    }
}

void pit_initialize(uint32_t frequency_hz) {
    if (frequency_hz == 0) frequency_hz = PIT_FREQUENCY_HZ;
    tick_hz = frequency_hz;

    uint32_t divisor = PIT_BASE_HZ / frequency_hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    irq_install_handler(0, pit_on_tick);

    outb(PIT_COMMAND, PIT_MODE);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    pic_unmask(0);
}

uint32_t pit_ticks(void) {
    return ticks;
}

uint32_t pit_uptime_ms(void) {
    return (ticks * 1000u) / tick_hz;
}

void sleep_ms(uint32_t ms) {
    uint32_t target = ticks + ((ms * tick_hz) + 999u) / 1000u;

    while (ticks < target) {
        /* hlt rather than spin: without it qemu pins a host core and it looks
         * like the kernel is doing something expensive when it is doing
         * nothing at all. */
        __asm__ volatile("hlt");
    }
}
