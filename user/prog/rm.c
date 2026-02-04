/* rm: unlink one file. */

#include "ulib.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        print("rm: what?\n");
        return 1;
    }

    if (unlink(argv[1]) != 0) {
        print("rm: can't remove ");
        print(argv[1]);
        print("\n");
        return 1;
    }
    return 0;
}
