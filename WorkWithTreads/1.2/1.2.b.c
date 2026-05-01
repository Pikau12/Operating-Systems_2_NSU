#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>


void *mythread(void *arg) {
    sleep(1);
    printf("mythread [pid=%d ppid=%d tid=%ld pthread=%lu]: Hello from mythread!\n",
        getpid(), getppid(), (long)gettid(), (unsigned long)pthread_self());
    
    return (void*)42;
}

int main(void) {
    pthread_t tids;
    int err;
    void *number;

    printf("main [pid=%d ppid=%d tid=%ld]: Hello from main!\n",
           getpid(), getppid(), (long)gettid());

    err = pthread_create(&tids, NULL, mythread, NULL);
    if (err) {
        fprintf(stderr, "main: pthread_create failed: %s\n", strerror(err));
        return 1;
    }

    if (pthread_join(tids, &number) != 0) {
            perror("pthread_join");
            return 1;
    }

    printf("Return value = %ld\n", (long)number);



    //1,1 pthread_self что возвращает на самом деле
    //1,1 зачем нужна заглужка между стеками и можем ли её перескочить
    //1,2 почему принтф в потоке не напечатался

    return 0;
}