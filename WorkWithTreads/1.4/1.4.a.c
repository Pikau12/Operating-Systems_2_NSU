#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>


void *mythread(void *arg) {
    while (1) {
        printf("mythread [pid=%d ppid=%d tid=%ld pthread=%lu]: Hello from mythread!\n",
        getpid(), getppid(), (long)gettid(), (unsigned long)pthread_self());
    }
    
    return NULL;
}

int main(void) {
    pthread_t tid;
    int err;

    printf("main [pid=%d ppid=%d tid=%ld]: Hello from main!\n",
           getpid(), getppid(), (long)gettid());

    err = pthread_create(&tid, NULL, mythread, NULL);
    if (err) {
        fprintf(stderr, "main: pthread_create failed: %s\n", strerror(err));
        return 1;
    }

    sleep(1);
    printf("Main: sending cancel request\n");
    pthread_cancel(tid);   
    pthread_join(tid, NULL); 

    return 0;
}