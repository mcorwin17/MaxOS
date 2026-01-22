/* wc: lines, words, bytes from stdin. Reads until EOF, which for a pipe
 * means until the last writer closes - that moment arriving correctly is
 * half of what this program exists to test. */

#include "ulib.h"

int main(void) {
    u32 lines = 0, words = 0, bytes = 0;
    int in_word = 0;

    char buf[256];
    i32 n;

    while ((n = read(0, buf, sizeof(buf))) > 0) {
        for (i32 i = 0; i < n; ++i) {
            char c = buf[i];
            bytes++;

            if (c == '\n') lines++;

            if (c == ' ' || c == '\t' || c == '\n') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }

    printn(lines);
    print(" lines, ");
    printn(words);
    print(" words, ");
    printn(bytes);
    print(" bytes\n");

    return 0;
}
