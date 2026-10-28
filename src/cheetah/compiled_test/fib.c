#include <cilk/cilk.h>
#include <stdio.h>
#include <stdlib.h>
#include "ktiming.h"
#include <byteswap.h>
#include "../runtime/sched_stats.h"

#ifndef TIMING_COUNT
#define TIMING_COUNT 1
#endif

/*
 * fib 39: 63245986
 * fib 40: 102334155
 * fib 41: 165580141
 * fib 42: 267914296
 */

#include "../runtime/cilk2c.h"
extern uintptr_t _SPAWN_ADDR_MAP_START_;
extern uintptr_t _SPAWN_ADDR_MAP_END_;
extern global_state *default_cilkrts;

__attribute__((constructor(200))) void register_spawns() {
  printf("Registering Spawns\n");
  uint32_t* offset = &_SPAWN_ADDR_MAP_START_;
  while (offset < &_SPAWN_ADDR_MAP_END_) {
    uintptr_t func_addr = *((uintptr_t*) offset);
    printf("Found spawning function at %p\n", func_addr);
    offset += 2;
    uint64_t total_count = *((uintptr_t*) offset);
    offset += 2;
    for (int i = 0; i < total_count; i++) {
      offset += 3;
      uintptr_t retpad = func_addr + (*offset);
      offset += 4;
      uintptr_t landpad = func_addr + (*offset);
      offset += 4;
      uintptr_t poppad = func_addr + (*offset);
      offset += 1;
      printf("Found retpad %p with landpad %p and poppad %p\n", retpad, landpad, poppad);
      int errval = __cilkrts_register_spawn_state(
                     retpad,
                     retpad,
                     poppad,
                     landpad);
      if (errval) printf("Error %d loading spawn states\n", errval);
    }
  }
}

void fib(int n, int* retdest) {
    int x, y;

    if (n < 2) {
        *retdest = n;
        return;
    }
    cilk_spawn fib(n - 1, &x);
    fib(n - 2, &y);
    cilk_sync;
    *retdest = x + y;
    return;
}

void fib_serial(int n, int*retdest) {
    int x, y;

    if (n < 2) {
        *retdest = n;
        return;
    }
    fib(n - 1, &x);
    fib(n - 2, &y);
    *retdest = x + y;
    return;
}

int __attribute__((noinline)) run(int n, uint64_t *running_time, int i) {
    int res;
    clockmark_t begin, end;
    begin = ktiming_getmark();
    fib(n, &res);
    end = ktiming_getmark();
    running_time[i] = ktiming_diff_nsec(&begin, &end);

    return res;
}

int main(int argc, char * args[]) {
    int i;
    int n, res;
    if (argc != 2) {
        fprintf(stderr, "Usage: fib [<cilk-options>] <n>\n");
        exit(1);
    }

    n = atoi(args[1]);
    cilkify(default_cilkrts);
    uint64_t running_time[TIMING_COUNT];
    for (i = 0; i < TIMING_COUNT; i++) {
        res = run(n, running_time, i);
    }
    uncilkify(default_cilkrts);
    printf("Result: %d\n", res);
    if (argc > 1000) {
        fib_serial(argc, &n);
    }

    if ( TIMING_COUNT > 10 )
        print_runtime_summary(running_time, TIMING_COUNT);
    else
        print_runtime(running_time, TIMING_COUNT);
    return 0;
}