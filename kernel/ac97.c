#include <stdint.h>

#include "ac97.h"
#include "pci.h"
#include "io.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "thread.h"
#include "spinlock.h"
#include "sine.h"

/* Native Audio Mixer (BAR0) */
#define NAM_RESET        0x00
#define NAM_MASTER_VOL   0x02
#define NAM_PCM_VOL      0x18

/* Native Audio Bus Master (BAR1). PCM OUT's box starts at 0x10. */
#define NABM_PO_BDBAR    0x10   /* buffer descriptor list, physical */
#define NABM_PO_CIV      0x14   /* current index the card is playing */
#define NABM_PO_LVI      0x15   /* last valid index we've filled */
#define NABM_PO_SR       0x16   /* status */
#define NABM_PO_PICB     0x18   /* samples left in the current buffer */
#define NABM_PO_CR       0x1B   /* control */
#define NABM_GLOB_CNT    0x2C
#define NABM_GLOB_STA    0x30

#define CR_RUN           0x01
#define CR_RESET         0x02
#define CR_LVBIE         0x04   /* last-valid-buffer interrupt */
#define CR_IOCE          0x10

#define SR_DCH           0x01   /* DMA controller halted */
#define SR_LVBCI         0x04
#define SR_BCIS          0x08

/* 32 descriptors is the hardware limit. Each buffer is a slice of one
 * contiguous allocation, which keeps the physical addresses trivially
 * correct - the card walks physical memory, not our page tables. */
#define BDL_COUNT        32
#define BUFFER_FRAMES    1024                       /* per descriptor */
#define BUFFER_SAMPLES   (BUFFER_FRAMES * AC97_CHANNELS)
#define BUFFER_BYTES     (BUFFER_SAMPLES * 2)

struct bdl_entry {
    uint32_t address;
    uint16_t samples;       /* SAMPLES, not frames and not bytes */
    uint16_t flags;
} __attribute__((packed));

static uint16_t nam, nabm;
static int      present;

static struct bdl_entry* bdl;       /* physical == virtual, identity mapped */
static int16_t*          buffers;
static uint32_t          next_index;    /* descriptor we'll fill next */
static int               running;
static struct spinlock   audio_lock;

int ac97_present(void) { return present; }

int ac97_initialize(void) {
    spin_init(&audio_lock, "ac97");
    present = 0;

    struct pci_device dev;
    if (!pci_find(0x8086, 0x2415, &dev)) {
        kprintf("ac97: no card\n");
        return -1;
    }

    pci_enable(&dev);
    nam  = (uint16_t)(dev.bar[0] & ~0x3u);
    nabm = (uint16_t)(dev.bar[1] & ~0x3u);

    kprintf("ac97: found at pci %u:%u, nam 0x%04x nabm 0x%04x\n",
            dev.bus, dev.slot, nam, nabm);

    /* Cold reset the controller, then the mixer. */
    outl(nabm + NABM_GLOB_CNT, 0x00000002);
    for (volatile int d = 0; d < 100000; ++d) { }
    outw(nam + NAM_RESET, 0);

    /* Volume registers are attenuation: 0 is loudest, 0x8000 is mute. The
     * sense being inverted is the classic first bug here - "no sound" and
     * "full volume" are one bit apart. */
    outw(nam + NAM_MASTER_VOL, 0x0000);
    outw(nam + NAM_PCM_VOL,    0x0000);

    /* The BDL and every buffer come from one contiguous run of frames, so
     * the descriptor addresses are just base + offset. */
    uint32_t total = sizeof(struct bdl_entry) * BDL_COUNT
                   + BUFFER_BYTES * BDL_COUNT;
    uint32_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Has to be one physically flat run. The first attempt allocated page by
     * page and checked the addresses came out adjacent, which failed on the
     * very first boot: the pmm selftest allocates a thousand frames and frees
     * them again, and vma/vmm do the same on a smaller scale, so by the time
     * a driver starts the low end of the bitmap is full of holes. The card
     * walks physical memory with no idea our pages are scattered. */
    uint32_t base = pmm_alloc_contiguous(pages);
    if (base == PMM_NO_FRAME) {
        kprintf("ac97: couldn't get %u contiguous frames for DMA\n", pages);
        return -1;
    }

    bdl     = (struct bdl_entry*)(uintptr_t)base;
    buffers = (int16_t*)(uintptr_t)(base + sizeof(struct bdl_entry) * BDL_COUNT);

    for (uint32_t i = 0; i < BDL_COUNT; ++i) {
        bdl[i].address = (uint32_t)(uintptr_t)&buffers[i * BUFFER_SAMPLES];
        bdl[i].samples = 0;
        bdl[i].flags   = 0;
    }

    /* Reset the PCM OUT engine and point it at our list. */
    outb(nabm + NABM_PO_CR, CR_RESET);
    for (volatile int d = 0; d < 100000; ++d) { }

    outl(nabm + NABM_PO_BDBAR, (uint32_t)(uintptr_t)bdl);
    outb(nabm + NABM_PO_LVI, 0);

    next_index = 0;
    running = 0;
    present = 1;

    kprintf("ac97: %u Hz %u-channel, %u x %u-frame buffers\n",
            AC97_RATE, AC97_CHANNELS, BDL_COUNT, BUFFER_FRAMES);
    return 0;
}

/* How many descriptors are queued but not yet consumed. */
static uint32_t queued_depth(void) {
    uint8_t civ = inb(nabm + NABM_PO_CIV);
    return (next_index - civ) & (BDL_COUNT - 1);
}

uint32_t ac97_play(const int16_t* frames, uint32_t frame_count) {
    if (!present) return 0;

    uint32_t done = 0;

    while (done < frame_count) {
        /* Leave one descriptor of headroom: filling the slot the card is
         * about to read is how you get a click. */
        while (running && queued_depth() >= BDL_COUNT - 2) {
            thread_sleep_ms(5);
        }

        uint32_t irq = spin_lock_irq(&audio_lock);

        uint32_t slot  = next_index;
        uint32_t chunk = frame_count - done;
        if (chunk > BUFFER_FRAMES) chunk = BUFFER_FRAMES;

        int16_t* dst = &buffers[slot * BUFFER_SAMPLES];
        for (uint32_t i = 0; i < chunk * AC97_CHANNELS; ++i) {
            dst[i] = frames[done * AC97_CHANNELS + i];
        }

        bdl[slot].samples = (uint16_t)(chunk * AC97_CHANNELS);
        bdl[slot].flags   = 0;

        next_index = (next_index + 1) & (BDL_COUNT - 1);

        /* LVI is the card's stop line: it plays up to and including this
         * index. Moving it is what actually releases the buffer. */
        outb(nabm + NABM_PO_LVI, (uint8_t)slot);

        if (!running) {
            outb(nabm + NABM_PO_CR, CR_RUN);
            running = 1;
        }

        spin_unlock_irq(&audio_lock, irq);

        done += chunk;
    }

    return done;
}

void ac97_drain(void) {
    if (!present || !running) return;

    /* DCH means the engine ran out of descriptors, i.e. everything queued
     * has been played. */
    for (int i = 0; i < 2000; ++i) {
        if (inb(nabm + NABM_PO_SR) & SR_DCH) break;
        thread_sleep_ms(5);
    }

    outb(nabm + NABM_PO_CR, 0);
    running = 0;
    next_index = 0;
    outb(nabm + NABM_PO_LVI, 0);
    outb(nabm + NABM_PO_CR, CR_RESET);
    for (volatile int d = 0; d < 100000; ++d) { }
    outl(nabm + NABM_PO_BDBAR, (uint32_t)(uintptr_t)bdl);
}

/* Full-circle sine from the stored quarter, by symmetry. phase is 0..1023. */
static int16_t sine(uint32_t phase) {
    phase &= 1023;

    if (phase < 256)  return  sine_quarter[phase];
    if (phase < 512)  return  sine_quarter[511 - phase];
    if (phase < 768)  return (int16_t)-sine_quarter[phase - 512];
    return (int16_t)-sine_quarter[1023 - phase];
}

int ac97_tone(uint32_t hz, uint32_t ms, uint32_t volume_pct) {
    if (!present) return -1;

    uint32_t total = (AC97_RATE * ms) / 1000;

    /* Phase in 1024ths of a cycle, stepped in 16.16 fixed point so the
     * frequency doesn't drift over the length of a note.
     *
     * Computed as separate whole and fractional parts rather than one 64-bit
     * divide: this is a freestanding 32-bit build with no libgcc, so a
     * `uint64_t / uint32_t` is a link error for __udivdi3 rather than an
     * instruction. Both halves below stay inside 32 bits - the remainder is
     * under 48000, and 48000 << 16 still fits. */
    uint32_t units = hz * 1024;
    uint32_t step  = ((units / AC97_RATE) << 16)
                   | (((units % AC97_RATE) << 16) / AC97_RATE);
    uint32_t phase = 0;

    static int16_t chunk[BUFFER_FRAMES * AC97_CHANNELS];

    while (total > 0) {
        uint32_t n = total > BUFFER_FRAMES ? BUFFER_FRAMES : total;

        for (uint32_t i = 0; i < n; ++i) {
            int32_t s = sine(phase >> 16);
            s = (s * (int32_t)volume_pct) / 100;

            chunk[i * 2]     = (int16_t)s;   /* both channels, same signal */
            chunk[i * 2 + 1] = (int16_t)s;

            phase += step;
        }

        ac97_play(chunk, n);
        total -= n;
    }

    return 0;
}

int ac97_demo(void) {
    if (!present) return -1;

    /* An ascending arpeggio - A4, C#5, E5, A5 - then the octave held. Four
     * distinct pitches with a known order gives the host something it can
     * check note by note rather than "audio happened". */
    static const uint32_t notes[] = { 440, 554, 659, 880 };

    for (int i = 0; i < 4; ++i) ac97_tone(notes[i], 250, 60);
    ac97_tone(880, 500, 60);

    ac97_drain();
    return 0;
}
