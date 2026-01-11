/* VFS: path-based routing to mounted filesystems.
 *
 * A mount is a prefix and a vtable; resolution is longest-prefix match, and
 * the backend sees the remainder of the path. Two real backends (ramfs and
 * FAT16) keep the abstraction honest - one backend is just that filesystem
 * with extra steps.
 *
 * Paths rather than inodes/dentries for now. The full inode + dentry-cache
 * design earns its complexity when there are hard links, rename, and cache
 * pressure; none of those exist here yet. */

#ifndef VFS_H
#define VFS_H

#include <stdint.h>

#define VFS_NAME_MAX 64

struct vfs_stat {
    uint32_t size;
    int      is_dir;
};

typedef void (*vfs_emit_fn)(const char* name, uint32_t size, int is_dir,
                            void* ctx);

struct fs_ops {
    const char* fsname;
    int (*stat)(const char* path, struct vfs_stat* out);
    int (*read)(const char* path, uint32_t off, void* buf, uint32_t n);
    int (*list)(const char* path, vfs_emit_fn emit, void* ctx);
};

int vfs_mount(const char* prefix, const struct fs_ops* ops);

int vfs_stat(const char* path, struct vfs_stat* out);

/* Returns bytes read, negative on error. Short reads at end of file. */
int vfs_read(const char* path, uint32_t off, void* buf, uint32_t n);

int vfs_list(const char* path, vfs_emit_fn emit, void* ctx);

#endif
