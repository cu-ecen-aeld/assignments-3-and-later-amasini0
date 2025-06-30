#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
//#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define CONN_BUFSIZE 256

// Name of the file
extern const char* TMPFILE;

//
// Declarations of objects with external linkage defined in other source files.
//
// ...socket.c
int sock_gethost(int, char*, size_t);
char* sock_getline(int);
int sock_putchars(int, char*, size_t);

//
// Connection management
//
// ...connections linked list entry
struct cl_entry {
    pthread_t thread;
    int descriptor;
    bool is_active;
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

    char conn_host[NI_MAXHOST];
    if (sock_gethost(connection->descriptor, conn_host, sizeof(conn_host)) < 0) {
        strcpy(conn_host, "_gethost_failed_");
    }
    syslog(LOG_INFO, "Accepted connection from %s", conn_host);

    // Open temporary file.
    FILE* file = fopen(TMPFILE, "a+");
    if (!file) {
        abort = true;
        goto cleanup;
    }

    // Receive packet from client. A packet ends when a newline is found in
    // the character stream obtained from the socket.
    // If the packet is received correctly write its contend to file, then
    // free memory. Otherwise stop execution.
    char* packet = sock_getline(connection->descriptor);
    if (!packet) {
        abort = true;
        goto cleanup;
    }

    // Write to file then free string. After free, check write status and go to
    // cleanup if necessary.
    int write_status = fputs(packet, file);
    free(packet);
    
    if (write_status < 0) {
        syslog(LOG_ERR, "fputs: %s", "who knows");
        abort = true;
        goto cleanup;
    }

    // Send the whole content of the file to the connected client.
    char buffer[CONN_BUFSIZE];
    fseek(file, 0, SEEK_SET);

    while (!feof(file)) {
        size_t count = fread(buffer, 1, sizeof(buffer), file);
        if (ferror(file)) {
            syslog(LOG_ERR, "fread: %s", strerror(errno));
            abort = true;
            goto cleanup;
        }

        if (sock_putchars(connection->descriptor, buffer, count) < 0) {
            abort = true;
            goto cleanup;
        }
    }

cleanup: // Close file and connection, then exit thread
    if (fclose(file) != 0) {
        syslog(LOG_ERR, "fclose: %s", strerror(errno));
        abort = true;
    }

    // Finalize connection
    if (close(connection->descriptor)) {
        syslog(LOG_ERR, "close: %s", strerror(errno));
        abort = true;
    }
    syslog(LOG_INFO, "Closed connection from %s", conn_host);

    connection->is_active = false;
    int retval = abort ? -1 : 0;
    pthread_exit(&retval);
}
