#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

int global_var = 1;
const int global_const = 412;

void *mythread(void *arg) {
    int local = 10;
    const int const_var = 10;
    static int static_var = 3;
    printf("mythread [pid=%d ppid=%d tid=%ld pthread=%lu]: Hello from mythread!\n",
        getpid(), getppid(), (long)gettid(), (unsigned long)pthread_self());

    printf("[pid=%d ppid=%d tid=%ld pthread=%lu]: global - %p |  global const - %p |  local - %p | const var - %p | static var - %p\n",
        getpid(), getppid(), (long)gettid(), (unsigned long)pthread_self(),
        (void*)&global_var, (void*)&global_const, (void*)&local, (void*)&const_var, (void*)&static_var);

     printf("[pid=%d ppid=%d tid=%ld pthread=%lu]: global - %p | %d |  local - %p | %d |\n",
        getpid(), getppid(), (long)gettid(), (unsigned long)pthread_self(),
        (void*)&global_var, global_var, (void*)&local, local);
    
    local = 1245;
    global_var = 4567;

    printf("[pid=%d ppid=%d tid=%ld pthread=%lu]: global - %p | %d |  local - %p | %d |\n",
        getpid(), getppid(), (long)gettid(), (unsigned long)pthread_self(),
        (void*)&global_var, global_var, (void*)&local, local);
    return NULL;
}

int main(void) {
    pthread_t tids[5];
    int err;

    printf("main [pid=%d ppid=%d tid=%ld]: Hello from main!\n",
           getpid(), getppid(), (long)gettid());

    for (int i = 0; i < 5; i++) {
        err = pthread_create(&tids[i], NULL, mythread, NULL);
       
        if (err) {
            fprintf(stderr, "main: pthread_create(%d) failed: %s\n", i, strerror(err));
            for (int j = 0; j < i; j++) pthread_join(tids[j], NULL);
            return 1;
        }
    }



    for (int i = 0; i < 5; i++) {
        if (pthread_join(tids[i], NULL) != 0) {
            perror("pthread_join");
            return 1;
        }
    }

    for (int i = 0; i < 5; i++) {
        printf("Созданный поток tids[%d] = %lu\n", i, (unsigned long)tids[i]);
    }

    sleep(100);

    return 0;
}