/* cat /HELLO.TXT | wc
 *
 * Two processes loaded from the filesystem, wired together through a pipe:
 * pipe, fork twice, dup2 the ends over stdin/stdout, exec the binaries off
 * the disk, close the parent's copies, wait for both.
 *
 * The parent closing its pipe fds is the classic footgun: keep them open
 * and wc never sees EOF, because the parent still counts as a writer. */

#include "ulib.h"

int main(void) {
    i32 fds[2];

    if (pipe(fds) != 0) {
        print("pipeline: pipe failed\n");
        return 1;
    }

    i32 pid1 = fork();
    if (pid1 == 0) {
        dup2(fds[1], 1);            /* stdout into the pipe */
        close(fds[0]);
        close(fds[1]);
        exec("/BIN/CAT.BIN", "/HELLO.TXT");
        exit(127);                  /* exec only returns on failure */
    }

    i32 pid2 = fork();
    if (pid2 == 0) {
        dup2(fds[0], 0);            /* stdin from the pipe */
        close(fds[0]);
        close(fds[1]);
        exec("/BIN/WC.BIN", 0);
        exit(127);
    }

    close(fds[0]);
    close(fds[1]);

    wait();
    wait();

    print("pipeline: done\n");
    return 0;
}
