#include <cilk/cilk.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>
#include <sys/time.h>

#include "getoptions.h"
#include <byteswap.h>

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

 
 __attribute__((constructor(200))) void register_spawns() {
     uint32_t* offset = &_SPAWN_ADDR_MAP_START_;
     while (offset < &_SPAWN_ADDR_MAP_END_) {
         uintptr_t func_addr = *((uintptr_t*) offset);
         //printf("Found spawning function at %p\n", func_addr);
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
             //printf("Found retpad %p with landpad %p and poppad %p\n", retpad, landpad, poppad);
             int errval = __cilkrts_register_spawn_state(
                              retpad,
                              retpad,
                              poppad,
                              landpad);
             if (errval) printf("Error %d loading spawn states\n", errval);
         }
     }
 }
unsigned long long todval (struct timeval *tp) {
    return tp->tv_sec * 1000 * 1000 + tp->tv_usec;
}

void fib(int n, int* retdest);

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
int __attribute__((noinline)) run(int n, uint64_t *running_time, int i) {
    int res;
    struct timeval t1, t2;
    gettimeofday(&t1,0);
    fib(n, &res);
    gettimeofday(&t2,0);
    unsigned long long runtime_ms = (todval(&t2)-todval(&t1))/1000;
    printf("%f\n", runtime_ms/1000.0);
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
    fprintf(stderr, "Result: %d\n", res);
    if (argc > 1000) {
        fib_serial(argc, &n);
    }

    return 0;
}
