#include <stdint.h>
#include <stddef.h>

#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "panic.h"

/* Canaries either side of every allocation. A heap overflow caught on the
 * next free costs an hour; the same overflow found three weeks later costs a
 * weekend. */
#define HEADER_MAGIC  0x4B484452u   /* KHDR */
#define FOOTER_MAGIC  0x4B464F54u   /* KFOT */

#define ALIGNMENT     8
#define ALIGN_UP(n)   (((n) + (ALIGNMENT - 1)) & ~((size_t)ALIGNMENT - 1))

/* Header and footer are both padded to a multiple of ALIGNMENT. Without that
 * the header is 20 bytes and the footer 4, so every payload lands 4 aligned
 * and each split shifts the next block further out of true. */
struct block {
    uint32_t      magic;
    uint32_t      size;      /* payload bytes, not counting header or footer */
    struct block* next;      /* address ordered, every block, free or not */
    struct block* prev;
    uint32_t      free;
    uint32_t      pad;       /* keeps sizeof(struct block) at 24 */
};

#define FOOTER_SIZE   8      /* only 4 are used, the rest is alignment */

#define FOOTER_OF(b)  (*(uint32_t*)((uint8_t*)((b) + 1) + (b)->size))
#define PAYLOAD(b)    ((void*)((b) + 1))
#define TOTAL(size)   (sizeof(struct block) + (size) + FOOTER_SIZE)

static struct block* first_block;
static uint32_t heap_end;          /* virtual address one past the heap */
static uint32_t live_allocations;
static uint32_t live_bytes;

/* Backs [heap_end, heap_end + bytes) with real frames. */
static int heap_grow(uint32_t bytes) {
    uint32_t start = heap_end;
    uint32_t end   = start + ((bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    if (end > HEAP_BASE + HEAP_MAX) return 0;

    for (uint32_t v = start; v < end; v += PAGE_SIZE) {
        uint32_t frame = pmm_alloc_frame();
        if (frame == PMM_NO_FRAME) return 0;

        vmm_map(v, frame, PAGE_WRITE);
    }

    heap_end = end;
    return 1;
}

static void set_canaries(struct block* b) {
    b->magic     = HEADER_MAGIC;
    FOOTER_OF(b) = FOOTER_MAGIC;
}

static void check_canaries(struct block* b, const char* where) {
    if (b->magic != HEADER_MAGIC) {
        kprintf("heap: header canary at %p is 0x%08x, expected 0x%08x\n",
                (void*)b, b->magic, HEADER_MAGIC);
        panic(where);
    }
    if (FOOTER_OF(b) != FOOTER_MAGIC) {
        kprintf("heap: footer canary after %p is 0x%08x, expected 0x%08x\n",
                PAYLOAD(b), FOOTER_OF(b), FOOTER_MAGIC);
        panic(where);
    }
}

void heap_initialize(void) {
    heap_end = HEAP_BASE;

    if (!heap_grow(HEAP_INITIAL)) {
        panic("heap: couldn't map the initial region");
    }

    first_block        = (struct block*)HEAP_BASE;
    first_block->size  = HEAP_INITIAL - TOTAL(0);
    first_block->next  = 0;
    first_block->prev  = 0;
    first_block->free  = 1;
    set_canaries(first_block);

    live_allocations = 0;
    live_bytes       = 0;

    kprintf("heap: %u KB at 0x%08x\n", HEAP_INITIAL / 1024, HEAP_BASE);
}

/* Cut a block in two if the leftover is worth having. */
static void split(struct block* b, size_t wanted) {
    if (b->size < wanted + TOTAL(ALIGNMENT)) return;

    struct block* rest =
        (struct block*)((uint8_t*)PAYLOAD(b) + wanted + FOOTER_SIZE);

    rest->size = b->size - wanted - TOTAL(0);
    rest->free = 1;
    rest->next = b->next;
    rest->prev = b;
    if (rest->next) rest->next->prev = rest;
    set_canaries(rest);

    b->size = (uint32_t)wanted;
    b->next = rest;
    set_canaries(b);
}

void* kmalloc(size_t bytes) {
    if (bytes == 0) return 0;

    size_t wanted = ALIGN_UP(bytes);

    struct block* b = first_block;
    struct block* last = b;

    while (b) {
        check_canaries(b, "kmalloc: heap corrupted");

        if (b->free && b->size >= wanted) {
            split(b, wanted);
            b->free = 0;

            live_allocations++;
            live_bytes += b->size;
            return PAYLOAD(b);
        }

        last = b;
        b = b->next;
    }

    /* Nothing big enough. Grow, hang a new block off the end, and retry
     * through the same path so splitting and accounting stay in one place. */
    uint32_t previous_end = heap_end;
    if (!heap_grow(TOTAL(wanted))) return 0;

    struct block* fresh = (struct block*)previous_end;
    fresh->size = (heap_end - previous_end) - TOTAL(0);
    fresh->free = 1;
    fresh->next = 0;
    fresh->prev = last;
    set_canaries(fresh);
    last->next = fresh;

    split(fresh, wanted);
    fresh->free = 0;

    live_allocations++;
    live_bytes += fresh->size;
    return PAYLOAD(fresh);
}

/* Merge b with the block after it, if both are free and adjacent. */
static void coalesce_forward(struct block* b) {
    struct block* n = b->next;
    if (!n || !n->free || !b->free) return;

    uint8_t* expected = (uint8_t*)PAYLOAD(b) + b->size + FOOTER_SIZE;
    if ((uint8_t*)n != expected) return;    /* a gap, so not adjacent */

    b->size += TOTAL(n->size);
    b->next = n->next;
    if (n->next) n->next->prev = b;
    set_canaries(b);
}

void kfree(void* ptr) {
    if (!ptr) return;

    struct block* b = (struct block*)ptr - 1;
    check_canaries(b, "kfree: bad pointer or heap corrupted");

    if (b->free) panic("kfree: double free");

    b->free = 1;
    live_allocations--;
    live_bytes -= b->size;

    coalesce_forward(b);
    if (b->prev) coalesce_forward(b->prev);
}

#define TEST_N 64

static size_t test_size(int i) {
    return 8 + (size_t)((i * 13) % 200);
}

void heap_selftest(void) {
    uint32_t base_allocs = live_allocations;
    uint32_t base_bytes  = live_bytes;

    static void* p[TEST_N];

    for (int i = 0; i < TEST_N; ++i) {
        size_t n = test_size(i);

        p[i] = kmalloc(n);
        if (!p[i]) panic("heap selftest: kmalloc returned nothing");
        if ((uintptr_t)p[i] & (ALIGNMENT - 1)) {
            panic("heap selftest: misaligned allocation");
        }

        uint8_t* bytes = (uint8_t*)p[i];
        for (size_t j = 0; j < n; ++j) bytes[j] = (uint8_t)(i + 1);
    }

    /* If two allocations overlapped, one of them just wrote over the other's
     * pattern. Cheap check, catches an entire class of splitting bug. */
    for (int i = 0; i < TEST_N; ++i) {
        size_t n = test_size(i);
        uint8_t* bytes = (uint8_t*)p[i];

        for (size_t j = 0; j < n; ++j) {
            if (bytes[j] != (uint8_t)(i + 1)) {
                panic("heap selftest: allocations overlap");
            }
        }
    }

    if (live_allocations != base_allocs + TEST_N) {
        panic("heap selftest: allocation count wrong");
    }

    /* Free back to front, so coalescing has to work through prev as well. */
    for (int i = TEST_N - 1; i >= 0; --i) kfree(p[i]);

    if (live_allocations != base_allocs) panic("heap selftest: allocations leaked");
    if (live_bytes != base_bytes)        panic("heap selftest: bytes leaked");

    /* Three neighbours freed should become one block, not three. Checking the
     * header directly rather than inferring it from a later allocation
     * succeeding, which would also pass if it just found room elsewhere. */
    void* a = kmalloc(512);
    void* b = kmalloc(512);
    void* c = kmalloc(512);
    if (!a || !b || !c) panic("heap selftest: kmalloc failed setting up coalescing");

    struct block* head = (struct block*)a - 1;
    kfree(a);
    kfree(b);
    kfree(c);

    if (head->size < 3 * 512) {
        kprintf("heap: merged block is %u bytes, expected at least %u\n",
                head->size, 3 * 512);
        panic("heap selftest: adjacent free blocks didn't coalesce");
    }

    if (live_allocations != base_allocs) panic("heap selftest: leak after coalescing");

    kprintf("heap: selftest ok, %u allocations, coalescing merges to %u bytes\n",
            TEST_N, head->size);
}

uint32_t heap_live_allocations(void) { return live_allocations; }
uint32_t heap_live_bytes(void)       { return live_bytes; }
uint32_t heap_size(void)             { return heap_end - HEAP_BASE; }
