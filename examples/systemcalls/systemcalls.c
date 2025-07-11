#include "systemcalls.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>


/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    int ret = system(cmd);
    if (ret == -1) {
        perror("system");
        return false;
    }              

    int status = WEXITSTATUS(ret);
    if (status == 127) {
        printf("system: child process shell could not be executed\n");
        return false;
    } else if (status != 0) {
        printf("system: child process terminated with nonzero exit status: %d\n", status);
        return false;
    }

    return true;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    va_end(args);

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    int pid;
    switch (pid = fork()) {
        case -1:
            perror("fork");
            return false;
        case 0:
            // This executes in child process only
            if (execv(command[0], command) == -1) {
                perror("execv");
                _exit(127);
            }
        default: 
    }

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return false;
    }

    int exit_status = WEXITSTATUS(status);
    if (exit_status == 127) {
        return false;
    } else if (exit_status != 0) {
        printf("child process terminated with nonzero return value: %d\n", exit_status);
        return false;
    }

    return true;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    va_end(args);

/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
   
    // Open output file
    int fd = open(outputfile, O_WRONLY|O_CREAT|O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        perror("open");
        return false;
    }


    int pid;
    switch (pid = fork()) {
        case -1:
            perror("fork");
            return false;
        case 0:
            // This executes in child process only
            if (dup2(fd, 1) == -1) {
                perror("dup2");
                _exit(126);
            }
            close(fd);
            if (execv(command[0], command) == -1) {
                perror("execv");
                _exit(127);
            }
        default:
            close(fd);
    }

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return false;
    }

    int exit_status = WEXITSTATUS(status);
    if (exit_status == 126 || exit_status == 127) {
        return false;
    } else if (exit_status != 0) {
        printf("child process terminated with nonzero exit value: %d\n", exit_status);
        return false;
    }

    return true;
}
