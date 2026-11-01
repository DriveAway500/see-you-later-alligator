#include "headers/pty_watch.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

static void *watch_main(void *arg) {
    PtyWatch *w = arg;
    struct pollfd p[2] = {
        { .fd = w->fd,           .events = POLLIN },
        { .fd = w->stop_pipe[0], .events = POLLIN },
    };

    for (;;) {
        p[0].revents = p[1].revents = 0;
        if (poll(p, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (p[1].revents) break;                              /* stop requested */
        if (!p[0].revents) continue;

        /* readable, or hung up (shell exited): tell the main thread, then wait
           for it to consume before polling again, so we never spin */
        pthread_mutex_lock(&w->mu);
        w->pending = true;
        pthread_mutex_unlock(&w->mu);
        w->wake();

        pthread_mutex_lock(&w->mu);
        while (w->pending && !w->quit) pthread_cond_wait(&w->cv, &w->mu);
        bool q = w->quit;
        pthread_mutex_unlock(&w->mu);
        if (q) break;
    }
    return NULL;
}

int pty_watch_start(PtyWatch *w, int fd, void (*wake)(void)) {
    w->fd = fd;
    w->wake = wake;
    w->pending = w->quit = w->running = false;
    if (pipe(w->stop_pipe) != 0) return -1;
    pthread_mutex_init(&w->mu, NULL);
    pthread_cond_init(&w->cv, NULL);

    /* the thread must not take process signals (SIGCHLD, SIGWINCH...) */
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    int rc = pthread_create(&w->th, NULL, watch_main, w);
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    if (rc != 0) {
        close(w->stop_pipe[0]);
        close(w->stop_pipe[1]);
        return -1;
    }
    w->running = true;
    return 0;
}

void pty_watch_ack(PtyWatch *w) {
    pthread_mutex_lock(&w->mu);
    if (w->pending) {
        w->pending = false;
        pthread_cond_signal(&w->cv);
    }
    pthread_mutex_unlock(&w->mu);
}

void pty_watch_stop(PtyWatch *w) {
    if (!w->running) return;
    pthread_mutex_lock(&w->mu);
    w->quit = true;
    pthread_cond_signal(&w->cv);
    pthread_mutex_unlock(&w->mu);
    (void)!write(w->stop_pipe[1], "x", 1);
    pthread_join(w->th, NULL);
    close(w->stop_pipe[0]);
    close(w->stop_pipe[1]);
    pthread_mutex_destroy(&w->mu);
    pthread_cond_destroy(&w->cv);
    w->running = false;
}
