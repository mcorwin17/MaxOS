/* Pipes: a ring buffer with a reader count and a writer count.
 *
 * The counts are the semantics. Read on an empty pipe blocks until data or
 * until every writer is gone - then it's EOF. Write with no readers left is
 * SIGPIPE and -1. fork duplicates descriptors, so the ends are refcounted;
 * the buffer dies with its last descriptor. */

#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

#define PIPE_BUF 512

struct pipe {
    uint8_t  data[PIPE_BUF];
    uint32_t rpos, count;
    uint32_t readers, writers;
};

struct pipe* pipe_create(void);

void pipe_ref_reader(struct pipe* p);
void pipe_ref_writer(struct pipe* p);
void pipe_close_reader(struct pipe* p);
void pipe_close_writer(struct pipe* p);

/* Blocking. Read returns 0 at EOF (empty and no writers); write returns -1
 * once no readers remain, after raising SIGPIPE on the caller. */
int pipe_read(struct pipe* p, void* buf, uint32_t n);
int pipe_write(struct pipe* p, const void* buf, uint32_t n);

#endif
