#include <stdint.h>
#include <stddef.h>

#include "pmm.h"
#include "serial.h"
#include "panic.h"
#include "spinlock.h"

/* Where the bootloader left the BIOS map. Must match boot.asm. */
#define E820_COUNT_ADDR    0x500
#define E820_ENTRIES_ADDR  0x504

#define E820_USABLE        1

struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed));

/* From link.ld. Only the address matters, never the contents. */
extern char kernel_end[];

static uint32_t* bitmap;
static uint32_t  bitmap_words;
static uint32_t  total_frames;
static uint32_t  free_frames;
static uint32_t  usable_bytes;

/* One byte per frame, right after the bitmap. Only fork shares frames, so a
 * byte (255 owners) is plenty. */
static uint8_t*  refcounts;
static uint32_t  map_limit;

static struct spinlock pmm_lock;

static inline void mark_used(uint32_t frame) {
    uint32_t word = frame / 32, bit = frame % 32;
    if (word >= bitmap_words) return;

    if (!(bitmap[word] & (1u << bit))) {
        bitmap[word] |= (1u << bit);
        free_frames--;
    }
}

static inline void mark_free(uint32_t frame) {
    uint32_t word = frame / 32, bit = frame % 32;
    if (word >= bitmap_words) return;

    if (bitmap[word] & (1u << bit)) {
        bitmap[word] &= ~(1u << bit);
        free_frames++;
    }
}

static inline int is_used(uint32_t frame) {
    uint32_t word = frame / 32, bit = frame % 32;
    if (word >= bitmap_words) return 1;
    return (bitmap[word] & (1u << bit)) != 0;
}

static void reserve_range(uint32_t start, uint32_t end) {
    uint32_t first = start / PAGE_SIZE;
    uint32_t last  = (end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t f = first; f < last && f < total_frames; ++f) {
        mark_used(f);
    }
}

static const struct e820_entry* map_entries(void) {
    return (const struct e820_entry*)E820_ENTRIES_ADDR;
}

static uint32_t map_count(void) {
    return *(const uint32_t*)E820_COUNT_ADDR;
}

void pmm_dump_map(void) {
    uint32_t count = map_count();

    if (count == 0) {
        kprintf("e820: BIOS gave us nothing\n");
        return;
    }

    const struct e820_entry* e = map_entries();
    for (uint32_t i = 0; i < count; ++i) {
        /* 32 bit kernel, so anything above 4G is not addressable here and the
         * high halves only matter for spotting that case. */
        kprintf("e820: %08x%08x len %08x%08x type %u%s\n",
                (uint32_t)(e[i].base >> 32),   (uint32_t)e[i].base,
                (uint32_t)(e[i].length >> 32), (uint32_t)e[i].length,
                e[i].type,
                e[i].type == E820_USABLE ? " (usable)" : "");
    }
}

void pmm_initialize(void) {
    uint32_t count = map_count();

    if (count == 0) {
        panic("no E820 memory map, refusing to guess how much RAM exists");
    }

    const struct e820_entry* e = map_entries();

    /* Highest usable byte below 4G decides how big the bitmap has to be.
     * Separately, the highest byte of ANY region decides how far to identity
     * map - ACPI tables live in reserved regions just above usable RAM. */
    uint64_t highest = 0, highest_any = 0;
    usable_bytes = 0;

    for (uint32_t i = 0; i < count; ++i) {
        uint64_t end = e[i].base + e[i].length;
        if (end > 0xFFFFFFFFull) end = 0xFFFFFFFFull;

        /* Skip the memory-mapped hardware regions right below 4G - mapping
         * those is a separate decision, not "RAM the kernel can poke". */
        if (e[i].base < 0xF0000000ull && end > highest_any) highest_any = end;

        if (e[i].type != E820_USABLE) continue;

        if (end > highest) highest = end;
        if (e[i].base < 0xFFFFFFFFull) {
            usable_bytes += (uint32_t)(end - e[i].base);
        }
    }

    map_limit = (uint32_t)highest_any;

    total_frames = (uint32_t)(highest / PAGE_SIZE);
    bitmap_words = (total_frames + 31) / 32;

    /* Bitmap goes at 1M, not straight after the kernel.
     *
     * Putting it at kernel_end looks tidier and is a trap: kernel_end used to
     * be around 0x7000, the bitmap is a few KB, and it ran straight over the
     * boot sector at 0x7C00 - which is where the GDT lived at the time. The
     * CPU re-reads the GDT on every segment register load, and isr_common
     * reloads DS on every single interrupt, so the first timer tick after sti
     * faulted on a descriptor table made of bitmap. Everything below 1M is
     * either taken or too close to something that is. */
    uint32_t bitmap_addr = 0x100000;
    bitmap = (uint32_t*)(uintptr_t)bitmap_addr;

    /* Refcounts live on the page after the bitmap. */
    refcounts = (uint8_t*)(uintptr_t)(bitmap_addr + 0x1000);
    for (uint32_t i = 0; i < total_frames; ++i) refcounts[i] = 0;

    spin_init(&pmm_lock, "pmm");

    /* Start with everything used, then punch out what the BIOS says is real.
     * Safer than the other way round: a region nobody told us about stays
     * unavailable instead of getting handed out. */
    for (uint32_t i = 0; i < bitmap_words; ++i) bitmap[i] = 0xFFFFFFFF;
    free_frames = 0;

    for (uint32_t i = 0; i < count; ++i) {
        if (e[i].type != E820_USABLE) continue;
        if (e[i].base > 0xFFFFFFFFull) continue;

        uint32_t start = (uint32_t)e[i].base;
        uint64_t end64 = e[i].base + e[i].length;
        uint32_t end   = (end64 > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)end64;

        /* Round the start up and the end down, so a partly usable frame is
         * never handed out as a whole one. */
        uint32_t first = (start + PAGE_SIZE - 1) / PAGE_SIZE;
        uint32_t last  = end / PAGE_SIZE;

        for (uint32_t f = first; f < last && f < total_frames; ++f) {
            mark_free(f);
        }
    }

    /* Now take back the things that are real memory but not ours to give:
     * everything below 1M (BIOS, video, the E820 map itself), the kernel, and
     * the bitmap. */
    reserve_range(0, 0x100000);
    reserve_range(0x10000, (uint32_t)(uintptr_t)kernel_end);
    reserve_range(bitmap_addr, bitmap_addr + 0x1000 + total_frames);

    kprintf("pmm: %u frames, %u free, bitmap %u bytes at 0x%08x\n",
            total_frames, free_frames, bitmap_words * 4, bitmap_addr);
}

uint32_t pmm_alloc_frame(void) {
    uint32_t flags = spin_lock_irq(&pmm_lock);

    for (uint32_t word = 0; word < bitmap_words; ++word) {
        if (bitmap[word] == 0xFFFFFFFF) continue;   /* all taken, skip 32 */

        for (uint32_t bit = 0; bit < 32; ++bit) {
            if (bitmap[word] & (1u << bit)) continue;

            uint32_t frame = word * 32 + bit;
            if (frame >= total_frames) break;

            mark_used(frame);
            refcounts[frame] = 1;
            spin_unlock_irq(&pmm_lock, flags);
            return frame * PAGE_SIZE;
        }
    }

    spin_unlock_irq(&pmm_lock, flags);
    return PMM_NO_FRAME;
}

void pmm_free_frame(uint32_t addr) {
    uint32_t frame = addr / PAGE_SIZE;

    if (frame >= total_frames) {
        panic("pmm_free_frame: address past the end of memory");
    }
    if (!is_used(frame)) {
        panic("pmm_free_frame: double free");
    }

    uint32_t flags = spin_lock_irq(&pmm_lock);

    /* Shared frame: just drop this claim. */
    if (refcounts[frame] > 1) {
        refcounts[frame]--;
        spin_unlock_irq(&pmm_lock, flags);
        return;
    }

    refcounts[frame] = 0;
    mark_free(frame);
    spin_unlock_irq(&pmm_lock, flags);
}

void pmm_ref(uint32_t addr) {
    uint32_t frame = addr / PAGE_SIZE;

    if (frame >= total_frames || !is_used(frame)) {
        panic("pmm_ref: frame isn't allocated");
    }
    if (refcounts[frame] == 0xFF) {
        panic("pmm_ref: refcount overflow");
    }

    uint32_t flags = spin_lock_irq(&pmm_lock);
    refcounts[frame]++;
    spin_unlock_irq(&pmm_lock, flags);
}

uint32_t pmm_refcount(uint32_t addr) {
    uint32_t frame = addr / PAGE_SIZE;
    if (frame >= total_frames) return 0;
    return refcounts[frame];
}

#define SELFTEST_COUNT 1024

void pmm_selftest(void) {
    static uint32_t got[SELFTEST_COUNT];

    uint32_t baseline = free_frames;
    uint32_t n = 0;

    for (; n < SELFTEST_COUNT; ++n) {
        got[n] = pmm_alloc_frame();
        if (got[n] == PMM_NO_FRAME) break;

        /* The scan hands them out low to high, so anything that isn't
         * strictly increasing means a frame came back twice. */
        if (n > 0 && got[n] <= got[n - 1]) {
            panic("pmm selftest: frame handed out twice");
        }
        if (got[n] & (PAGE_SIZE - 1)) {
            panic("pmm selftest: unaligned frame");
        }
    }

    if (free_frames != baseline - n) {
        panic("pmm selftest: free count wrong after allocating");
    }

    for (uint32_t i = 0; i < n; ++i) {
        pmm_free_frame(got[i]);
    }

    if (free_frames != baseline) {
        panic("pmm selftest: frames leaked");
    }

    kprintf("pmm: selftest ok, %u frames allocated and returned\n", n);
}

uint32_t pmm_map_limit(void)    { return map_limit; }
uint32_t pmm_total_frames(void) { return total_frames; }
uint32_t pmm_free_frames(void)  { return free_frames; }
uint32_t pmm_usable_bytes(void) { return usable_bytes; }
