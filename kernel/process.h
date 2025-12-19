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

struct thread;

#define USER_CODE_BASE   0x08048000
#define USER_STACK_BASE  0xBFFF0000
#define USER_STACK_TOP   0xBFFFFFF0
#define USER_STACK_SIZE  0x00010000

enum process_state { PROC_LIVE, PROC_ZOMBIE };

struct process {
    uint32_t           pid;
    enum process_state state;
    int                exit_code;
    struct process*    parent;
    struct addrspace   as;
    struct thread*     thread;
    struct process*    next;
};

void process_initialize(void);

struct process* process_current(void);

/* Start an embedded program as a child of the current process. Returns the
 * pid, or -1 if the name isn't in the table. */
int process_spawn(const char* name);

/* The syscall backends. fork/exec need the saved user frame because fork
 * duplicates it and exec rewrites it. */
int  process_fork(struct registers* r);
int  process_exec(struct registers* r, const char* name);
int  process_wait(uint32_t* pid_out);
void process_exit(int code) __attribute__((noreturn));

/* True while the pid exists and hasn't exited. */
int  process_is_live(uint32_t pid);

/* A user thread faulted: report, kill the process, schedule on. The kernel
 * does not go down because a user program dereferenced garbage. */
void process_kill_current(uint32_t vector) __attribute__((noreturn));

void process_dump(void);

#endif
