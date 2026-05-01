#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>

struct mystr {
    int number;
    const char *str; 
};

void *mythread(void *arg) {
    struct mystr *data = (struct mystr *)arg;

    printf("mythread [pid=%d ppid=%d tid=%ld pthread=%lu]: num=%d, str=%s\n",
           getpid(), getppid(), (long)gettid(), (unsigned long)pthread_self(),
           data->number, data->str);

    free(data);
    return NULL;
}

int main(void) {
    pthread_t tid;
    int err;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); 

    struct mystr *pop = malloc(sizeof *pop);
    if (!pop) { perror("malloc"); return 1; }
    
    pop->number = 42;
    pop->str    = "Hello World";

    err = pthread_create(&tid, &attr, mythread, pop);
    pthread_attr_destroy(&attr);
    if (err) {
        fprintf(stderr, "pthread_create failed: %s\n", strerror(err));
        free(pop);
        return 1;
    }

    printf("main [pid=%d ppid=%d tid=%ld]: Hello from main!\n",
           getpid(), getppid(), (long)gettid());

    pthread_exit(NULL);

    // надо ли освобождпать строчку и почему не падает
}
