#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

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

