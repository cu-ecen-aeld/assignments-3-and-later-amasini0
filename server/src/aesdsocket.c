#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <syslog.h>
#include <unistd.h>

//
// Defs and constants.
#define PORT "9000"
#define BACKLOG 10
const char* TMPFILE = "/var/tmp/aesdsocketdata";
//
// Global variables.
bool sig_exit = false;

// 
// Declarations of objects with external linkage defined in other source files.
// 
// ...signal.c
int sig_setexit(int);
//
// ...socket.c
int sock_create(const char*, const char*);
int sock_gethost(int, char*, size_t);
//
// ...utils.c
void usage(void);
int daemonize(void);
//
// ...connection.c
struct cl_entry {
    pthread_t thread;
    int descriptor;
    bool is_active;
    SLIST_ENTRY(cl_entry) entries;
};
SLIST_HEAD(cl_head, cl_entry);
void* conn_handler(void*);

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

    // Create socket for accepting connections on port PORT. If the socket is
    // successfully created, daemonize the process, then start listening for 
    // incoming connections and log socket address to syslog.
    int sock_fd = sock_create(NULL, PORT);
    if (sock_fd < 0) {
        exit(-1);
    }

    if (daemon_mode) {
        if (daemonize() < 0) {
            exit(-1);
        }
    }

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
    // After accepting a new connection dispatch a thread that handles it, then
    // loop on all active threads to check if someone has finished.
    struct cl_head head;
    SLIST_INIT(&head);
    struct cl_entry* tail = NULL;

    bool abort = false; // Used skip to connection/program finalization.


    while(!abort) {
        int conn_fd = accept(sock_fd, NULL, NULL);
        if (conn_fd < 0) {
            if (!sig_exit) // Log error only if not handling exit signal.
                syslog(LOG_ERR, "accept: %s", strerror(errno));
            abort = true;
            break;
        }

        // Create thread and add it to the list
        struct cl_entry* connection = malloc(sizeof(struct cl_entry));
        connection->descriptor = conn_fd;
        connection->is_active = true;
        error = pthread_create(&connection->thread, NULL, conn_handler, (void*)connection);
        if (error < 0 && error != EAGAIN) {
            syslog(LOG_ERR, "pthread_create: %s", strerror(errno));
            abort = true;
        }
        
        if (tail) {
            SLIST_INSERT_AFTER(tail, connection, entries);
        } else {
            SLIST_INSERT_HEAD(&head, connection, entries);
        }
        tail = connection;

        // Loop and join threads that completed execution.
        struct cl_entry* previous = NULL;
        struct cl_entry* current = SLIST_FIRST(&head);

        while(current) {
            if (!current->is_active) {
                void* retval;
                error = pthread_join(current->thread, &retval);
                if (error < 0) {
                    syslog(LOG_ERR, "pthread_join: %s", strerror(error));
                    abort = true;
                }
                if (*(int*)retval < 0) {
                    syslog(LOG_ERR, "thread execution finished with error");
                    abort = true;
                }

                SLIST_REMOVE(&head, current, cl_entry, entries);

                // Set new current. If previous exists then set it as its next,
                // otherwise it means that first elem was removed, and we set
                // new current as the new first element.
                if (previous) {
                    current = SLIST_NEXT(previous, entries);
                } else {
                    current = SLIST_FIRST(&head);
                }
            } else {
                // No elimination happened, set previous as current, then get
                // the new current.
                previous = current;
                current = SLIST_NEXT(current, entries);
            }
        } // End join loop.
    }

    // Join all remaining threads (keep removing first element until empty).
    while (!SLIST_EMPTY(&head)) {
        struct cl_entry* connection = SLIST_FIRST(&head);
        void* retval;
        error = pthread_join(connection->thread, &retval);
        if (error < 0) {
            syslog(LOG_ERR, "pthread_join: %s", strerror(error));
        }
        if (*(int*)retval < 0) {
            syslog(LOG_ERR, "thread execution finished with error");
            abort = true;
        }
        SLIST_REMOVE_HEAD(&head, entries);
    }

    // Finalize program.
    error = remove(TMPFILE);
    if (error < 0) {
        syslog(LOG_ERR, "remove: %s: %s", TMPFILE, strerror(errno));
    }

    close(sock_fd);
    closelog();

    if (abort && !sig_exit) return -1;
    return 0;
}
