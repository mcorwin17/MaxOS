#include "ulib.h"

u32 strlen(const char* s) {
    u32 n = 0;
    while (s[n]) ++n;
    return n;
}

void print(const char* s) {
    write(1, s, strlen(s));
}

void printn(u32 n) {
    char buf[12];
    int i = 0;

    if (n == 0) { write(1, "0", 1); return; }

    while (n > 0) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (i-- > 0) write(1, &buf[i], 1);
}
