/* Kernel heap. Free list with block headers, first fit, coalescing.
 *
 * Lives in its own virtual region rather than on identity mapped frames, so
 * growing it is a vmm_map rather than a hope that the next physical frame
 * happens to be adjacent. */

#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

#define HEAP_BASE     0xD0000000
#define HEAP_INITIAL  (64 * 1024)
#define HEAP_MAX      (4 * 1024 * 1024)

void  heap_initialize(void);

void* kmalloc(size_t bytes);
void  kfree(void* ptr);

/* Live accounting, so a test can assert a subsystem doesn't leak. */
uint32_t heap_live_allocations(void);
uint32_t heap_live_bytes(void);
uint32_t heap_size(void);

void heap_selftest(void);

#endif
