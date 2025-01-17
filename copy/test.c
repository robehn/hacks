#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include "copy.h"

#define SIZE 24*1024*1024

unsigned long data[3][SIZE] = {-1};

void sigint_handler(int signo, siginfo_t *info, void *context) {
    copy(data[0], data[1], SIZE);
}

static void* runner(void* arg) {
  while (raise(SIGINT) == 0) {
    usleep(100);
  }
}

int main(int argc, char* argv) {
  struct sigaction sa;
  pthread_t intr_thread;

  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = sigint_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  
  pthread_create(&intr_thread, 0, runner, 0);
  
  for (int i = 0; i < 100; i++) {
    copy(data[0], data[1], SIZE);
    copy(data[1], data[2], SIZE);
    copy(data[2], data[0], SIZE);
  }
  return 0;
}
