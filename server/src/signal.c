#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

//
// Defs and constants.
#define DATEFMT "timestamp:%Y_%m_%d_%H:%M:%S\n"
#define DATESIZE 30
extern const char* TMPFILE;
//
// Global variables.
extern bool sig_exit;

//
// Handler to update flag when signal is received.
//
void _exit_handler(int) {
    syslog(LOG_INFO, "Caught signal. Exiting...");
    sig_exit = true;
}

//
// Function that sets _exit_handler as handler for the specified signal.
// Returns 0 on success, -1 on failure.
//
int sig_setexit(int signo) {
    struct sigaction _action;
    _action.sa_handler = _exit_handler;
    sigfillset(&_action.sa_mask); // Block all signals during handler execution.
    _action.sa_flags = 0;

    if (sigaction(signo, &_action, NULL) < 0) {
        syslog(LOG_ERR, "sigaction: %s", strerror(errno));
        return -1;
    }

    return 0;
}

//
// Handler function for timer thread. 
// and prints timestamp when one of such signals is received.
//
void* timer_handler(void* arg) {
    pthread_mutex_t *io_mutex = (pthread_mutex_t*)arg;
    bool abort = false;
    int error;

    // Create signal mask to block signals and handle via sigwait.
    sigset_t block_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGTERM);
    sigaddset(&block_mask, SIGALRM);

    error = pthread_sigmask(SIG_BLOCK, &block_mask, NULL);
    if (error < 0) {
        syslog(LOG_ERR, "pthread_sigmask: %s", strerror(error));
        int retval = -1;
        pthread_exit(&retval);
    }

    // Open file for printing timestamps.
    int fd = open(TMPFILE, O_WRONLY|O_APPEND|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (fd < 0) {
        syslog(LOG_ERR, "open: %s", strerror(errno));
        abort = true;
        goto file_cleanup;
    }

    // Create timer and arm it (10s interval, first expires after 10s).
    timer_t timer; 
    if (timer_create(CLOCK_REALTIME, NULL, &timer) < 0) { 
        syslog(LOG_ERR, "timer_create: %s", strerror(errno));
        abort = true;
        goto cleanup;
    }

    struct itimerspec ts;
    ts.it_interval.tv_sec = 10;
    ts.it_interval.tv_nsec = 0;
    ts.it_value.tv_sec = 10;
    ts.it_value.tv_nsec = 0;
    if (timer_settime(timer, 0, &ts, NULL) < 0) {
        syslog(LOG_ERR, "timer_settime: %s", strerror(errno));
        abort = true;
    }

    // Wait for timer to expire and print timestamp on file.
    int signo;
    time_t now;
    struct tm now_tm;
    char now_str[DATESIZE+1];
    now_str[DATESIZE] = '\0';

    while (!sig_exit) {
        if (sigwait(&block_mask, &signo) < 0) {
            syslog(LOG_ERR, "sigwait: %s", strerror(errno));
            abort = true;
            goto cleanup;
        }

        if (signo == SIGTERM) {
            goto cleanup;
        }

        now = time(NULL);
        if (now == (time_t) -1) {
            syslog(LOG_ERR, "time: %s", strerror(errno));     
            abort = true;
            goto cleanup;
        }

        if (!localtime_r(&now, &now_tm)) {
            syslog(LOG_ERR, "localtime_r: %s", strerror(errno));
            abort = true;
            goto cleanup;
        }

        if (strftime(now_str, DATESIZE+1, DATEFMT, &now_tm) != DATESIZE) {
            syslog(LOG_ERR, "strftime: %s", strerror(errno));
            abort = true;
            goto cleanup;
        }
        
        size_t bytes_remaining = DATESIZE;
        ssize_t bytes_written = 0;
        char* str_tail = now_str;
        error = pthread_mutex_lock(io_mutex);
        if (error == 0) {
            while (bytes_remaining > 0) {
                bytes_written = write(fd, str_tail, bytes_remaining);
                if (bytes_written < 0) {
                    syslog(LOG_ERR, "write: %s", strerror(errno));
                    break;
                }
                bytes_remaining -= bytes_written;
                str_tail += bytes_written;
            }

            error = pthread_mutex_unlock(io_mutex);
            if (error != 0) {
                syslog(LOG_ERR, "pthread_mutex_unlock: %s", strerror(error));
            }
        } else {
            syslog(LOG_ERR, "pthread_mutex_lock: %s", strerror(error));
        }

        if (error != 0 || bytes_written < 0) {
            abort = true;
            goto cleanup;
        }
    }

cleanup:
    if (timer_delete(timer) < 0) {
        syslog(LOG_ERR, "timer_delete: %s", strerror(errno));
        abort = true;
    }

file_cleanup:
    if (close(fd) < 0) {
        syslog(LOG_ERR, "close: %s", strerror(errno));
        abort = true;
    }

    int retval = abort ? -1 : 0;
    pthread_exit(&retval);
} 

