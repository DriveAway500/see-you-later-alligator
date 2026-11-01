#ifndef PTY_WATCH_H
#define PTY_WATCH_H

#include <pthread.h>
#include <stdbool.h>

/*
 * Lets the main thread sleep inside the windowing system's event wait
 * (0% CPU) and still wake up the instant the shell writes something.
 *
 * A helper thread blocks in poll() on the pty. When data is readable it calls
 * `wake` (glfwPostEmptyEvent, which is thread-safe) once and then sleeps
 * again until the main thread calls pty_watch_ack() after draining the pty.
 * No data -> no wake-ups, no polling, no timers.
 */
typedef struct {
    pthread_t       th;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    int             fd;
    int             stop_pipe[2];
    bool            pending;
    bool            quit;
    bool            running;
    void          (*wake)(void);
} PtyWatch;

int  pty_watch_start(PtyWatch *w, int fd, void (*wake)(void));

/* Call after reading the pty (it re-arms the watcher). Cheap. */
void pty_watch_ack(PtyWatch *w);

/* Stop and join the thread. Call before closing the fd. */
void pty_watch_stop(PtyWatch *w);

#endif
