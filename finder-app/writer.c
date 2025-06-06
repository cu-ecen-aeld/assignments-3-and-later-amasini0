#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
    int errnum;

    // Open log    
    openlog("aeld-writer", 0, LOG_USER);

    // Validate command line arguments
    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: received %d, expected 2", argc-1);
        exit(1);
    }

    const char* writefile = argv[1];
    const char* writestr = argv[2];

    if (strcmp(writefile, "") == 0) {
        syslog(LOG_ERR, "Invalid argument: file name cannot be empty");
        exit(1);
    }
    
    // Write to file
    int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, \
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        errnum = errno;
        syslog(LOG_ERR, "Could not open %s: %s", writefile, strerror(errnum));
        exit(1);
    }

    size_t count = strlen(writestr);
    ssize_t nr;

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    while (count > 0) {
        nr = write(fd, writestr, count);

        if (nr == -1) {
            errnum = errno;
            syslog(LOG_ERR, "Could not write to %s: %s", writefile, strerror(errnum));
            exit(1);
        } else if (nr != count) {
            syslog(LOG_ERR, "Could not complete write to %s: retrying", writefile);
        }

        count -= nr; 
        writestr += nr;
    }

    if (close(fd) == -1) {
        errnum = errno;
        syslog(LOG_ERR, "Could not close %s: %s", writefile, strerror(errnum));
    }

    return 0;
}
