#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <cilk/cilk.h>
#include <string.h>
#include "../cilkbench/register_spawns.h"

#define true 1
#define false 0

#ifndef TIMING_COUNT
#define TIMING_COUNT 1
#endif

int check_validity(int* work_array, int parallel_jobs, int inner_work) {
    for (int i = 0; i < parallel_jobs; i++) {
        if (work_array[i] != inner_work) {
            printf("broke when index %d was equal to %d instead of %d", i, work_array[i], inner_work);
            return false;
        }
    }
    return true;
}

void inner_loop(int* work_array, int inner_work, int i) {
    for (int j = 0; j < inner_work; j++) {
        work_array[i] += 1;
        asm volatile ("nop");
    }
}

void work_loop(int* work_array, int parallel_jobs, int inner_work) {
    for (int i = 0; i < parallel_jobs; i++) {
        cilk_spawn inner_loop(work_array, inner_work, i);
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
    for (i = 0; i < TIMING_COUNT; i++) {
        memset(work_array, 0, parallel_jobs * sizeof(int));
        work_loop(work_array, parallel_jobs, inner_work);
    }
    uncilkify(default_cilkrts);
    int valid = check_validity(work_array, parallel_jobs, inner_work);
    if (valid) {
        printf("Valid execution\n");
    } else {
        printf(" Invalid execution\n");
    }

    free(work_array);
    return valid ? 0 : 1;
}


