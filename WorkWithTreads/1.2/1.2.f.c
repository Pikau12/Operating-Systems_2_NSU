#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>


void *mythread(void *arg) {
  
    printf("mythread [pid=%d ppid=%d tid=%ld pthread=%lu]: Hello from mythread!\n",
        getpid(), getppid(), (long)gettid(), (unsigned long)pthread_self());
    return NULL;
}

int main(void) {
    pthread_t tid;
    int err;
    pthread_attr_t attr;

    pthread_attr_init(&attr);

    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    printf("main [pid=%d ppid=%d tid=%ld]: Hello from main!\n",
           getpid(), getppid(), (long)gettid());

    while (1) {  
       err = pthread_create(&tid, &attr, mythread, NULL);
        if (err) {
            fprintf(stderr, "main: pthread_create failed: %s\n", strerror(err));
            return 1;
        }
        pthread_attr_destroy(&attr);
    }
 
    return 0;

    // почему вирт память в 1000 раз больше чем физ память
    // как переполнить стек потока чтоб он перескочил заглушку
}