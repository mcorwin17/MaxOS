/* A filesystem made of string constants. Exists to keep the VFS honest:
 * with two backends behind the same vtable, the abstraction is tested by
 * having to serve both. */

#ifndef RAMFS_H
#define RAMFS_H

void ramfs_mount(void);

#endif
