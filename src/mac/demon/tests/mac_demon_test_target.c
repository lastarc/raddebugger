// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

// Debuggee for mac_demon_test. Build w/ `clang -O0 -g`. Modes (argv[1]):
//   (none)  breakpoints, threads, a handled signal, dlopen, a debug trap;
//           exits w/ 42 if every step ran
//   spin    loops forever (attach, halt, detach, kill)
//   crash   writes through a null pointer

#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

volatile int g_counter = 0;

__attribute__((noinline)) void target_func(void) { g_counter += 1; }
__attribute__((noinline)) void target_caller(void) { target_func(); }
static void *thread_main(void *p) { target_func(); return 0; }
static void on_usr1(int signo) { g_counter += 100; }

int
main(int argc, char **argv)
{
  if(argc > 1 && strcmp(argv[1], "spin") == 0)
  {
    for(;;) { usleep(1000); }
  }
  if(argc > 1 && strcmp(argv[1], "crash") == 0)
  {
    *(volatile int *)0 = 1;
    return 3;
  }
  signal(SIGUSR1, on_usr1);
  target_caller();
  pthread_t thread;
  pthread_create(&thread, 0, thread_main, 0);
  pthread_join(thread, 0);
  raise(SIGUSR1);
  void *lib = dlopen("/usr/lib/libz.1.dylib", RTLD_NOW);
  if(lib == 0) { return 2; }
  __builtin_debugtrap();
  return (g_counter == 102) ? 42 : 1;
}
