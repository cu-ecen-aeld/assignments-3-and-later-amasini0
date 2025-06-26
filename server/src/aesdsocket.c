#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define PORT "9000"
#define BACKLOG 10
#define BUFSIZE 256
#define TMPFILE "/var/tmp/aesdsocketdata"

//
// Global variable for signal handling.
//
bool sig_received = false;

// 
// Declarations of external functions defined in other source files.
// 
// ...signal.c
int sig_setexit(int);
//
// ...socket.c
int sock_create(const char*, const char*);
int sock_gethost(int, char*, size_t);
char* sock_getline(int);
int sock_putbytes(int, char*, size_t);

//
// ...utils.c
void usage(void);
int daemonize(void);

//
// Main program.
//
int main(int argc, char** argv) {
    bool daemon_mode = false; // Wether to daemonize the program.
    int error; // Used for error handling throughout the program.
    
    openlog("aesdsocket", LOG_PERROR, LOG_USER);

    if (argc > 2) {
        // Too many args.
        usage();
        exit(-1);
    } else if (argc == 2) {
        if (strcmp(argv[1], "-d") != 0) {
            // Invalid option.
            usage();
            exit(-1);
        }

        daemon_mode = true;
    }

    // Register SIGINT and SIGTERM as (graceful) exit signals.
    if (sig_setexit(SIGTERM) < 0) {
        exit(-1);
    }
    if (sig_setexit(SIGINT) < 0) {
        exit(-1);
    }

    // Create passive socket for accepting connections.
    int sock_fd = sock_create(NULL, PORT);
    if (sock_fd < 0) {
        exit(-1);
    }

    // Deamonize the process.
    if (daemon_mode) {
        if (daemonize() < 0) {
            exit(-1);
        }
    }

    // Start listening for incoming connections.
    if (listen(sock_fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        exit(-1);
    }
    
    char sock_host[NI_MAXHOST];
    if (sock_gethost(sock_fd, sock_host, sizeof(sock_host)) < 0) {
        exit(-1);
    }
    syslog(LOG_INFO, "Server listening on %s port %s", sock_host, PORT);
    syslog(LOG_INFO, "Waiting for connections...");

    // Keep accepting connections until receiving either a SIGINT or a SIGTERM.
    // Write received data to file, then send entire updated file to client.
    FILE* fp = fopen(TMPFILE, "a+");
    if (!fp) {
        syslog(LOG_ERR, "fopen: %s", strerror(errno));
        exit(-1);
    }

    bool abort = false; // Used skip to connection/program finalization.

    while(!abort) {
        // Accept an incoming connection and log hostname of connected client.
        int conn_fd = accept(sock_fd, NULL, NULL);
        if (conn_fd < 0) {
            if (!sig_received) // Log error only if not handling exit signal.
                syslog(LOG_ERR, "accept: %s", strerror(errno));
            abort = true;
            break;
        }

        char conn_host[NI_MAXHOST];
        if (sock_gethost(conn_fd, conn_host, sizeof(conn_host)) < 0) {
            abort = true;
        }
        syslog(LOG_INFO, "Accepted connection from %s", conn_host);

        // Receive packet from client. A packet ends when a newline is found in
        // the character stream obtained from the socket.
        // If the packet is received correctly write its contend to file, then
        // free memory.
        char* packet = sock_getline(conn_fd);
        if (packet) {
            if (fputs(packet, fp) < 0) {
                syslog(LOG_ERR, "fputs: %s", "bubu");
                abort = true;
            }
            free(packet);
        } else {
            abort = true;
        }

        // Send content of the file to client.
        char buffer[BUFSIZE];

        fseek(fp, 0, SEEK_SET);
        while (!abort && !feof(fp)) {
            size_t count = fread(buffer, 1, sizeof(buffer), fp);
            if (ferror(fp)) {
                syslog(LOG_ERR, "fread: %s", strerror(errno));
                abort = true;
            }

            if (sock_putbytes(conn_fd, buffer, count) < 0) {
                abort = true;
            }
        }

        // Finalize connection
        if (close(conn_fd)) {
            syslog(LOG_ERR, "close: %s", strerror(errno));
        }
        syslog(LOG_INFO, "Closed connection from %s", conn_host);
    }

    // Finalize program
    fclose(fp);
    error = remove(TMPFILE);
    if (error < 0) {
        syslog(LOG_ERR, "remove: %s: %s", TMPFILE, strerror(errno));
    }

    close(sock_fd);
    closelog();

    if (abort && !sig_received) return -1;
    return 0;
}
