#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h> 


static void cleanup_free(void *p) {
    printf("cleanup: free(%p)\n", p);
    free(p);
}

void *mythread(void *arg) {
    char *s = malloc(12);              
    if (!s) return NULL;
    strcpy(s, "hello world");

    // pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    // pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    pthread_cleanup_push(cleanup_free, s);
    // механика работы cleanup'ов

    while (1) {
        puts(s);              
        fflush(stdout);
        usleep(20 * 100);   
    }

    pthread_cleanup_pop(1);    
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

    sleep(10);
    printf("Main: sending cancel request\n");
    pthread_cancel(tid);   
    pthread_join(tid, NULL); 

    return 0;
}