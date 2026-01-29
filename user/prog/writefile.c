/* Writes a file from ring 3 - header line plus a 5000-byte pattern that
 * forces the cluster chain to grow - closes it, reopens it, and verifies
 * every byte came back. The host then checks the same bytes in the raw
 * image, and a second boot checks they survived. */

#include "ulib.h"

#define PATTERN_LEN 5000

static char pattern_byte(u32 i) {
    return (char)(((i * 7 + 3) % 253) + 1);
}

int main(void) {
    i32 fd = syscall3(12, (u32)"/OUT.TXT", 1, 0);   /* open create */
    if (fd < 0) { print("writefile: create failed\n"); return 1; }

    const char* header = "written from ring 3 through fat16\n";
    write(fd, header, strlen(header));

    char chunk[250];
    for (u32 off = 0; off < PATTERN_LEN; off += sizeof(chunk)) {
        for (u32 i = 0; i < sizeof(chunk); ++i) {
            chunk[i] = pattern_byte(off + i);
        }
        if (write(fd, chunk, sizeof(chunk)) != (i32)sizeof(chunk)) {
            print("writefile: write failed\n");
            return 1;
        }
    }
    close(fd);

    /* Read it all back through a fresh descriptor. */
    fd = open("/OUT.TXT");
    if (fd < 0) { print("writefile: reopen failed\n"); return 1; }

    char buf[250];
    u32 hlen = strlen(header);
    if (read(fd, buf, hlen) != (i32)hlen) {
        print("writefile: header reread failed\n");
        return 1;
    }
    for (u32 i = 0; i < hlen; ++i) {
        if (buf[i] != header[i]) {
            print("writefile: header mismatch\n");
            return 1;
        }
    }

    u32 checked = 0;
    while (checked < PATTERN_LEN) {
        i32 got = read(fd, buf, sizeof(buf));
        if (got <= 0) { print("writefile: pattern reread failed\n"); return 1; }

        for (i32 i = 0; i < got; ++i) {
            if (buf[i] != pattern_byte(checked + (u32)i)) {
                print("writefile: pattern mismatch\n");
                return 1;
            }
        }
        checked += (u32)got;
    }
    close(fd);

    print("writefile: wrote and verified ");
    printn(hlen + PATTERN_LEN);
    print(" bytes\n");
    return 0;
}
