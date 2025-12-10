#include <stdint.h>
#include <stddef.h>

#include "syscall.h"
#include "process.h"
#include "thread.h"
#include "console.h"
#include "vma.h"
#include "pit.h"
#include "serial.h"

#define WRITE_MAX  4096
#define NAME_MAX   32

/* Every pointer from userspace gets vetted against the caller's own address
 * space before the kernel touches it. Reading through it afterwards is safe
 * even if the page was never faulted in - we're running as the process, so
 * the demand-pager resolves it like any other access. What this stops is a
 * user program handing the kernel a kernel address and having it echoed back
 * through write(). */
static int sys_write(uint32_t fd, uint32_t buf, uint32_t len) {
    (void)fd;                       /* one console, every fd is it */

    if (len == 0) return 0;
    if (len > WRITE_MAX) return -1;

    if (!vma_user_range_ok(vma_active(), buf, len)) return -1;

    const char* s = (const char*)buf;
    for (uint32_t i = 0; i < len; ++i) console_putchar(s[i]);

    return (int)len;
}

/* Strings need the byte-at-a-time treatment: length isn't known up front,
 * so each byte's page has to be vetted before it's read. */
static int copy_user_string(char* dst, uint32_t src, uint32_t max) {
    for (uint32_t i = 0; i < max - 1; ++i) {
        if (!vma_user_range_ok(vma_active(), src + i, 1)) return -1;

        dst[i] = *(const char*)(src + i);
        if (dst[i] == '\0') return 0;
    }

    dst[max - 1] = '\0';
    return 0;
}

void syscall_dispatch(struct registers* r) {
    switch (r->eax) {

    case SYS_EXIT:
        process_exit((int)r->ebx);
        /* not reached */

    case SYS_FORK:
        r->eax = (uint32_t)process_fork(r);
        return;

    case SYS_WRITE:
        r->eax = (uint32_t)sys_write(r->ebx, r->ecx, r->edx);
        return;

    case SYS_GETPID:
        r->eax = process_current()->pid;
        return;

    case SYS_SLEEP:
        thread_sleep_ms(r->ebx);
        r->eax = 0;
        return;

    case SYS_YIELD:
        schedule();
        r->eax = 0;
        return;

    case SYS_WAIT:
        r->eax = (uint32_t)process_wait(0);
        return;

    case SYS_EXEC: {
        char name[NAME_MAX];
        if (copy_user_string(name, r->ebx, NAME_MAX) != 0) {
            r->eax = (uint32_t)-1;
            return;
        }
        r->eax = (uint32_t)process_exec(r, name);
        return;
    }

    default:
        kprintf("syscall: pid %u asked for unknown syscall %u\n",
                process_current()->pid, r->eax);
        r->eax = (uint32_t)-1;
        return;
    }
}
