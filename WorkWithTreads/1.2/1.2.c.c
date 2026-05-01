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

    return "Hello world";
}

int main(void) {
    pthread_t tids;
    int err;
    void *str;

    printf("main [pid=%d ppid=%d tid=%ld]: Hello from main!\n",
           getpid(), getppid(), (long)gettid());

    err = pthread_create(&tids, NULL, mythread, NULL);
    if (err) {
        fprintf(stderr, "main: pthread_create failed: %s\n", strerror(err));
        pthread_join(tids, NULL);
        return 1;
    }

    if (pthread_join(tids, &str) != 0) {
        perror("pthread_join");
        return 1;
    }


    printf("Return value = %s\n", (char*)str);

    return 0;
}