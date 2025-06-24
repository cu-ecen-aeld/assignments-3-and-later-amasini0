#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define PORT "9000"
#define BACKLOG 10
#define BUFSIZE 256

void _cleanup_handler(int) {
    syslog(LOG_INFO, "Caught signal, exiting");
    closelog();
    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {
    if (argc == 1) {
        printf("PROGRAM MODE\n");
    }
    else if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        printf("DEAMON MODE\n");
    } 
    else {
        printf("aesdsocket: Usage: aesdsocket [-d]\n");
        exit(-1);
    }

    openlog("aesdsocket", LOG_PERROR, LOG_USER);

    /*
     * Register actions for SIGINT and SIGTERM.
     *
     */ 
    struct sigaction cleanup;
    cleanup.sa_handler = _cleanup_handler;
    sigfillset(&cleanup.sa_mask); // block all signals during cleanup

    if (sigaction(SIGINT, &cleanup, NULL) < 0) {
        syslog(LOG_ERR, "sigaction: %s", strerror(errno));
        exit(-1);
    }

    if (sigaction(SIGTERM, &cleanup, NULL) < 0) {
        syslog(LOG_ERR, "sigaction: %s", strerror(errno));
        exit(-1);
    }


    /*
     * Create a socket, bind to loopback address on given port and start 
     * listening for incoming connections.
     *
     */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

    struct addrinfo *server_info;
    int error;
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

    if (bind(socket_fd, server_info->ai_addr, server_info->ai_addrlen) < 0) {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        exit(-1);
    }

    if (listen(socket_fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        exit(-1);
    }

    char server_ip[NI_MAXHOST];
    error = getnameinfo(server_info->ai_addr, server_info->ai_addrlen, server_ip, sizeof(server_ip), NULL, 0, 0);
    if (error != 0) {
        syslog(LOG_ERR, "getnameinfo: %s", gai_strerror(error));
        exit(-1);
    }
    syslog(LOG_INFO, "Server listening on %s port %s", server_ip, PORT);
    syslog(LOG_INFO, "Waiting for connections...");

    freeaddrinfo(server_info); // not needed after this point


    /*
     * Keep accepting connections until receiving either a SIGINT or a SIGTERM.
     * 
     */
    FILE* fp = fopen("/var/tmp/aesdsocketdata", "a+");
    if (!fp) {
        syslog(LOG_ERR, "fopen: %s", strerror(errno));
        exit(-1);
    }
    int fp_error = 0;

    for (;;) {
        struct sockaddr_storage client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int connection_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &client_addrlen);
        if (connection_fd < 0) {
            syslog(LOG_ERR, "accept: %s", strerror(errno));
            exit(-1);
        }
        

        // Log client IP
        char client_ip[NI_MAXHOST];
        error = getnameinfo((struct sockaddr*)&client_addr, client_addrlen, client_ip, sizeof(client_ip), NULL, 0, 0);
        if (error != 0) {
            syslog(LOG_ERR, "getnameinfo: %s", gai_strerror(error));
            exit(-1);
        }
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);


        // Receive packet and write it to file
        char buffer[BUFSIZE];
        buffer[BUFSIZE - 1] = '\0';
        size_t bufsize = sizeof(buffer) - 1;
        ssize_t count = 0;
       
        for(;;) {
            count = recv(connection_fd, buffer, bufsize, 0);
            if (count < 0) {
                syslog(LOG_ERR, "recv: %s", strerror(errno));
                exit(-1);
            }

            if (count == 0) {
                break;
            }

            // Check if newline was received
            char* line_end = strchr(buffer, '\n');
            if (line_end) {
                *(line_end + 1) = '\0';
            }

            // Write to file
            fputs(buffer, fp);

            if (line_end) {
                fflush(fp);
                break;
            }
        }


        // Send all content of file to client
        fseek(fp, 0, SEEK_SET);

        for (;;) {
            count = fread(buffer, 1, bufsize, fp);
            fp_error = ferror(fp);
            if (fp_error != 0) {
                syslog(LOG_ERR, "fread: %s", strerror(fp_error));
                exit(-1);
            }

            if (count == 0) {
                break;
            }

            // make sure the buffer is completely sent before loading next
            size_t bytes_sent = 0;
            size_t bytes_left = count; 
            char* start = buffer;

            while (bytes_left > 0) {
                bytes_sent = send(connection_fd, start, bytes_left, 0);
                if (bytes_sent < 0) {
                    syslog(LOG_ERR, "send: %s", strerror(errno));
                    exit(-1);
                }

                start += bytes_sent;
                bytes_left -= bytes_sent;
            }
        }
    }

    fclose(fp);
    closelog();
    return 0;
}
