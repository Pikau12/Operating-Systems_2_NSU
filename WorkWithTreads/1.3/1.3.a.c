#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h> 

struct mystr {
    int number;
    char *str;
};

void *mythread(void *arg) {
    struct mystr *data = (struct mystr *)arg; 
    printf("mythread [pid=%d ppid=%d tid=%ld pthread=%lu]: num=%d, str=%s\n",
           getpid(), getppid(), (long)gettid(), (unsigned long)pthread_self(),
           data->number, data->str);
    return NULL;
}

int main(void) {
    pthread_t tid;
    int err;

    struct mystr pop = {42, "Hello World"};

    err = pthread_create(&tid, NULL, mythread, &pop); // передаём адрес структуры
    if (err) {
        fprintf(stderr, "pthread_create failed: %s\n", strerror(err));
        return 1;
    }


    printf("main [pid=%d ppid=%d tid=%ld]: Hello from main!\n",
           getpid(), getppid(), (long)gettid());

    pthread_join(tid, NULL); 

    return 0;
}