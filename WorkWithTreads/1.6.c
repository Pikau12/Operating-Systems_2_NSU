#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define PAGE 4096
#define STACK_SIZE 1024 * 1024

typedef void*(*mythread_routine_t)(void*);

struct mythread_t {
    int                      tid;
    void*                    stack;
    mythread_routine_t  routine;
    void*                    arg;
    void*                    ret;
    _Atomic int                      exited;

    struct mythread_t *shared;
};

void print_mythread(struct mythread_t* mythread) {
    printf("struct mythread_t {\n\taddr: %p,\n\ttid: %d,\n\tstack: %p,\n\troutine: %p,\n\targ: %p,\n\tret: %p,\n\texited: %d\n}\n", 
        (void*)mythread, mythread->tid, mythread->stack, (void*)mythread->routine,
        mythread->arg, mythread->ret,
        atomic_load_explicit(&mythread->exited, memory_order_relaxed)); 
}
int mythread_startup(void* arg) {
    struct mythread_t* mythread = (struct mythread_t*)arg;

    printf("mythread_startup [%d]: ", mythread->tid);
    print_mythread(mythread);
    
    mythread->ret = mythread->routine(mythread->arg);
    atomic_store_explicit(&mythread->exited, 1, memory_order_release);
    
    printf("mythread_startup [%d]: ", mythread->tid);
    print_mythread(mythread);
    
    return 0;
}
int mythread_create(struct mythread_t* mythread, mythread_routine_t mythread_routine, void* arg) {
    static int mythread_num = 0;

    mythread->stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mythread->stack == MAP_FAILED) {
        perror("mmap");
        mythread->stack = NULL;
        return -1;
    }

    mythread->tid = mythread_num++;
    mythread->routine = mythread_routine;
    mythread->arg = arg;
    mythread->ret = NULL;
    atomic_store_explicit(&mythread->exited, 0, memory_order_relaxed);

    mythread->shared = (struct mythread_t*)malloc(sizeof(struct mythread_t));
    if (!mythread->shared) {
        perror("malloc(shared)");
        munmap(mythread->stack, STACK_SIZE);
        mythread->stack = NULL;
        return -1;
    }
    *mythread->shared = *mythread; 
    void *stack_top = (char*)mythread->stack + STACK_SIZE;

    int child_pid = clone(mythread_startup, stack_top,
                          CLONE_VM | CLONE_SIGHAND | CLONE_THREAD,
                          (void*)mythread->shared);
    if (child_pid == -1) {
        perror("clone");
        free(mythread->shared);           
        mythread->shared = NULL;
        munmap(mythread->stack, STACK_SIZE);
        mythread->stack = NULL;
        return -1;
    }

    return 0;
}

int mythread_join(struct mythread_t *mythread, void** ret) {
    struct mythread_t *shared = mythread->shared;
    if (!shared) {
        errno = EINVAL;
        return -1;
    }

    while (atomic_load_explicit(&shared->exited, memory_order_acquire) == 0) {
        usleep(100000);
        printf("mythread_join[%d]: wait\n", shared->tid);
    }

    mythread->ret = shared->ret;
    if (ret) *ret = mythread->ret;

    free(shared);
    mythread->shared = NULL;

    int err = munmap(mythread->stack, STACK_SIZE);
    if (err == -1) {
        perror("munmap");
        return -1;
    }
    mythread->stack = NULL;

    return 0;
}

void* example_routine(void* arg) {
    int a = 71;
    int b = *(int*)arg;

    int* sum = (int*)malloc(sizeof(int));
    *sum = a + b;
    sleep(2);

    return (void*)sum;
}

int main() {
    struct mythread_t mythread1;
    struct mythread_t mythread2;

    int arg1 = 33;
    int err = mythread_create(&mythread1, example_routine, &arg1);
    if (err == -1) {
        perror("mythread_create");
        return -1;
    }
    printf("main: create thread %d\n", mythread1.tid);

    int arg2 = 4;
    err = mythread_create(&mythread2, example_routine, &arg2);
    if (err == -1) {
        perror("mythread_create");
        return -1;
    }
    printf("main: create thread %d\n", mythread2.tid);
    
    void* ret;
    err = mythread_join(&mythread1, &ret);
    if (err == -1) {
        perror("mythread_join");
        return -1;
    }
    printf("main: Sum1: %d\n", *(int*)ret);
    free(ret);

    err = mythread_join(&mythread2, &ret);
    if (err == -1) {
        perror("mythread_join");
        return -1;
    }
    printf("main: Sum2: %d\n", *(int*)ret);
    free(ret);

    return 0;
}