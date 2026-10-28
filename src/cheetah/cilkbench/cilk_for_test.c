#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <cilk/cilk.h>
#include <string.h>
#include "../runtime/cilk2c.h"
#include <sys/time.h>
#include "getoptions.h"

#define true 1
#define false 0

#ifndef TIMING_COUNT
#define TIMING_COUNT 1
#endif

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


int check_validity(int* work_array, int parallel_jobs, int inner_work) {
    for (int i = 0; i < parallel_jobs; i++) {
        if (work_array[i] != inner_work) {
            printf("broke when index %d was equal to %d instead of %d", i, work_array[i], inner_work);
            return false;
        }
    }
    return true;
}

void work_loop(int* work_array, int parallel_jobs, int inner_work) {
    cilk_for (int i = 0; i < parallel_jobs; i++) {
        for (int j = 0; j < inner_work; j++) {
            work_array[i] += 1;
            __asm__("");
        }
    }
}

int main(int argc, char * args[]) {
    int i;
    int parallel_jobs, inner_work;
    if (argc != 3) {
        fprintf(stderr, "Usage: cilk_for_test [<cilk-options>] <PARALLEL_WORK> <INNER_WORK\n");
        exit(1);
    }

    parallel_jobs = atoi(args[1]);
    inner_work = atoi(args[2]);
    if (parallel_jobs <= 0 || inner_work <= 0) return 1;
    int* work_array = calloc(parallel_jobs, sizeof(int));
    if (!work_array) {
        printf("mem failure, exiting \n");
        exit(1);
    }
    cilkify(default_cilkrts);
    struct timeval t1, t2;
    gettimeofday(&t1,0);
    for (i = 0; i < TIMING_COUNT; i++) {
        memset(work_array, 0, parallel_jobs * sizeof(int));
        work_loop(work_array, parallel_jobs, inner_work);
    }
    gettimeofday(&t2,0);
    unsigned long long runtime_ms = (todval(&t2)-todval(&t1))/1000;
    uncilkify(default_cilkrts);
    printf("%f\n", runtime_ms/1000.0);
    int valid = check_validity(work_array, parallel_jobs, inner_work);
    if (valid) {
        printf("Valid execution\n");
    } else {
        printf(" Invalid execution\n");
    }

    free(work_array);
    return valid ? 0 : 1;
}


