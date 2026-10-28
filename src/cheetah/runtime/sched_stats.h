#ifndef __SCHED_STATS_HEADER__
#define __SCHED_STATS_HEADER__

#include "rts-config.h"
#include <stdint.h>
//#include <likwid-marker.h>
#define LIKWID_MARKER_START(x)
#define LIKWID_MARKER_STOP(x)
typedef struct __cilkrts_worker __cilkrts_worker;

#define SCHED_STATS CILK_STATS
#define SAVED_BACKTRACKS 10000
#define SAVED_WORK_STAMPS 100000


enum output_format {
    OUTPUT_FORMAT_HUMAN_READABLE = 0,
    OUTPUT_FORMAT_CSV
};

enum timing_type {
    INTERVAL_WORK = 0, // work time
    INTERVAL_SCHED,    // scheduling time
    INTERVAL_IDLE,     // idle time
    INTERVAL_SLEEP,    // sleeping time in work-stealing loop
    INTERVAL_SLEEP_UNCILK,  // sleeping time outside of work-stealing loop
    INTERVAL_BACKTRACKING, //time spent on backtracking
    INTERVAL_BACKTRACKING_SUCCESS,  // time spent on backtracking successes
    INTERVAL_BACKTRACKING_FAILURE,  // time spent on backtracking failures
    INTERVAL_STEALING, //time spent successfully stealing in the steal function
    INTERVAL_STEALING_CACHED, //time spent successfully stealing from a cached frame in the steal function
    INTERVAL_TEMP,  // variable timer to be moved around for debugging
    INTERVAL_INTERUPTED,  // time spent in the interupt handler
    INTERVAL_FAILED_DEQUE_LOCK, //time spent failed due to deque lock failures
    INTERVAL_FAILED_DEQUE_EMPTY, //time spent failed due to empty deque failures
    INTERVAL_FAILED_BACKTRACK_EMPTY, //time spent failed due to empty backtrack failures
    INTERVAL_FAILED_BACKTRACK_NULL_RIP, //time spent failed due to empty backtrack failures
    INTERVAL_FAILED_BACKTRACK_POP_STATE, //time spent failed due to empty backtrack failures
    INTERVAL_FAILED_BACKTRACK_OUTSIDE_FIBER, //time spent failed due to empty backtrack failures
    INTERVAL_FAILED_BACKTRACK_MISMATCHED_FRAME, //time spent failed due to empty backtrack failures
    INTERVAL_FAILED_CLOSURE_LOCK, //time spent failed due to closure lock failures
    INTERVAL_FAILED_GREEDY_HEURISTIC, //time spent failed due to the greedy heuristic
    INTERVAL_FAILED_TRUE_HEURISTIC, //time spent failed due to the true heuristic
    INTERVAL_FAILED_FRAME_TYPE, //time spent failed due to the wrong frame type
    INTERVAL_FAILED_CACHELESS_BUFFER, //time spent failed due to not being a cached frame
    INTERVAL_SIGNAL_HANDLER,
    NUMBER_OF_STATS    // must be the very last entry
};

struct sched_stats {
    uint64_t time[NUMBER_OF_STATS];  // Total time measured for all stats
    uint64_t count[NUMBER_OF_STATS];
    uint64_t begin[NUMBER_OF_STATS]; // Begin time of current measurement
    uint64_t end[NUMBER_OF_STATS];   // End time of current measurement
    uint64_t stats_init_time;
    uint64_t work_begin[SAVED_WORK_STAMPS];
    uint64_t work_end[SAVED_WORK_STAMPS];
    uint64_t interupt_begin[SAVED_WORK_STAMPS];
    uint64_t interupt_end[SAVED_WORK_STAMPS];

    
    uint64_t backtrack_time[SAVED_BACKTRACKS];  // A measurement of the first SAVED_BACKTRACKS number of backtracks
    uint64_t backtrack_sframes[SAVED_BACKTRACKS];  // a count of how many stack frames were incremented in those backtracks
    uint64_t backtrack_cframes[SAVED_BACKTRACKS];  // a count of how many cilk frames were incremented in those backtracks
    uint64_t backtrack_steal_time[SAVED_BACKTRACKS];  // a count of how many cilk frames were incremented in those backtracks
    uint64_t backtrack_success[SAVED_BACKTRACKS];  // a count of how many cilk frames were incremented in those backtracks
    uint64_t backtrack_steal_fails[SAVED_BACKTRACKS];  // a count of how many cilk frames were incremented in those backtracks

    uint64_t frames_backtracked;
    uint64_t successful_steals;
    uint64_t failed_steals;
    uint64_t steals;
    uint64_t repos;
    uint64_t slow_steals;

#if CILK_BACKTRACK_CACHE_SIZE
    uint64_t cache_steals;
    uint64_t cache_insertions;
#endif
};

struct global_sched_stats {
    // Stats for the boss thread
    uint64_t boss_waiting;
    uint64_t boss_wait_count;
    uint64_t boss_begin;
    uint64_t exit_time;
    uint64_t boss_end;
    uint64_t frames_backtracked;
    uint64_t steals;
    uint64_t repos;
    uint64_t successful_steals;
    uint64_t failed_steals;
    uint64_t slow_steals;
#if CILK_BACKTRACK_CACHE_SIZE
    uint64_t cache_steals;
    uint64_t cache_insertions;
#endif
    double time[NUMBER_OF_STATS]; // Total time measured for all stats
    uint64_t count[NUMBER_OF_STATS];
};

#if SCHED_STATS
CHEETAH_INTERNAL
void cilk_global_sched_stats_init(struct global_sched_stats *s);
CHEETAH_INTERNAL
void cilk_sched_stats_init(struct sched_stats *s);
CHEETAH_INTERNAL
void cilk_start_timing(__cilkrts_worker *w, enum timing_type t);
CHEETAH_INTERNAL
void cilk_stop_timing(__cilkrts_worker *w, enum timing_type t);
CHEETAH_INTERNAL
void cilk_switch_timing(__cilkrts_worker *w, enum timing_type t1,
                        enum timing_type t2);
CHEETAH_INTERNAL
void cilk_swap_stop_timing(__cilkrts_worker *w, enum timing_type t1,
                        enum timing_type t2);
CHEETAH_INTERNAL
void cilk_drop_timing(__cilkrts_worker *w, enum timing_type t);
CHEETAH_INTERNAL
void cilk_boss_start_timing(struct global_state *g);
CHEETAH_INTERNAL
void cilk_boss_stop_timing(struct global_state *g);
CHEETAH_INTERNAL
void cilk_exit_worker_timing(struct global_state *g);
CHEETAH_INTERNAL
void cilk_sched_stats_print(struct global_state *g);
// void cilk_reset_timing(__cilkrts_worker *w, enum timing_type t);
// FIXME: should have a header file that's user-code interfacing
// void __cilkrts_reset_timing(); // user-code facing

CHEETAH_INTERNAL
void cilk_backtrack_stop_timing(__cilkrts_worker *w, uint64_t success);
CHEETAH_INTERNAL
void cilk_backtrack_increment_cilk(__cilkrts_worker *w);
CHEETAH_INTERNAL
void cilk_backtrack_increment_frame(__cilkrts_worker *w);
CHEETAH_INTERNAL
void cilk_backtrack_increment_slow_spawn(__cilkrts_worker *w);

CHEETAH_INTERNAL
void cilk_increment_successful_steals(__cilkrts_worker *w);
CHEETAH_INTERNAL
void cilk_increment_failed_steals(__cilkrts_worker *w);
#if CILK_BACKTRACK_CACHE_SIZE
CHEETAH_INTERNAL
void cilk_increment_cache_insertions(__cilkrts_worker *w);
CHEETAH_INTERNAL
void cilk_increment_cache_steals(__cilkrts_worker *w);
#endif


#define WHEN_SCHED_STATS(ex) ex
#define CILK_START_TIMING(w, t) cilk_start_timing(w, t); LIKWID_MARKER_START(#t)
#define CILK_STOP_TIMING(w, t) cilk_stop_timing(w, t); LIKWID_MARKER_STOP(#t)
#define CILK_BACKTRACK_STOP_TIMING(w, s) cilk_backtrack_stop_timing(w, s); LIKWID_MARKER_STOP("INTERVAL_BACKTRACKING")
#define CILK_BACKTRACK_INCR_CILK(w) cilk_backtrack_increment_cilk(w)
#define CILK_BACKTRACK_INCR_FRAME(w) cilk_backtrack_increment_frame(w)
#define CILK_BACKTRACK_INCR_SLOW_SPAWN(w) cilk_backtrack_increment_slow_spawn(w)
#define CILK_BACKTRACK_INCR_CACHE_STEALS(w) cilk_increment_cache_steals(w)
#define CILK_BACKTRACK_INCR_CACHE_INSERT(w) cilk_increment_cache_insertions(w)
#define CILK_INCR_BACKTRACK_SUCCESS(w) cilk_increment_successful_steals(w)
#define CILK_INCR_HEURISTIC_FAILED(w) cilk_increment_failed_heuristic_steals(w)
#define CILK_INCR_BACKTRACK_FAILED(w) cilk_increment_failed_steals(w)
#define CILK_SWITCH_TIMING(w, t1, t2) cilk_switch_timing(w, t1, t2); LIKWID_MARKER_STOP(#t1); LIKWID_MARKER_START(#t2)
#define CILK_SWAP_STOP_TIMING(w, t1, t2) cilk_swap_stop_timing(w, t1, t2); LIKWID_MARKER_STOP(#t1)
#define CILK_DROP_TIMING(w, t) cilk_drop_timing(w, t); LIKWID_MARKER_STOP(#t)
#define CILK_BOSS_START_TIMING(g) cilk_boss_start_timing(g)
#define CILK_BOSS_STOP_TIMING(g) cilk_boss_stop_timing(g)
#define CILK_EXIT_WORKER_TIMING(g) cilk_exit_worker_timing(g)

#else
#define cilk_global_sched_stats_init(s)
#define cilk_sched_stats_init(s)
#define cilk_sched_stats_print(g)
// #define cilk_reset_timing()

#define WHEN_SCHED_STATS(ex)
#define CILK_START_TIMING(w, t) LIKWID_MARKER_START(#t)
#define CILK_STOP_TIMING(w, t) LIKWID_MARKER_STOP(#t)
#define CILK_BACKTRACK_STOP_TIMING(w, s) LIKWID_MARKER_STOP("INTERVAL_BACKTRACKING")
#define CILK_BACKTRACK_INCR_CILK(w) 
#define CILK_BACKTRACK_INCR_FRAME(w)
#define CILK_BACKTRACK_INCR_SLOW_SPAWN(w)
#define CILK_BACKTRACK_INCR_CACHE_STEALS(w) 
#define CILK_BACKTRACK_INCR_CACHE_INSERT(w) 
#define CILK_INCR_BACKTRACK_SUCCESS(w)
#define CILK_INCR_BACKTRACK_FAILED(w)
#define CILK_INCR_HEURISTIC_FAILED(w)
#define CILK_SWITCH_TIMING(w, t1, t2) LIKWID_MARKER_STOP(#t1); LIKWID_MARKER_START(#t2)
#define CILK_SWAP_STOP_TIMING(w, t1, t2) LIKWID_MARKER_STOP(#t1)
#define CILK_DROP_TIMING(w, t)
#define CILK_BOSS_START_TIMING(g)
#define CILK_BOSS_STOP_TIMING(g)
#define CILK_EXIT_WORKER_TIMING(g)
#endif // SCHED_STATS

#endif // __SCHED_STATS_HEADER__
