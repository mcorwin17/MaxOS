/* The libc, such as it is. Written rather than ported on purpose: a few
 * hundred lines I understand completely beat a hundred thousand I
 * configured. */

#ifndef ULIB_H
#define ULIB_H

typedef unsigned int   u32;
typedef int            i32;

/* eax = number, ebx/ecx/edx = arguments, result back in eax. */
static inline i32 syscall3(i32 n, u32 a, u32 b, u32 c) {
    i32 ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "b"(a), "c"(b), "d"(c)
                     : "memory");
    return ret;
}

static inline void exit(i32 code) {
    syscall3(1, (u32)code, 0, 0);
    for (;;) { }
}

static inline i32  fork(void)                        { return syscall3(2, 0, 0, 0); }
static inline i32  write(i32 fd, const void* b, u32 n) { return syscall3(3, (u32)fd, (u32)b, n); }
static inline i32  getpid(void)                      { return syscall3(4, 0, 0, 0); }
static inline void sleep_ms(u32 ms)                  { syscall3(5, ms, 0, 0); }
static inline i32  wait(void)                        { return syscall3(7, 0, 0, 0); }
static inline i32  exec(const char* path, const char* args) { return syscall3(8, (u32)path, (u32)args, 0); }
static inline i32  open(const char* path)            { return syscall3(12, (u32)path, 0, 0); }
static inline i32  read(i32 fd, void* b, u32 n)      { return syscall3(13, (u32)fd, (u32)b, n); }
static inline i32  close(i32 fd)                     { return syscall3(14, (u32)fd, 0, 0); }
static inline i32  pipe(i32 fds[2])                  { return syscall3(15, (u32)fds, 0, 0); }
static inline i32  dup2(i32 oldfd, i32 newfd)        { return syscall3(16, (u32)oldfd, (u32)newfd, 0); }

struct udirent {
    char name[16];
    u32  size;
    u32  is_dir;
};

static inline i32 readdir(const char* path, u32 index, struct udirent* out) {
    return syscall3(17, (u32)path, index, (u32)out);
}
static inline i32 unlink(const char* path)           { return syscall3(18, (u32)path, 0, 0); }
static inline i32 open_create(const char* path)      { return syscall3(12, (u32)path, 1, 0); }
static inline i32 tcsetfg(i32 pid)                   { return syscall3(19, (u32)pid, 0, 0); }

#define SIG_IGN 1u
static inline i32 signal(i32 sig, u32 handler, u32 restorer) {
    return syscall3(9, (u32)sig, handler, restorer);
}

u32  strlen(const char* s);
void print(const char* s);
void printn(u32 n);

#endif
