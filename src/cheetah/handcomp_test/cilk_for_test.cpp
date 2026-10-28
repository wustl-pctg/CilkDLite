#include <iostream>
#include <functional>
#include <cstddef>
#include <stdio.h>
#include <stdlib.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <time.h>
#include <math.h>
#include <chrono>
#include <string>
#include <unistd.h>
#include <signal.h>
#include "numa_allocate.h"
#include "microparams.hpp"

#ifdef LIKWID_PERFMON
#include <likwid.h>
#else
#define LIKWID_MARKER_INIT
#define LIKWID_MARKER_THREADINIT
#define LIKWID_MARKER_SWITCH
#define LIKWID_MARKER_REGISTER(regionTag)
#define LIKWID_MARKER_START(regionTag)
#define LIKWID_MARKER_STOP(regionTag)
#define LIKWID_MARKER_CLOSE
#define LIKWID_MARKER_GET(regionTag, nevents, events, time, count)
#define LIKWID_MARKER_RESET(regionTag)
#endif


#define GRAINSIZE_CALC(size) ((size/(8*num_workers)) > 2048 ? 2048 : (size/(8*num_workers)))
#define INDEX(x) x
struct Node{
	uint32_t* val_arr;
	uint32_t target;
};

uint32_t **O, **I, *N;

uint32_t prime_arr [14] = {2003, 4001, 8009, 16001, 32003, 64007, 128021, 256019, 512009, 1024021, 2048003, 4096013, 8192000, 16384001};

int num_workers;

volatile int termination_marker;
//#define CACHE_CALC
#ifdef MICRO_CILK
	#include <cilk/cilk.h>
	#include <cilk/cilk_api.h>
	extern "C" {
    	#include "../cheetah-p4/include/cilk_stats.h"
	}
	#define PARFOR(var_name, start, end, incr, ...) cilk_for(int var_name = start; var_name < end; var_name += incr) {__VA_ARGS__}
#endif

#ifdef MICRO_OMP_STATIC
	#define MICRO_OMP
#endif
#ifdef MICRO_OMP_DYNAMIC
	#define MICRO_OMP
#endif
#ifdef MICRO_OMP_GUIDED
	#define MICRO_OMP
#endif
#ifdef MICRO_OMP
	#include <omp.h>
	#define PARFOR(var_name, start, end, incr, ...) for(int var_name = start; var_name < end; var_name += incr) {__VA_ARGS__}
#endif

#ifdef MICRO_FF
	#include <ff/parallel_for.hpp>
	ff::ParallelFor * pf;
	#ifdef BALANCED
		#define PARFOR(var_name, start, end, incr, ...) pf->parallel_for(start, end, incr, [&](int var_name) {__VA_ARGS__});
	#else
		#define PARFOR(var_name, start, end, incr, ...) pf->parallel_for(start, end, incr, GRAINSIZE_CALC((end - start)/incr) ,[&](int var_name) {__VA_ARGS__});
	#endif
#endif


#ifdef MICRO_SERIAL
	#define PARFOR(var_name, start, end, incr, ...) for(int var_name = start; var_name < end; var_name += incr) {__VA_ARGS__}
#endif

void stall_out() {
	int worker_num = -1;
	#ifdef MICRO_CILK
		worker_num = __cilkrts_get_worker_number();
	#endif
	#ifdef MICRO_OMP
		worker_num = omp_get_thread_num();
	#endif
	printf("Worker %d stalled\n", worker_num);
	while(!termination_marker) {
		asm volatile("");
	}
}

void work_loop(Node* work_array, int loop_length) {
		#ifdef MICRO_OMP_STATIC
			#pragma omp for schedule(static)
		#endif
		#ifdef MICRO_OMP_DYNAMIC
			#pragma omp for schedule(dynamic, GRAINSIZE_CALC(loop_length))
		#endif
		#ifdef MICRO_OMP_GUIDED
			#pragma omp for schedule(guided)
		#endif
		PARFOR(i, 0, loop_length, 1,
			Node* node = &work_array[i];
			int target = node->target;
			int j = 0;
			int target_work_val = node->val_arr[0] + WORK_ITERS;
			while(node->val_arr[j] < target_work_val) {
				node->val_arr[j]++;
				j = (j+13)%target;
				asm volatile ("nop");
			}
			#ifdef CACHE_CALC
				#ifdef MICRO_CILK
					int w = __cilkrts_get_worker_number();
				#elif defined MICRO_OMP
					int w = omp_get_thread_num();
				#else
					int w = 0;
				#endif
				if (N[i] != 1 + w) {
            		I[w][i]++;
        		}
        		N[i] = 1 + w;
        		O[w][i]++;
        	#endif
		);

}

#ifdef CACHE_CALC
int microbench_stat_printing(int printing, int runs, int cores, int iterations) {
    uint64_t lost_iters = 0;
    uint64_t good_iters = 0;
    uint64_t medium_iters = 0;
    uint64_t bad_iters = 0;
    uint64_t cache_flushed_iters = 0;
    int st = 0;
    int extra = iterations % cores;
    uint32_t* endings = (uint32_t*) calloc(cores, sizeof(uint32_t));
    uint32_t* core_iters = (uint32_t*) calloc(cores, sizeof(uint32_t));
    for (int i = 0; i < cores; i++) {
        if (i > 0) endings[i - 1]--;
        st = st + (iterations / cores) + (i < extra ? 1 : 0);
        endings[i] = st;
    }
    if (printing) printf("St.\tEnd\t");
    for (int i = 0; i < cores; i++) {
        if (printing) printf("W%d\t", i);
    }
    if (printing) printf("Lost");
    if (printing) printf("\n");
    int core = 0;
    for (int out = 0; out < iterations; out++) {
        int start = out;
        while (out != endings[core]) {
            int eq = 1;
            for (int i = 0; i < cores; i++) {
                if (O[i][INDEX(out + 1)] != O[i][INDEX(out)]) {
                    eq = 0;
                    break;
                }
            }
            if (!eq) {
                break;
            }
            out++;
        }
        if (printing > 1) printf("%d\t%d\t", start, out);
        uint64_t executed_iters = 0;
        for (int i = 0; i < cores; i++) {
            //Based on how many tiems it's runs on that core
            core_iters[i] += (1 + out - start) * O[i][INDEX(out)];
            executed_iters += O[i][INDEX(out)];
            if (O[i][INDEX(out)] >= runs / 2) {
                if (printing > 1) printf("\x1b[32m%2.2f\t\x1b[0m", (100.0 * O[i][INDEX(out)]) / ((double) runs));
                good_iters += (1 + out - start) * O[i][INDEX(out)];
                cache_flushed_iters += (1 + out - start) * I[i][INDEX(out)];
            } else if (O[i][INDEX(out)] >= runs / cores) {
                if (printing > 1) printf("\x1b[36m%2.2f\t\x1b[0m", (100.0 * O[i][INDEX(out)]) / ((double) runs));
                medium_iters += (1 + out - start) * O[i][INDEX(out)];
                cache_flushed_iters += (1 + out - start) * I[i][INDEX(out)];
            } else if (O[i][INDEX(out)] == 0) {
                if (printing > 1) printf("%d\t", O[i][INDEX(out)]);
            } else {
                if (printing > 1) printf("\x1b[31m%2.2f\t\x1b[0m", (100.0 * O[i][INDEX(out)]) / ((double) runs));
                bad_iters += (1 + out - start) * O[i][INDEX(out)];
                cache_flushed_iters += (1 + out - start) * I[i][INDEX(out)];
            }
            /* //Based on assigned core
            if (i == core) {
                printf("\x1b[32m%d\t\x1b[0m", O[i][out]);
            } else if (i == core-1) {
                printf("\x1b[35m%d\t\x1b[0m", O[i][out]);
            } else if (i == core-2) {
                printf("\x1b[34m%d\t\x1b[0m", O[i][out]);
            } else if (O[i][out] != 0) {
                printf("\x1b[31m%d\t\x1b[0m", O[i][out]);
            } else {
                printf("%d\t", O[i][out]);
            }
            */
        }
        lost_iters += (1 + out - start) * ((runs) - executed_iters);
        if (printing > 1) printf("\x1b[31m%2.2f\t\x1b[0m", 100.0 * ((1 + runs) - executed_iters) / ((double) runs));
        if (printing > 1) printf("\n");
        if (out == endings[core]) {
            core++;
        }

    }
    double all_iterations = ((double) runs) * ((double) iterations);
    if (printing) printf("Total Portion\t");
    for (int i = 0; i < cores; i++) {
        if (printing) printf("%2.2f\t",  (100.0 * core_iters[i]) / all_iterations);
    }
    if (printing) printf("%2.2f\t",  (100.0 * lost_iters) / all_iterations);
    if (printing) printf("\n");

    unsigned long sum_iters = good_iters + medium_iters + bad_iters;
    printf("Good Iters: \x1b[32m%ld(%2.2f)\x1b[0m\n", good_iters, (100.0 * good_iters) / all_iterations);
    printf("Mediocre Iters: \x1b[34m%ld(%2.2f)\x1b[0m\n", medium_iters, (100.0 * medium_iters) / all_iterations);
    printf("Bad Iters: \x1b[31m%ld(%2.2f)\x1b[0m\n", bad_iters, (100.0 * bad_iters) / all_iterations);
    printf("Flushed Iters: \x1b[33m%ld(%2.2f)\x1b[0m\n", cache_flushed_iters, (100.0 * cache_flushed_iters) / all_iterations);
    printf("Repeated Iters: \x1b[33m%ld(%2.2f)\x1b[0m\n", sum_iters - cache_flushed_iters, 100.0 - ((100.0 * cache_flushed_iters) / all_iterations));
    printf("Total Iters: %ld(%2.2f)\n", sum_iters, (100.0 * sum_iters) / all_iterations);
    printf("Lost Iters: %ld(%2.2f)\n", lost_iters, (100.0 * lost_iters) / all_iterations);
    free(endings);
    free(core_iters);
    return 0;
}
#endif


int main(int argc, char *argv[]) 
{
	num_workers = 1;
	int avg_work = AVG_WORK;
	int prime_index = 0;
	uint64_t loop_length = 1000;
	if (argc > 1) loop_length = atol(argv[1]);
	int runs = RUNS;
	if (argc > 2) runs = atoi(argv[2]);
	std::chrono::_V2::system_clock::time_point time_end, time_start;
	std::chrono::microseconds execution_total;
	printf("Running %s\n", argv[0]);
	#ifdef MICRO_CILK
		num_workers = __cilkrts_get_nworkers();
	#endif
	#ifdef MICRO_OMP
		num_workers = omp_get_max_threads();
	#endif
	#ifdef MICRO_FF
		if (const char * nw = std::getenv("FF_NUM_THREADS")) {
        	num_workers = atoi(nw);
    	}
	    pf = new ff::ParallelFor(num_workers, true);
	#endif

	//uint64_t loop_length = prime_arr[prime_index];

	int pattern_array[4] = {0, 1, 2, 3};
	uint64_t bytes_in_array = ( (sizeof(Node) * loop_length) + (loop_length*avg_work*sizeof(uint32_t)));
	printf("%ld bytes in the array\n", bytes_in_array);
	printf("%ld sized nodes (total %ld nodes)\n", sizeof(Node), loop_length);
	Node* work_array = (Node*) pattern_bind_memory_numa(BYTES_TO_PAGES(sizeof(Node) * loop_length), NUM_SOCKETS_FROM_WORKERS(num_workers), pattern_array);
	int num_sockets = NUM_SOCKETS_FROM_WORKERS(num_workers);
	int iters_per_socket = (loop_length/num_sockets);
	double twice_work = (2.0*avg_work)/(1.0*loop_length);

	#ifdef CACHE_CALC
		N = (uint32_t *) pattern_bind_memory_numa(BYTES_TO_PAGES(sizeof(uint32_t) * loop_length ), NUM_SOCKETS_FROM_WORKERS(num_workers), pattern_array);
		O = (uint32_t **) calloc(num_workers, sizeof(uint32_t*));
		I = (uint32_t **) calloc(num_workers, sizeof(uint32_t*));
		for(int i = 0; i < num_workers; i++) {
			O[i] = (uint32_t *) pattern_bind_memory_numa(BYTES_TO_PAGES(sizeof(uint32_t) * loop_length), 1, &(pattern_array[i / WORKERS_PER_SOCKET]));
    		I[i] = (uint32_t *) pattern_bind_memory_numa(BYTES_TO_PAGES(sizeof(uint32_t) * loop_length), 1, &(pattern_array[i / WORKERS_PER_SOCKET]));
    		for (uint32_t j = 0; j < (loop_length); j++) {
    		    O[i][j] = 0;
    		    I[i][j] = 0;
    		}
		}
		uint64_t bytes_in_N = (sizeof(uint32_t)*loop_length);
		uint64_t bytes_in_O = (sizeof(uint32_t*)*num_workers) + (sizeof(uint32_t)*loop_length);
		uint64_t bytes_in_I = (sizeof(uint32_t*)*num_workers) + (sizeof(uint32_t)*loop_length);
		printf("%ld bytes in the cache calc computation\n", bytes_in_N + bytes_in_O + bytes_in_I);
		printf("%ld bytes total\n", bytes_in_N + bytes_in_O + bytes_in_I + bytes_in_array);
	#endif
	LIKWID_MARKER_STOP("INTERVAL_WORK");
	LIKWID_MARKER_RESET("INTERVAL_WORK");
	LIKWID_MARKER_START("INTERVAL_WORK");
	for(int i = 0; i < loop_length; i++) {
			#ifdef BALANCED
				work_array[i].target = avg_work;
				work_array[i].val_arr = (uint32_t *) pattern_bind_memory_numa(BYTES_TO_PAGES(sizeof(uint32_t)*work_array[i].target), 1, &pattern_array[i/iters_per_socket]);;

			#else
				work_array[i].target = 1 + twice_work*i;
				work_array[i].val_arr = (uint32_t *) pattern_bind_memory_numa(BYTES_TO_PAGES(sizeof(uint32_t)*work_array[i].target), 1, &pattern_array[i/iters_per_socket]);;
			#endif
			for (int j = 0; j < work_array[i].target; j++) {
				work_array[i].val_arr[j] = 0;
			}
	}

	// for(int i = 0; i < num_segments; i++) {
	// 	for (int j = 0; j < loop_length; j++) {
	// 		if (work_array[(i*loop_length) + j].next_node != &work_array[(i*loop_length) + ((j*4)%loop_length)]) {
	// 			printf("%d iter in %d segment didn't get set properly\n", j, i);
	// 		}
	// 	}
	// }

	#ifdef MICRO_OMP
		#pragma omp parallel
		{
	#endif
	//run an initial run of the loop to get it into cache
	work_loop(work_array, loop_length);

	#ifdef CACHE_CALC
		//reset all counters after initial run
		for(int i = 0; i < num_workers; i++) {
			for (uint32_t j = 0; j < (loop_length); j++) {
			    O[i][j] = 0;
			    I[i][j] = 0;
			}
		}
	#endif
	#ifdef MICRO_CILK
		sched_timing_handle_t cilk_begin = cilk_init_sched_timing_handle();
    	sched_timing_handle_t cilk_end = cilk_init_sched_timing_handle();
    	cilk_get_sched_timing(cilk_begin);
    #endif

	#ifdef STALLED_WORKERS
		if (num_workers <= STALLED_WORKERS) {
			printf("Number of workers (%d) cannot be less than number of workers to stall (%d) \n", num_workers, STALLED_WORKERS);
		}
		termination_marker = 0;
		#ifdef MICRO_CILK
		for(int i = 0; i < STALLED_WORKERS; i++) {
			cilk_spawn stall_out();
		}
		#endif
		#ifdef MICRO_OMP
		#pragma omp single nowait
		{
			#pragma omp parallel num_threads(STALLED_WORKERS)
			{
				#pragma omp for schedule(static) nowait
				for(int i = 0; i < STALLED_WORKERS; i++) {
					stall_out();
				}
			}
		}
		#pragma omp single nowait
		{
			#pragma omp parallel
			{
		#endif
	#endif

	#ifdef MICRO_OMP
		#pragma omp single
		{
	#endif
	time_start = std::chrono::high_resolution_clock::now();
	#ifdef MICRO_OMP
		}
	#endif

	for (int run = 1; run < runs; run++) {
		work_loop(work_array, loop_length);
	}



	#ifdef STALLED_WORKERS
		#ifdef MICRO_OMP
			#pragma omp single
			{
		#endif
		termination_marker = 1;
		#ifdef MICRO_OMP
				}
		#endif
	#endif

	#ifdef MICRO_OMP
		#pragma omp single
		{
	#endif


	time_end = std::chrono::high_resolution_clock::now();
	#ifdef MICRO_OMP
		}
		#ifdef STALLED_WORKERS
				} 
			}
		#endif
	#endif
	
	execution_total = std::chrono::duration_cast<std::chrono::microseconds>(time_end - time_start);

	#ifdef MICRO_OMP
		}
	#endif

	#ifdef MICRO_CILK
    	cilk_get_sched_timing(cilk_end);
    	cilk_print_sched_timing_diff(cilk_begin, cilk_end);
    	cilk_deinit_sched_timing_handle(cilk_begin);
	    cilk_deinit_sched_timing_handle(cilk_end); 
    #endif

	//Test for correctness
	int errors_exist = 0;
	for(int i = 0; i < loop_length; i++) {
		if (work_array[i].val_arr[0] != (runs*WORK_ITERS)) {
			printf("Error in %dth bucket.  Value %d should be %d.\n", i, work_array[i].val_arr[0], (runs*WORK_ITERS));
			errors_exist = 1;
		}
	}
	#ifdef CACHE_CALC
		microbench_stat_printing(3, runs-1, num_workers, loop_length);
	#endif
	if (errors_exist) 
		return 1;
	printf("Total runtime in microseconds: %lu \n", execution_total.count());
	return 0;
}