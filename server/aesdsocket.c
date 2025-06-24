#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <errno.h>
#include <fcntl.h>
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

bool sig_received = false;

void _graceful_exit_handler(int) {
    syslog(LOG_INFO, "Caught signal. Exiting...");
    sig_received = true;
}

void usage(void) {
    printf("aesdsocket: Usage: aesdsocket [-d]\n");
}

int main(int argc, char** argv) {
    bool daemon_mode = false; // Wether to daemonize the program.
    int error; // USed for error handling throughout the program.
    
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


    // Register custom action for SIGINT and SIGTERM. When signaled the global
    // variable sig_received is set to true.
    struct sigaction graceful_exit;
    graceful_exit.sa_handler = _graceful_exit_handler;
    sigfillset(&graceful_exit.sa_mask); // Block all signals during handler execution.
    graceful_exit.sa_flags = 0;

    if (sigaction(SIGINT, &graceful_exit, NULL) < 0) {
        syslog(LOG_ERR, "sigaction: %s", strerror(errno));
        exit(-1);
    }

    if (sigaction(SIGTERM, &graceful_exit, NULL) < 0) {
        syslog(LOG_ERR, "sigaction: %s", strerror(errno));
        exit(-1);
    }


    // Create a socket, bind to loopback address on given port and start
    // listening for incoming connections.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

    struct addrinfo *server_info;
    error = getaddrinfo(NULL, PORT, &hints, &server_info); // listen on all netw interfaces on PORT
    // error = getaddrinfo("localhost", PORT, NULL, &server_info); // listen on localhost port PORT
    if (error != 0) {
        syslog(LOG_ERR, "getaddrinfo: %s", gai_strerror(error));
        exit(-1);
    }

    int socket_fd = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
    if (socket_fd < 0) {
        syslog(LOG_ERR, "socket: %s", strerror(errno));
        exit(-1);
    }

    int optval = 1; // Set option to enabled.
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        syslog(LOG_ERR, "setsockopt: %s", strerror(errno));
        exit(-1);
    }

    if (bind(socket_fd, server_info->ai_addr, server_info->ai_addrlen) < 0) {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        exit(-1);
    }

    char server_ip[NI_MAXHOST];
    error = getnameinfo(server_info->ai_addr, server_info->ai_addrlen, server_ip, sizeof(server_ip), NULL, 0, 0);
    if (error != 0) {
        syslog(LOG_ERR, "getnameinfo: %s", gai_strerror(error));
        exit(-1);
    }

    freeaddrinfo(server_info); // Not needed after this point.

    // Deamonize the process.
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid == -1) {
            syslog(LOG_ERR, "fork: %s", strerror(errno));
            exit(-1);
        }

        // Call exit in parent process.
        if (pid != 0) {
            exit(EXIT_SUCCESS);
        }

        // Set child as session leader
        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid: %s", strerror(errno));
            exit(-1);
        }

        // Change workdir to root dir
        if (chdir("/") < 0) {
            syslog(LOG_ERR, "chdir: %s", strerror(errno));
            exit(-1);
        }

        // Redirect stdin, stdout, stderr to /dev/null
        int fd = open("/dev/null", O_RDWR);
        if (dup2(fd, 0) < 0 ||
            dup2(fd, 1) < 0 ||
            dup2(fd, 2) < 0) {
            syslog(LOG_ERR, "dup2: %s", strerror(errno));
            exit(-1);
        }
        close(fd); // This is used only for redirection.
    }


    if (listen(socket_fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        exit(-1);
    }

    syslog(LOG_INFO, "Server listening on %s port %s", server_ip, PORT);
    syslog(LOG_INFO, "Waiting for connections...");



    // Keep accepting connections until receiving either a SIGINT or a SIGTERM.
    // Write received data to file, then send entire updated file to client.
    FILE* fp = fopen(TMPFILE, "a+");
    if (!fp) {
        syslog(LOG_ERR, "fopen: %s", strerror(errno));
        exit(-1);
    }
    bool aborted = false;

    while(!sig_received && !aborted) {
        // Accept incoming connection and log client IP address,
        struct sockaddr_storage client_addr;
        socklen_t client_addrlen = sizeof(client_addr);

        int connection_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &client_addrlen);
        if (connection_fd < 0) {
            if (sig_received) break; // Proceed to cleanup without logging error. 
            syslog(LOG_ERR, "accept: %s", strerror(errno));
            aborted = true;
            break;
        }

        char client_ip[NI_MAXHOST];
        error = getnameinfo((struct sockaddr*)&client_addr, client_addrlen, client_ip, sizeof(client_ip), NULL, 0, 0);
        if (error != 0) {
            syslog(LOG_ERR, "getnameinfo: %s", gai_strerror(error));
            exit(-1);
        }

        syslog(LOG_INFO, "Accepted connection from %s", client_ip);


        // Receive packets from client and write string to file. A packet is 
        // finished when '\n' is found in the character stream.
        char buffer[BUFSIZE];
        buffer[BUFSIZE - 1] = '\0';
        size_t bufsize = sizeof(buffer) - 1;
       
        while (!sig_received && !aborted) {
            ssize_t count = recv(connection_fd, buffer, bufsize, 0);
            if (count < 0) {
                if (sig_received) break; // Proceed to cleanup without logging error.
                syslog(LOG_ERR, "recv: %s", strerror(errno));
                aborted = true;
                break;
            }

            char* line_end = strchr(buffer, '\n');
            if (line_end) {
                // Write only until newline, then stop receiving.
                *(line_end + 1) = '\0';
                fputs(buffer, fp);
                fflush(fp);
                break;
            } else {
                fputs(buffer, fp);
            }
        }


        // Send content of the file to the client. Handle partial sends to make
        // sure all read bytes are correctly sent to client.
        fseek(fp, 0, SEEK_SET);
        while (!sig_received && !aborted && !feof(fp)) {
            size_t count = fread(buffer, 1, bufsize, fp);
            if (ferror(fp)) {
                syslog(LOG_ERR, "fread: %s", strerror(errno));
                exit(-1);
            }

            // Handling partial sends.
            ssize_t bytes_sent = 0;
            size_t bytes_left = count; 
            char* start = buffer;

            while (bytes_left > 0) {
                bytes_sent = send(connection_fd, start, bytes_left, 0);
                if (bytes_sent < 0) {
                    if (sig_received) break; // Proceed to cleanup without logging error.
                    syslog(LOG_ERR, "send: %s", strerror(errno));
                    aborted = true;
                    break;
                }

                start += bytes_sent;
                bytes_left -= bytes_sent;
            }
        }

        
        // Finalize connection
        if (close(connection_fd)) {
            syslog(LOG_ERR, "close: %s", strerror(errno));
        }
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    // Finalize program
    fclose(fp);
    error = remove(TMPFILE);
    if (error < 0) {
        syslog(LOG_ERR, "remove: %s: %s", TMPFILE, strerror(errno));
    }

    close(socket_fd);
    closelog();

    if (aborted) return -1;
    return 0;
}
