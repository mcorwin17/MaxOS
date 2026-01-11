#include <stdint.h>
#include <stddef.h>

#include "vfs.h"
#include "serial.h"

#define MAX_MOUNTS 4

struct mount {
    char                 prefix[16];
    uint32_t             prefix_len;
    const struct fs_ops* ops;
};

static struct mount mounts[MAX_MOUNTS];
static uint32_t     mount_count;

static uint32_t str_len(const char* s) {
    uint32_t n = 0;
    while (s[n]) ++n;
    return n;
}

int vfs_mount(const char* prefix, const struct fs_ops* ops) {
    if (mount_count >= MAX_MOUNTS) return -1;

    struct mount* m = &mounts[mount_count];

    uint32_t n = str_len(prefix);
    if (n >= sizeof(m->prefix)) return -1;

    for (uint32_t i = 0; i <= n; ++i) m->prefix[i] = prefix[i];
    m->prefix_len = n;
    m->ops        = ops;
    mount_count++;

    kprintf("vfs: %s mounted at %s\n", ops->fsname, prefix);
    return 0;
}

/* Longest matching prefix wins, so /ram/x goes to ramfs even with a
 * filesystem on /. The backend gets the remainder, always starting with
 * a slash. */
static const struct mount* resolve(const char* path, const char** rest) {
    const struct mount* best = 0;

    for (uint32_t i = 0; i < mount_count; ++i) {
        const struct mount* m = &mounts[i];
        uint32_t n = m->prefix_len;

        int match = 1;
        for (uint32_t j = 0; j < n; ++j) {
            if (path[j] != m->prefix[j]) { match = 0; break; }
        }
        if (!match) continue;

        /* Prefix must end at a component boundary. */
        if (path[n] != '\0' && path[n] != '/' && n > 1) continue;

        if (!best || n > best->prefix_len) {
            best = m;
            *rest = (path[n] == '\0') ? "/" : path + (n > 1 ? n : 0);
        }
    }

    return best;
}

int vfs_stat(const char* path, struct vfs_stat* out) {
    const char* rest;
    const struct mount* m = resolve(path, &rest);
    if (!m) return -1;

    return m->ops->stat(rest, out);
}

int vfs_read(const char* path, uint32_t off, void* buf, uint32_t n) {
    const char* rest;
    const struct mount* m = resolve(path, &rest);
    if (!m) return -1;

    return m->ops->read(rest, off, buf, n);
}

int vfs_list(const char* path, vfs_emit_fn emit, void* ctx) {
    const char* rest;
    const struct mount* m = resolve(path, &rest);
    if (!m) return -1;

    return m->ops->list(rest, emit, ctx);
}
