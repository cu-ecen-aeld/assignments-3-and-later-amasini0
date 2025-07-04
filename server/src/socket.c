#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

// 
// Contants.
const size_t SOCK_READBUFSIZE = 64;
//
// Global variables.
extern bool sig_exit;

//
// Creates a TCP socket that listens on the given port on all net interfaces.
// Returns the socket file descriptor. If socket_addr is not NULL, it is used 
// to return a dynamically allocated string containing the socket address.
//
int sock_create(const char* node, const char* service) {
    int socket_fd, error;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM; // TCP socket
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST; // Receive connections (server).

    struct addrinfo *server_info;
    error = getaddrinfo(node, service, &hints, &server_info);
    if (error != 0) {
        syslog(LOG_ERR, "getaddrinfo: %s", gai_strerror(error));
        return -1;
    }

    socket_fd = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
    if (socket_fd < 0) {
        syslog(LOG_ERR, "socket: %s", strerror(errno));
        return -1;
    }

    int optval = 1; // Set socket option to enabled.
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        syslog(LOG_ERR, "setsockopt: %s", strerror(errno));
        return -1;
    }

    if (bind(socket_fd, server_info->ai_addr, server_info->ai_addrlen) < 0) {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        return -1;
    }

    freeaddrinfo(server_info); // This was allocated by getaddrinfo.

    return socket_fd;
}

// 
// Returns the host to which the socket is bound to.
//
int sock_gethost(int sockfd, char* host, size_t hostlen) {
    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    
    if (getsockname(sockfd, &addr, &addrlen) < 0) {
        syslog(LOG_ERR, "getsockname: %s", strerror(errno));
        return -1;
    }

    int error = getnameinfo(&addr, addrlen, host, hostlen, NULL, 0, NI_NUMERICHOST);
    if (error != 0) {
        syslog(LOG_ERR, "getnameinfo: %s", gai_strerror(error));
        return -1;
    }
    
    return 0;
}

//
// Reads characters from socket until a newline is found. On succes, returns a
// pointer to a dynamically allocated string containing the line. On failure,
// returns a NULL pointer.
//
char* sock_getline(int connection_fd) {
    char buffer[SOCK_READBUFSIZE+1];
    buffer[SOCK_READBUFSIZE] = '\0';
    size_t buflen = sizeof(buffer) / sizeof(buffer[0]);

    char* string = (char*) malloc(buflen * sizeof(*string));
    size_t maxreadlen = buflen - 1;
    size_t capacity = maxreadlen;
    size_t length = 0;

    bool abort = false; // Used to stop loop and cleanup allocated memory.

    while (!abort) {
        ssize_t count = recv(connection_fd, buffer, maxreadlen, 0);
        if (count < 0) {
            if (!sig_exit) // Log error only if not handling exit signal.
                syslog(LOG_ERR, "recv: %s", strerror(errno));
            abort = true;
            break;
        }

        // Reallocate if updated lenght
        size_t new_length = length + count;
        if (new_length > capacity) {
            size_t new_capacity = 2 * capacity;
            char* new_string;

            new_string = realloc(string, new_capacity * sizeof(*string));
            if (!new_string) {
                syslog(LOG_ERR, "realloc: %s", strerror(errno));
                abort = true;
                break;
            }

            string = new_string;
            capacity = new_capacity;
        }

        // Compute string tail, i.e. one after last valid char in string.
        char* str_tail = string + length;
        
        // Check if newline is present. If it is, append characters up to 
        // the newline to tail, then go to cleanup.
        char* newline_pos = strchr(buffer, '\n');
        if (newline_pos) {
            *(newline_pos + 1) = '\0';
            strcpy(str_tail, buffer);
            break;
        }
        
        strcpy(str_tail, buffer);
        length = new_length;
    }

    // If something went wrong free memory and return NULL pointer.
    if (abort) {
        free(string);
        return NULL;
    }

    return string;
}

// 
// Sends a buffer of bytes (chars) via a socket handling possible partial sends.
// On success, returns 0. On failure, returns -1.
//
int sock_putchars(int sock_fd, char* buffer, size_t bufsize) {
    size_t bytes_left = bufsize;
    char* buf_head = buffer; // One after the last successfully sent byte.

    while (bytes_left > 0) {
        ssize_t bytes_sent = send(sock_fd, buf_head, bytes_left, 0);
        if (bytes_sent < 0) {
            if (!sig_exit) // Log error only if not handling exit signal.
                syslog(LOG_ERR, "send: %s", strerror(errno));
            return -1;
        }

        buf_head += bytes_sent;
        bytes_left -= bytes_sent;
    }

    return 0;
}
