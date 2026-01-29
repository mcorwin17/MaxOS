#include <stdint.h>
#include <stddef.h>

#include "syscall.h"
#include "process.h"
#include "thread.h"
#include "console.h"
#include "vma.h"
#include "pit.h"
#include "serial.h"
#include "signal.h"
#include "vfs.h"
#include "pipe.h"
#include "bcache.h"

#define WRITE_MAX  4096
#define NAME_MAX   32

/* Every pointer from userspace gets vetted against the caller's own address
 * space before the kernel touches it. Reading through it afterwards is safe
 * even if the page was never faulted in - we're running as the process, so
 * the demand-pager resolves it like any other access. What this stops is a
 * user program handing the kernel a kernel address and having it echoed back
 * through write(). */
static int sys_write(uint32_t fd, uint32_t buf, uint32_t len) {
    if (len == 0) return 0;
    if (len > WRITE_MAX) return -1;

    if (!vma_user_range_ok(vma_active(), buf, len)) return -1;

    struct process* p = process_current();
    if (fd >= FD_MAX) return -1;

    switch (p->fds[fd].type) {
    case FDT_CONSOLE: {
        const char* s = (const char*)buf;
        for (uint32_t i = 0; i < len; ++i) console_putchar(s[i]);
        return (int)len;
    }
    case FDT_PIPE_W:
        return pipe_write(p->fds[fd].pipe, (const void*)buf, len);
    case FDT_FILE: {
        int got = vfs_write(p->fds[fd].path, p->fds[fd].off,
                            (const void*)buf, len);
        if (got > 0) p->fds[fd].off += (uint32_t)got;
        return got;
    }
    default:
        return -1;
    }
}

/* readdir plumbing: vfs_list pushes every entry, this keeps the Nth. */
struct readdir_ctx {
    uint32_t want;
    uint32_t seen;
    struct udirent* out;
    int filled;
};

static void readdir_emit(const char* name, uint32_t size, int is_dir,
                         void* opaque) {
    struct readdir_ctx* c = (struct readdir_ctx*)opaque;

    if (c->seen++ != c->want) return;

    uint32_t i = 0;
    for (; name[i] && i < sizeof(c->out->name) - 1; ++i) {
        c->out->name[i] = name[i];
    }
    c->out->name[i]  = '\0';
    c->out->size     = size;
    c->out->is_dir   = (uint32_t)is_dir;
    c->filled        = 1;
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
        char path[FD_PATH_MAX];
        char args[CMDLINE_MAX];
        args[0] = '\0';

        if (copy_user_string(path, r->ebx, FD_PATH_MAX) != 0) {
            r->eax = (uint32_t)-1;
            return;
        }
        if (r->ecx && copy_user_string(args, r->ecx, CMDLINE_MAX) != 0) {
            r->eax = (uint32_t)-1;
            return;
        }

        r->eax = (uint32_t)process_exec(r, path, args);
        return;
    }

    case SYS_SIGNAL:
        r->eax = (uint32_t)signal_install(process_current(), (int)r->ebx,
                                          r->ecx, r->edx);
        return;

    case SYS_KILL: {
        struct process* target = process_find(r->ebx);
        if (!target) { r->eax = (uint32_t)-1; return; }
        signal_send(target, (int)r->ecx);
        r->eax = 0;
        return;
    }

    case SYS_SIGRETURN:
        /* Restores the whole parked frame, eax included - no return value
         * to write, the restored world IS the result. */
        (void)signal_return(r);
        return;

    case SYS_OPEN: {
        char path[FD_PATH_MAX];
        if (copy_user_string(path, r->ebx, FD_PATH_MAX) != 0) {
            r->eax = (uint32_t)-1;
            return;
        }

        /* ecx = 1: create-or-truncate for writing. */
        if (r->ecx == 1 && vfs_create(path) != 0) {
            r->eax = (uint32_t)-1;
            return;
        }

        struct vfs_stat st;
        if (vfs_stat(path, &st) != 0 || st.is_dir) {
            r->eax = (uint32_t)-1;
            return;
        }

        struct process* p = process_current();
        for (int fd = FD_FIRST; fd < FD_MAX; ++fd) {
            if (p->fds[fd].type != FDT_NONE) continue;

            p->fds[fd].type = FDT_FILE;
            p->fds[fd].off  = 0;
            for (int i = 0; i < FD_PATH_MAX; ++i) {
                p->fds[fd].path[i] = path[i];
                if (!path[i]) break;
            }

            r->eax = (uint32_t)fd;
            return;
        }

        r->eax = (uint32_t)-1;      /* table full */
        return;
    }

    case SYS_READ: {
        uint32_t fd = r->ebx, buf = r->ecx, n = r->edx;

        if (n > WRITE_MAX) n = WRITE_MAX;
        if (fd >= FD_MAX || !vma_user_range_ok(vma_active(), buf, n)) {
            r->eax = (uint32_t)-1;
            return;
        }

        struct process* p = process_current();

        switch (p->fds[fd].type) {
        case FDT_CONSOLE: {
            /* Block for the first byte - but a pending signal ends the wait
             * with -1 instead. Without this, a process stuck reading the
             * console can't be Ctrl-C'd until a keystroke arrives, and then
             * it eats that keystroke as its dying breath. Found the hard
             * way: a killed wc swallowed the r of the next command. */
            while (!console_has_input()) {
                if (p->sig_pending & ~(1u << 17)) {     /* SIGCHLD waits */
                    r->eax = (uint32_t)-1;
                    return;
                }
                __asm__ volatile("hlt");
            }

            char* out = (char*)buf;
            uint32_t got = 0;
            while (got < n && console_has_input()) {
                out[got++] = console_getchar();
            }
            r->eax = got;
            return;
        }
        case FDT_PIPE_R:
            r->eax = (uint32_t)pipe_read(p->fds[fd].pipe, (void*)buf, n);
            return;
        case FDT_FILE: {
            int got = vfs_read(p->fds[fd].path, p->fds[fd].off,
                               (void*)buf, n);
            if (got > 0) p->fds[fd].off += (uint32_t)got;
            r->eax = (uint32_t)got;
            return;
        }
        default:
            r->eax = (uint32_t)-1;
            return;
        }
    }

    case SYS_CLOSE: {
        uint32_t fd = r->ebx;
        struct process* p = process_current();

        if (fd >= FD_MAX || p->fds[fd].type == FDT_NONE) {
            r->eax = (uint32_t)-1;
            return;
        }

        /* Closing a written file is the moment it should be durable. */
        if (p->fds[fd].type == FDT_FILE) bflush();

        process_close_fd(p, (int)fd);
        r->eax = 0;
        return;
    }

    case SYS_READDIR: {
        char path[FD_PATH_MAX];
        if (copy_user_string(path, r->ebx, FD_PATH_MAX) != 0 ||
            !vma_user_range_ok(vma_active(), r->edx, sizeof(struct udirent))) {
            r->eax = (uint32_t)-1;
            return;
        }

        struct readdir_ctx ctx = { r->ecx, 0, (struct udirent*)r->edx, 0 };
        if (vfs_list(path, readdir_emit, &ctx) != 0 || !ctx.filled) {
            r->eax = (uint32_t)-1;
            return;
        }

        r->eax = 0;
        return;
    }

    case SYS_UNLINK: {
        char path[FD_PATH_MAX];
        if (copy_user_string(path, r->ebx, FD_PATH_MAX) != 0) {
            r->eax = (uint32_t)-1;
            return;
        }

        int rc = vfs_unlink(path);
        if (rc == 0) bflush();
        r->eax = (uint32_t)rc;
        return;
    }

    case SYS_PIPE: {
        uint32_t out = r->ebx;
        if (!vma_user_range_ok(vma_active(), out, 8)) {
            r->eax = (uint32_t)-1;
            return;
        }

        struct process* p = process_current();

        int rfd = -1, wfd = -1;
        for (int fd = FD_FIRST; fd < FD_MAX; ++fd) {
            if (p->fds[fd].type != FDT_NONE) continue;
            if (rfd < 0) { rfd = fd; continue; }
            wfd = fd;
            break;
        }
        if (wfd < 0) { r->eax = (uint32_t)-1; return; }

        struct pipe* pp = pipe_create();
        if (!pp) { r->eax = (uint32_t)-1; return; }

        p->fds[rfd].type = FDT_PIPE_R;
        p->fds[rfd].pipe = pp;
        p->fds[wfd].type = FDT_PIPE_W;
        p->fds[wfd].pipe = pp;

        ((int*)out)[0] = rfd;
        ((int*)out)[1] = wfd;

        r->eax = 0;
        return;
    }

    case SYS_DUP2: {
        uint32_t oldfd = r->ebx, newfd = r->ecx;
        struct process* p = process_current();

        if (oldfd >= FD_MAX || newfd >= FD_MAX ||
            p->fds[oldfd].type == FDT_NONE) {
            r->eax = (uint32_t)-1;
            return;
        }
        if (oldfd == newfd) { r->eax = newfd; return; }

        process_close_fd(p, (int)newfd);
        p->fds[newfd] = p->fds[oldfd];

        /* The copy is another claim on a pipe end. */
        if (p->fds[newfd].type == FDT_PIPE_R) pipe_ref_reader(p->fds[newfd].pipe);
        if (p->fds[newfd].type == FDT_PIPE_W) pipe_ref_writer(p->fds[newfd].pipe);

        r->eax = newfd;
        return;
    }

    default:
        kprintf("syscall: pid %u asked for unknown syscall %u\n",
                process_current()->pid, r->eax);
        r->eax = (uint32_t)-1;
        return;
    }
}
