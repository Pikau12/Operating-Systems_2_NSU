#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>


void *mythread(void *arg) {
    int rc = pthread_detach(pthread_self());
    if (rc != 0) {
        fprintf(stderr, "pthread_detach: %s\n", strerror(rc));
        return NULL;
    }
    printf("mythread [pid=%d ppid=%d tid=%ld pthread=%lu]: Hello from mythread!\n",
        getpid(), getppid(), (long)gettid(), (unsigned long)pthread_self());
    sleep(200);

    return NULL;
}

int main(void) {
    pthread_t tid;
    int err;

    printf("main [pid=%d ppid=%d tid=%ld]: Hello from main!\n",
           getpid(), getppid(), (long)gettid());

    while (1) {  
        err = pthread_create(&tid, NULL, mythread, NULL);
        if (err) {
            fprintf(stderr, "main: pthread_create failed: %s\n", strerror(err));
            return 1;
        }
    }


    return 0;
}