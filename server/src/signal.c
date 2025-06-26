#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

//
// Global variable used as flag for keeping track of signal reception.
//
extern bool sig_received;

//
// Handler to update flag when signal is received.
//
void _sig_handler(int) {
    syslog(LOG_INFO, "Caught signal. Exiting...");
    sig_received = true;
}

//
// Function that sets _exit_handler as handler for the specified signal.
// On success, zero is retured. On error, -1 is returned.
//
int sig_setexit(int signo) {
    struct sigaction graceful_exit;
    graceful_exit.sa_handler = _sig_handler;
    sigfillset(&graceful_exit.sa_mask); // Block all signals during handler execution.
    graceful_exit.sa_flags = 0;

    if (sigaction(signo, &graceful_exit, NULL) < 0) {
        syslog(LOG_ERR, "sigaction: %s", strerror(errno));
        return -1;
    }

    return 0;
}
