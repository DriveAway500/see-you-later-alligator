#define _GNU_SOURCE
#include "headers/pty_io.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

pid_t pty_spawn(int *master, const char *cmdline, const char *term, int rows, int cols) {
    struct winsize ws = { .ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols };
    pid_t pid = forkpty(master, NULL, NULL, &ws);
    if (pid < 0) return -1;

    if (pid == 0) {
        setenv("TERM", term, 1);
        setenv("COLORTERM", "truecolor", 1);

        char buf[256];
        snprintf(buf, sizeof buf, "%s", cmdline);
        char *argv[32];
        int n = 0;
        for (char *t = strtok(buf, " \t"); t && n < 31; t = strtok(NULL, " \t")) argv[n++] = t;
        argv[n] = NULL;
        if (n == 0) { argv[0] = "/bin/sh"; argv[1] = NULL; }

        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }

    fcntl(*master, F_SETFL, fcntl(*master, F_GETFL) | O_NONBLOCK);
    return pid;
}

void pty_write_all(int fd, const char *b, size_t n) {
    int tries = 0;
    while (n) {
        ssize_t w = write(fd, b, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN && ++tries < 100) {
                struct pollfd p = { fd, POLLOUT, 0 };
                poll(&p, 1, 50);
                continue;
            }
            return;
        }
        b += w;
        n -= (size_t)w;
    }
}

void pty_set_size(int fd, int rows, int cols) {
    struct winsize ws = { .ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols };
    ioctl(fd, TIOCSWINSZ, &ws);
}