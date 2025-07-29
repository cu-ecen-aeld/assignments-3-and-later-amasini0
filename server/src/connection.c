#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include "../../aesd-char-driver/aesd_ioctl.h"

#define CONN_BUFSIZE 256

// Name of the file
extern const char* TMPFILE;

//
// Declarations of objects with external linkage defined in other source files.
//
// ...socket.c
int sock_gethost(int, char*, size_t);
char* sock_getline(int, size_t*);
int sock_putchars(int, char*, size_t);
//
// ...utils.c
int putchars(int, char*, size_t);

//
// Connection management
//
// ...connections linked list entry
struct cl_entry {
    int descriptor;
    bool is_active;
    pthread_t thread;
    pthread_mutex_t* io_mutex;
    SLIST_ENTRY(cl_entry) entries;
};
//
// ...connections linked list head
SLIST_HEAD(cl_head, cl_entry);

//
// Takes socket file descriptor associated to an incoming connection, and a
// file pointer. Receives a string of characters from the socket, writes it
// to file, then sends the whole content of the file to the socket.
// On success, 0 is returned. On failure, -1 is returned. 
//
void* conn_handler(void* handler_arg) {
    // Recover arguments structure.
    struct cl_entry* connection = (struct cl_entry*) handler_arg;
    bool abort = false;
    int error;

    char conn_host[NI_MAXHOST];
    if (sock_gethost(connection->descriptor, conn_host, sizeof(conn_host)) < 0) {
        strcpy(conn_host, "_gethost_failed_");
    }
    syslog(LOG_INFO, "Accepted connection from %s", conn_host);

    // Open temporary file.
    int fd = open(TMPFILE, O_RDWR|O_APPEND|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (fd < 0) {
        syslog(LOG_ERR, "open: %s", strerror(errno));
        abort = true;
        goto finalize;
    }

    // Receive packet from client. A packet ends when a newline is found in
    // the character stream obtained from the socket.
    // If the packet is received correctly write its content to file/do ioctl,
    // then free memory. Otherwise stop execution.
    size_t packet_size;
    char* packet = sock_getline(connection->descriptor, &packet_size);
    if (!packet) {
        abort = true;
        goto cleanup_fd;
    }
    syslog(LOG_INFO, "received %zu bytes from %s", packet_size, conn_host);
    
#ifdef USE_AESD_CHAR_DEVICE
    if (strncmp(packet, "AESDCHAR_IOCSEEKTO:", 19) == 0) {

        // We do ioctl
        syslog(LOG_INFO, "received string %.*s", (int) packet_size-1, packet);
        struct aesd_seekto seekto;

        char *start = packet + 19, *end = NULL;
        error = strtoul(packet+19, &end, 10);
        if (error == ULONG_MAX) {
            syslog(LOG_ERR, "strtoul: %s", strerror(errno));
            abort = true;
            goto cleanup;
        }
        syslog(LOG_INFO, "parsed write_cmd = %.*s", (int) (end - start), start);
        seekto.write_cmd = error;

        start = end+1; end = NULL;
        error = strtoul(start, &end, 10);
        if (error == ULONG_MAX) {
            syslog(LOG_ERR, "strtoul: %s", strerror(errno));
            abort = true;
            goto cleanup;
        }
        syslog(LOG_INFO, "parsed write_cmd_offset = %.*s", (int) (end - start), start);
        seekto.write_cmd_offset = error;

        if (ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto) < 0) {
            syslog(LOG_ERR, "ioctl: %s", strerror(errno));
            abort = true;
            goto cleanup;
        }

    } else {
#endif

        if ((error = pthread_mutex_lock(connection->io_mutex))) {
            syslog(LOG_ERR, "pthread_mutex_lock: %s", strerror(error));
        }

        int write_status = putchars(fd, packet, packet_size);

        if ((error = pthread_mutex_unlock(connection->io_mutex))) {
            syslog(LOG_ERR, "pthread_mutex_unlock: %s", strerror(error));
        }

        if (error != 0 || write_status < 0) {
            abort = true;
            goto cleanup;
        }
        syslog(LOG_INFO, "bytes written to %s", TMPFILE);

        // move to file start for reading
        if (lseek(fd, 0, SEEK_SET) == (off_t) -1) {
            syslog(LOG_ERR, "lseek: %s", strerror(errno));
            abort = true;
            goto cleanup;
        }

#ifdef USE_AESD_CHAR_DEVICE
    } // end else
#endif

    // Send the whole content of the file to the connected client.
    char buffer[CONN_BUFSIZE];

    ssize_t bytes_read;
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        if (sock_putchars(connection->descriptor, buffer, bytes_read) < 0) {
            abort = true;
            goto cleanup;
        }
    }

    if (bytes_read < 0) {
        syslog(LOG_ERR, "read: %s", strerror(errno));
        abort = true;
    }

  cleanup: 
    // Free received packet
    free(packet);

  cleanup_fd:
    // Close file and connection, then exit thread
    if (close(fd) < 0) {
        syslog(LOG_ERR, "close: %s", strerror(errno));
        abort = true;
    }

  finalize:
    if (close(connection->descriptor) < 0) {
        syslog(LOG_ERR, "close: %s", strerror(errno));
        abort = true;
    }
    syslog(LOG_INFO, "Closed connection from %s", conn_host);

    connection->is_active = false;
    connection->descriptor = abort ? -1 : 0; // Reuse as storage for retval.
    pthread_exit(&connection->descriptor);
}
