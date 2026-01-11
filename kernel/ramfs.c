#include <stdint.h>
#include <stddef.h>

#include "ramfs.h"
#include "vfs.h"

struct ramfile {
    const char* name;
    const char* content;
};

static const struct ramfile files[] = {
    { "hello.txt", "ramfs says hi\n" },
    { "why.txt",   "two filesystems behind one vtable keep the vtable honest\n" },
};

#define NFILES (sizeof(files) / sizeof(files[0]))

static uint32_t str_len(const char* s) {
    uint32_t n = 0;
    while (s[n]) ++n;
    return n;
}

static int str_equal(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static const struct ramfile* find(const char* path) {
    if (path[0] != '/') return 0;

    for (uint32_t i = 0; i < NFILES; ++i) {
        if (str_equal(path + 1, files[i].name)) return &files[i];
    }
    return 0;
}

static int ramfs_stat(const char* path, struct vfs_stat* out) {
    if (str_equal(path, "/")) {
        out->size   = 0;
        out->is_dir = 1;
        return 0;
    }

    const struct ramfile* f = find(path);
    if (!f) return -1;

    out->size   = str_len(f->content);
    out->is_dir = 0;
    return 0;
}

static int ramfs_read(const char* path, uint32_t off, void* buf, uint32_t n) {
    const struct ramfile* f = find(path);
    if (!f) return -1;

    uint32_t size = str_len(f->content);
    if (off >= size) return 0;
    if (off + n > size) n = size - off;

    uint8_t* out = (uint8_t*)buf;
    for (uint32_t i = 0; i < n; ++i) out[i] = (uint8_t)f->content[off + i];

    return (int)n;
}

static int ramfs_list(const char* path, vfs_emit_fn emit, void* ctx) {
    if (!str_equal(path, "/")) return -1;

    for (uint32_t i = 0; i < NFILES; ++i) {
        emit(files[i].name, str_len(files[i].content), 0, ctx);
    }
    return 0;
}

static const struct fs_ops ramfs_ops = {
    .fsname = "ramfs",
    .stat   = ramfs_stat,
    .read   = ramfs_read,
    .list   = ramfs_list,
};

void ramfs_mount(void) {
    vfs_mount("/ram", &ramfs_ops);
}
