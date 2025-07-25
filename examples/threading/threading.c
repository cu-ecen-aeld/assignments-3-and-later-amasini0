#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // DONE: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    
    struct thread_data* data = (struct thread_data*) thread_param;
    int ret;

    // Create timespec structs for waiting
    struct timespec ts_lock   = {0, /*ms to ns*/ data->wait_to_obtain_ms  * 1000000};
    struct timespec ts_unlock = {0,              data->wait_to_release_ms * 1000000};

    // Wait to lock
    ret = nanosleep(&ts_lock, NULL);
    if (ret) {
        ERROR_LOG("failed to sleep until mutex lock");
        return thread_param;
    }
    ret = pthread_mutex_lock(data->p_mutex);
    if (ret) {
        ERROR_LOG("failed to obtain mutex");
        return thread_param;
    }

    // Wait to unlock
    ret = nanosleep(&ts_unlock, NULL);
    if (ret) {
        ERROR_LOG("failed to sleep until mutex unlock");
        return thread_param;
    }
    ret = pthread_mutex_unlock(data->p_mutex);
    if (ret) {
        ERROR_LOG("failed to release mutex");
        return thread_param;
    }

    // Set return parameter
    DEBUG_LOG("completed successfully");
    data->thread_complete_success = true;

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * DONE: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    
    // Init thread_data
    struct thread_data* args = malloc(sizeof(struct thread_data*));
    args->p_mutex = mutex;
    args->wait_to_obtain_ms = wait_to_obtain_ms;
    args->wait_to_release_ms = wait_to_release_ms;
    args->thread_complete_success = false;

    // Start thread
    int ret = pthread_create(thread, NULL, threadfunc, (void*)args);
    if (ret) {
        ERROR_LOG("failed to start thread");
        free(args);
        return false;
    }

    DEBUG_LOG("thread started successfully");
    return true;
}

