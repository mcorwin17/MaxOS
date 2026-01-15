/* Processes: an address space, a thread, a parent, and an exit code.
 *
 * Process 0 is the kernel itself - the boot thread and every kernel thread
 * belong to it. User programs come from a table of binaries embedded in the
 * kernel image, because there is no filesystem yet to load them from. */

#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "vma.h"
#include "panic.h"
#include "signal.h"

struct thread;

#define USER_CODE_BASE   0x08048000
#define USER_STACK_BASE  0xBFFF0000
#define USER_STACK_TOP   0xBFFFFFF0
#define USER_STACK_SIZE  0x00010000

enum process_state { PROC_LIVE, PROC_ZOMBIE };

/* Open files. 0/1/2 start out as the console; dup2 can point them at
 * anything, which is how a pipeline redirects stdin and stdout. */
#define FD_MAX      8
#define FD_PATH_MAX 64
#define FD_FIRST    3

enum fd_type {
    FDT_NONE = 0,
    FDT_CONSOLE,
    FDT_FILE,
    FDT_PIPE_R,
    FDT_PIPE_W
};

struct pipe;

struct fdesc {
    uint8_t      type;
    uint32_t     off;               /* FDT_FILE */
    struct pipe* pipe;              /* FDT_PIPE_* */
    char         path[FD_PATH_MAX]; /* FDT_FILE */
};

#define CMDLINE_MAX 128

struct process {
    uint32_t           pid;
    enum process_state state;
    int                exit_code;
    struct process*    parent;
    struct addrspace   as;
    struct thread*     thread;
    struct process*    next;

    /* Signals. Handlers survive fork, reset on exec; pending doesn't
     * survive either. */
    uint32_t         sig_pending;
    uint32_t         sig_handler[NSIG];
    uint32_t         sig_restorer;
    int              sig_in_handler;
    struct registers sig_saved;

    /* Open files survive both fork and exec, Unix style. */
    struct fdesc fds[FD_MAX];

    /* What to load and the argv to build, for the spawn entry thread. */
    char cmdline[CMDLINE_MAX];
};

void process_initialize(void);

struct process* process_current(void);

/* Start a program as a child of the current process. The cmdline's first
 * word names it: an embedded blob by bare name, or a file on the mounted
 * filesystem by /path. The rest becomes argv. Returns pid or -1. */
int process_spawn(const char* cmdline);

/* Close one descriptor properly - pipes need their end counts dropped. */
void process_close_fd(struct process* p, int fd);

/* The syscall backends. fork/exec need the saved user frame because fork
 * duplicates it and exec rewrites it. */
int  process_fork(struct registers* r);
int  process_exec(struct registers* r, const char* path, const char* args);
int  process_wait(uint32_t* pid_out);
void process_exit(int code) __attribute__((noreturn));

/* True while the pid exists and hasn't exited. */
int  process_is_live(uint32_t pid);

/* Live process for a pid, or 0. The pointer stays valid until the process
 * is reaped, so don't hold it across a wait. */
struct process* process_find(uint32_t pid);

/* A user thread faulted: hand it SIGSEGV and return. The default action
 * kills it on the way back to ring 3; a handler catches it like any other
 * signal. Either way the kernel keeps going. */
void process_fault_current(uint32_t vector);

void process_dump(void);

#endif
