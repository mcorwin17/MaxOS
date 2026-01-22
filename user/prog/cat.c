/* cat: a file to stdout, or stdin to stdout with no argument - which is
 * what makes it pipeline-able on either end. */

#include "ulib.h"

static void pump(i32 fd) {
    char buf[256];
    i32 n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, (u32)n);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        pump(0);
        return 0;
    }

    i32 fd = open(argv[1]);
    if (fd < 0) {
        print("cat: can't open ");
        print(argv[1]);
        print("\n");
        return 1;
    }

    pump(fd);
    close(fd);
    return 0;
}
