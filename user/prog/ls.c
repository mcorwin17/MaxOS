/* ls: the readdir syscall, one entry per call. */

#include "ulib.h"

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "/";

    struct udirent d;
    u32 index = 0;

    while (readdir(path, index++, &d) == 0) {
        print(d.name);

        u32 n = strlen(d.name);
        for (u32 pad = n; pad < 14; ++pad) print(" ");

        if (d.is_dir) {
            print("<dir>\n");
        } else {
            printn(d.size);
            print("\n");
        }
    }

    if (index == 1) {
        print("ls: can't list ");
        print(path);
        print("\n");
        return 1;
    }
    return 0;
}
