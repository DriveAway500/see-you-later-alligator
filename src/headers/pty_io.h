#ifndef PTY_IO_H
#define PTY_IO_H

#include <stddef.h>
#include <sys/types.h>

/* Fork a shell on a new pty. cmdline is split on spaces.
   *master is made non-blocking. Returns the child pid, or -1. */
pid_t pty_spawn(int *master, const char *cmdline, const char *term, int rows, int cols);

/* Write everything (retries on EAGAIN/EINTR, gives up after ~5 s). */
void pty_write_all(int fd, const char *buf, size_t n);

void pty_set_size(int fd, int rows, int cols);

#endif