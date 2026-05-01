#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t got_int = 0;

static void sigint_handler(int signo) {
    (void)signo;
    got_int = 1;
    const char msg[] = "t2: caught SIGINT\n";
    write(STDOUT_FILENO, msg, sizeof msg - 1);
}

static void *thread1_all_blocked(void *arg) {
    (void)arg;
    puts("t1: all signals blocked");
    for (;;) pause(); 
    return NULL;
}
// что будет если у потоков нет обработчика сигналов и куда девается сигнал?
static void *thread2_sigint_handler(void *arg) {
    (void)arg;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);          
    sa.sa_flags = SA_RESTART;         
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        pthread_exit(NULL);
    }
    // что если у нас несколько потоков обрабатывают один сигнал 

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0) {
        perror("pthread_sigmask(SIG_UNBLOCK)");
        pthread_exit(NULL);
    }

    puts("t2: waiting for SIGINT");
    for (;;) pause(); 
    return NULL;
}

static void *thread3_sigquit_wait(void *arg) {
    (void)arg;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);

    puts("t3: waiting for SIGQUIT");
    int sig;
    int rc = sigwait(&set, &sig);
    if (rc == 0 && sig == SIGQUIT) {
        puts("t3: got SIGQUIT via sigwait()");
    } else {
        fprintf(stderr, "t3: sigwait failed: rc=%d sig=%d\n", rc, sig);
    }

    return NULL;
}

int main(void) {
    sigset_t all;
    sigemptyset(&all);
    sigaddset(&all, SIGINT);
    sigaddset(&all, SIGQUIT);
    if (pthread_sigmask(SIG_BLOCK, &all, NULL) != 0) {
        perror("pthread_sigmask(SIG_BLOCK)");
        return 1;
    }

    pthread_t t1, t2, t3;
    if (pthread_create(&t1, NULL, thread1_all_blocked, NULL) != 0) {
        perror("pthread_create t1");
        return 1;
    }
    if (pthread_create(&t2, NULL, thread2_sigint_handler, NULL) != 0) {
        perror("pthread_create t2");
        return 1;
    }
    if (pthread_create(&t3, NULL, thread3_sigquit_wait, NULL) != 0) {
        perror("pthread_create t3");
        return 1;
    }

    pthread_join(t3, NULL);
    puts("main: t3 finished; exiting process.");
    return 0;
}
