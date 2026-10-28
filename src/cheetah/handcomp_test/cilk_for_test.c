#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <cilk/cilk.h>
#include "ktiming.h"

#define true 1
#define false 0

#ifndef TIMING_COUNT
#define TIMING_COUNT 1
#endif

int check_validity(int* work_array, int parallel_jobs, int inner_work) {
	for(int i = 0; i < parallel_jobs; i++) {
		if (work_array[i] != inner_work){
			printf("broke when index %d was equal to %d instead of %d", i, 
                work_array[i], inner_work);
			return false;
		}
	}
	return true;
}

void work_loop(int* work_array, int parallel_jobs, int inner_work) {
	cilk_for(int i = 0; i < parallel_jobs; i++) {
		for(int j = 0; j < inner_work; j++) {
			work_array[i] += 1;
			asm volatile ("nop");
		}
	}
}

int main(int argc, char * args[]) {
    int i;
    int parallel_jobs, inner_work;
    clockmark_t begin, end;
    uint64_t running_time[TIMING_COUNT];
    if(argc != 3) {
        fprintf(stderr, "Usage: cilk_for_test [<cilk-options>] <PARALLEL_WORK> <INNER_WORK\n");
        exit(1);
    }

    parallel_jobs = atoi(args[1]);
    inner_work = atoi(args[2]);
    int* work_array = calloc(parallel_jobs, sizeof(int));
    for(i = 0; i < TIMING_COUNT; i++) {
        begin = ktiming_getmark();
        work_loop(work_array, parallel_jobs, inner_work);
        end = ktiming_getmark();
        running_time[i] = ktiming_diff_nsec(&begin, &end);
    }
    if (check_validity(work_array,parallel_jobs,inner_work)) {
	    printf("Valid execution\n");
    } else {
	    printf(" Invalid execution\n");
    }
    print_runtime(running_time, TIMING_COUNT);

    return 0;
}