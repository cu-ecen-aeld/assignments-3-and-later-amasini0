#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

//
// Prints program usage.
//
void usage(void) {
    printf("aesdsocket: Usage: aesdsocket [-d]\n");
}

//
// Executes all the neccessary steps to run the program as daemon.
// 
int daemonize(void) {
    pid_t pid = fork();
    if (pid == -1) {
        syslog(LOG_ERR, "fork: %s", strerror(errno));
        return -1;
    }

    // Call exit in parent process.
    if (pid != 0) {
        exit(EXIT_SUCCESS);
    }

    // Set child as session leader
    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid: %s", strerror(errno));
        return -1;
    }

    // Change workdir to root dir
    if (chdir("/") < 0) {
        syslog(LOG_ERR, "chdir: %s", strerror(errno));
        return -1;
    }

    // Redirect stdin, stdout, stderr to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (dup2(fd, 0) < 0 ||
        dup2(fd, 1) < 0 ||
        dup2(fd, 2) < 0) {
        syslog(LOG_ERR, "dup2: %s", strerror(errno));
        return -1;
    }
    if (close(fd) < 0) { // This is used only for redirection.
       syslog(LOG_ERR, "close: %s", strerror(errno));
       return -1;
    }

    return 0;
}

// 
// Writes a buffer of bytes (chars) to a file handling possible parial sends.
// On success, returns 0. On failure, returns -1.
//
int putchars(int fd, char* buffer, size_t bufsize) {
    size_t bytes_left = bufsize;
    char* buf_head = buffer; // One after the last successfully sent byte.
    ssize_t bytes_sent = 0;

    while (bytes_left > 0) {
        bytes_sent = write(fd, buf_head, bytes_left);
        if (bytes_sent < 0) {
            syslog(LOG_ERR, "write: %s", strerror(errno));
            return -1;
        }

        bytes_left -= bytes_sent;
        buf_head += bytes_sent;
    }

    return 0;
}
