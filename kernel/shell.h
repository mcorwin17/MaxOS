/* In-kernel shell. No processes yet, just a read-eval-print loop over the
 * console buffer. */

#ifndef SHELL_H
#define SHELL_H

void shell_run(void) __attribute__((noreturn));

#endif
