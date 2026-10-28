#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#include "cilk-internal.h"
#include "debug.h"
#include "global.h"
#include "internal-malloc-impl.h"
#include "local.h"
#include "sched_stats.h"
#include "spawn-table.h"

#if SCHED_STATS
static const char *enum_to_str(enum timing_type t) {
    switch (t) {
    case INTERVAL_WORK:
        return "working";
    case INTERVAL_SCHED:
        return "scheduling";
    case INTERVAL_IDLE:
        return "idling";
    case INTERVAL_SLEEP:
        return "sleep (sched)";
    case INTERVAL_SLEEP_UNCILK:
        return "sleep (uncilk)";
    case INTERVAL_BACKTRACKING:
        return "backtrack";
    case INTERVAL_BACKTRACKING_SUCCESS:
        return "backtrack_s";
    case INTERVAL_BACKTRACKING_FAILURE:
        return "backtrack_f";
    case INTERVAL_STEALING:
        return "stealing";
    case INTERVAL_STEALING_CACHED:
        return "steal_cache";
    case INTERVAL_FAILED_DEQUE_LOCK:
        return "f_deque_lck";
    case INTERVAL_FAILED_CLOSURE_LOCK:
        return "f_clsre_lck";
    case INTERVAL_FAILED_GREEDY_HEURISTIC:
        return "f_grdy_heur";
    case INTERVAL_FAILED_BACKTRACK_EMPTY:
        return "f_bcktk_emp";
    case INTERVAL_FAILED_BACKTRACK_NULL_RIP:
        return "f_bcktk_nrp";
    case INTERVAL_FAILED_BACKTRACK_POP_STATE:
        return "f_bcktk_pst";
    case INTERVAL_FAILED_BACKTRACK_OUTSIDE_FIBER:
        return "f_bcktk_ofb";
    case INTERVAL_FAILED_BACKTRACK_MISMATCHED_FRAME:
        return "f_bcktk_mmf";
    case INTERVAL_FAILED_DEQUE_EMPTY:
        return "f_deque_emp";
    case INTERVAL_FAILED_TRUE_HEURISTIC:
        return "f_true_heur";
    case INTERVAL_FAILED_FRAME_TYPE:
        return "f_frme_type";
    case INTERVAL_FAILED_CACHELESS_BUFFER:
        return "f_nocache_b";
    case INTERVAL_INTERUPTED:
        return "interupt";
    case INTERVAL_SIGNAL_HANDLER:
        return "signal";
    case INTERVAL_TEMP:
        return "temp";
    default:
        return "unknown";
    }
}

__attribute__((unused)) static inline double cycles_to_micro_sec(uint64_t cycle) {
    return (double)cycle / ((double)PROC_SPEED_IN_GHZ * 1000.0);
}

__attribute__((unused)) static inline double
micro_sec_to_sec(double micro_sec) {
    return micro_sec / 1000000.0;
}

__attribute__((unused)) static inline uint64_t begin_cycle_count() {
    unsigned int low, high;
    __asm__ volatile("cpuid\n\t"
                     "rdtsc\n\t"
                     "mov %%edx, %0\n\t"
                     "mov %%eax, %1\n\t"
                     : "=r"(high), "=r"(low)::"%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t)high << 32) | low;
}

__attribute__((unused)) static inline uint64_t end_cycle_count() {
    unsigned int low, high;
    __asm__ volatile("rdtscp\n\t"
                     "mov %%edx, %0\n\t"
                     "mov %%eax, %1\n\t"
                     "cpuid\n\t"
                     : "=r"(high), "=r"(low)::"%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t)high << 32) | low;
}

static inline double nsec_to_sec(uint64_t nsec) { return nsec / 1.0e9; }

static inline uint64_t begin_time() {
    struct timespec res;
    clock_gettime(CLOCK_MONOTONIC, &res);
    return (res.tv_sec * 1e9) + (res.tv_nsec);
}

static inline uint64_t end_time() {
    struct timespec res;
    clock_gettime(CLOCK_MONOTONIC, &res);
    return (res.tv_sec * 1e9) + (res.tv_nsec);
}

int nr_events = 32;

void cilk_global_sched_stats_init(struct global_sched_stats *s) {
    s->boss_waiting = 0;
    s->boss_wait_count = 0;
    s->boss_begin = 0;
    s->boss_end = 0;
    s->exit_time = 0;
    s->steals = 0;
    s->repos = 0;
#if CILK_BACKTRACK_CACHE_SIZE
    s->cache_steals = 0;
    s->cache_insertions = 0;
#endif
    for (int i = 0; i < NUMBER_OF_STATS; ++i) {
        s->time[i] = 0.0;
        s->count[i] = 0;
    }
}

void cilk_sched_stats_init(struct sched_stats *s) {
    s->stats_init_time = begin_time();
    for (int i = 0; i < NUMBER_OF_STATS; ++i) {
        s->begin[i] = 0;
        s->end[i] = 0;
        s->time[i] = 0;
        s->count[i] = 0;
    }
    for (int i = 0; i < SAVED_BACKTRACKS; ++i) {
        s->backtrack_time[i] = 0;
        s->backtrack_cframes[i] = 0;
        s->backtrack_sframes[i] = 0;
        s->backtrack_steal_time[i] = 0;
    }
    s->frames_backtracked = 0;
    s->steals = 0;
    s->repos = 0;
    s->successful_steals = 0;
    s->failed_steals = 0;
#if CILK_BACKTRACK_CACHE_SIZE
    s->cache_steals = 0;
    s->cache_insertions = 0;
#endif
}

void cilk_start_timing(__cilkrts_worker *w, enum timing_type t) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        CILK_ASSERT(w, s->begin[t] == 0);
        s->end[t] = 0;
        s->begin[t] = begin_time();
        if ((t == INTERVAL_WORK) && (s->count[INTERVAL_WORK] < SAVED_WORK_STAMPS)) {
            s->work_begin[s->count[INTERVAL_WORK]] = s->begin[t] - s->stats_init_time;
        }
        if ((t == INTERVAL_INTERUPTED) && (s->count[INTERVAL_INTERUPTED] < SAVED_WORK_STAMPS)) {
            s->interupt_begin[s->count[INTERVAL_INTERUPTED]] = s->begin[t] - s->stats_init_time;
        }
    }
}

void cilk_stop_timing(__cilkrts_worker *w, enum timing_type t) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        CILK_ASSERT(w, s->end[t] == 0);
        s->end[t] = end_time();
        CILK_ASSERT(w, s->end[t] >= s->begin[t]);
        s->time[t] += (s->end[t] - s->begin[t]);
        if ((t == INTERVAL_WORK) && (s->count[INTERVAL_WORK] < SAVED_WORK_STAMPS)) {
            s->work_end[s->count[INTERVAL_WORK]] = s->end[t] - s->stats_init_time;
        }
        if ((t == INTERVAL_INTERUPTED) && (s->count[INTERVAL_INTERUPTED] < SAVED_WORK_STAMPS)) {
            s->interupt_end[s->count[INTERVAL_INTERUPTED]] = s->end[t] - s->stats_init_time;
        }
        s->count[t]++;
        s->begin[t] = 0;
    }
}

void cilk_backtrack_stop_timing(__cilkrts_worker *w, uint64_t success) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        CILK_ASSERT(w, s->end[INTERVAL_BACKTRACKING] == 0);
        s->end[INTERVAL_BACKTRACKING] = end_time();
        CILK_ASSERT(w, s->end[INTERVAL_BACKTRACKING] >= s->begin[INTERVAL_BACKTRACKING]);
        s->time[INTERVAL_BACKTRACKING] += (s->end[INTERVAL_BACKTRACKING] - s->begin[INTERVAL_BACKTRACKING]);
        if (s->count[INTERVAL_BACKTRACKING] < SAVED_BACKTRACKS)
        {
            s->backtrack_steal_time[s->count[INTERVAL_BACKTRACKING]] = (s->begin[INTERVAL_BACKTRACKING] - s->stats_init_time);
            s->backtrack_time[s->count[INTERVAL_BACKTRACKING]] = (s->end[INTERVAL_BACKTRACKING] - s->begin[INTERVAL_BACKTRACKING]);
            s->backtrack_success[s->count[INTERVAL_BACKTRACKING]] = success;
        }
        s->count[INTERVAL_BACKTRACKING]++;
        s->begin[INTERVAL_BACKTRACKING] = 0;
    }
}

void cilk_backtrack_increment_cilk(__cilkrts_worker *w) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        if (s->count[INTERVAL_BACKTRACKING] < SAVED_BACKTRACKS)
            s->backtrack_cframes[s->count[INTERVAL_BACKTRACKING]]++;
    }
}

void cilk_backtrack_increment_frame(__cilkrts_worker *w) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        s->frames_backtracked++;
        if (s->count[INTERVAL_BACKTRACKING] < SAVED_BACKTRACKS)
            s->backtrack_sframes[s->count[INTERVAL_BACKTRACKING]]++;
    }
}

void cilk_backtrack_increment_slow_spawn(__cilkrts_worker *w) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        s->slow_steals++;
    }
}


void cilk_increment_successful_steals(__cilkrts_worker *w) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        s->successful_steals++;
    }
}

void cilk_increment_failed_steals(__cilkrts_worker *w) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        s->failed_steals++;
        if (s->count[INTERVAL_BACKTRACKING] < SAVED_BACKTRACKS)
            s->backtrack_steal_fails[s->count[INTERVAL_BACKTRACKING]]++;
    }
}
#if CILK_BACKTRACK_CACHE_SIZE

void cilk_increment_cache_insertions(__cilkrts_worker *w) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        s->cache_insertions++;
    }
}

void cilk_increment_cache_steals(__cilkrts_worker *w) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        s->cache_steals++;
    }
}
#endif


void cilk_switch_timing(__cilkrts_worker *w, enum timing_type t1,
                        enum timing_type t2) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        // Stop timer t1
        CILK_ASSERT(w, s->end[t1] == 0);
        s->end[t1] = end_time();
        CILK_ASSERT(w, s->end[t1] >= s->begin[t1]);
        s->time[t1] += (s->end[t1] - s->begin[t1]);
        if ((t1 == INTERVAL_WORK) && (s->count[INTERVAL_WORK] < SAVED_WORK_STAMPS)) {
            s->work_end[s->count[INTERVAL_WORK]] = s->end[t1] - s->stats_init_time;
        }
        if ((t1 == INTERVAL_INTERUPTED) && (s->count[INTERVAL_INTERUPTED] < SAVED_WORK_STAMPS)) {
            s->interupt_end[s->count[INTERVAL_INTERUPTED]] = s->end[t1] - s->stats_init_time;
        }
        s->count[t1]++;
        s->begin[t1] = 0;

        // Start timer t2 where t1 left off
        CILK_ASSERT(w, s->begin[t2] == 0);
        s->end[t2] = 0;
        s->begin[t2] = s->end[t1];
        if ((t2 == INTERVAL_WORK) && (s->count[INTERVAL_WORK] < SAVED_WORK_STAMPS)) {
            s->work_begin[s->count[INTERVAL_WORK]] = s->begin[t2] - s->stats_init_time;
        }
        if ((t2 == INTERVAL_INTERUPTED) && (s->count[INTERVAL_INTERUPTED] < SAVED_WORK_STAMPS)) {
            s->interupt_begin[s->count[INTERVAL_INTERUPTED]] = s->begin[t2] - s->stats_init_time;
        }
    }
}

void cilk_swap_timing(__cilkrts_worker *w, enum timing_type t1,
                        enum timing_type t2) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        // Make sure timer 1 is running
        CILK_ASSERT(w, s->begin[t1] != 0);
        CILK_ASSERT(w, s->end[t1] == 0);

        // Make sure timer 2 wasn't running
        CILK_ASSERT(w, s->begin[t2] == 0);
        // Start timer t2 where t1 started
        s->end[t2] = 0;
        s->begin[t2] = s->begin[t1];
        s->begin[t1] = 0;
    }
}

void cilk_swap_stop_timing(__cilkrts_worker *w, enum timing_type t1,
                        enum timing_type t2) {
    if (w) {
        cilk_swap_timing(w, t1, t2);
        cilk_stop_timing(w, t2);
    }
}



void cilk_drop_timing(__cilkrts_worker *w, enum timing_type t) {
    if (w) {
        struct sched_stats *s = &(w->l->stats);
        CILK_ASSERT(w, s->begin[t] != 0);
        s->begin[t] = 0;
    }
}

void cilk_boss_start_timing(struct global_state *g) {
    struct global_sched_stats *s = &(g->stats);
    CILK_ASSERT_G(s->boss_begin == 0);
    s->boss_begin = begin_time();
    s->boss_end = 0;
}

void cilk_boss_stop_timing(struct global_state *g) {
    struct global_sched_stats *s = &(g->stats);
    CILK_ASSERT_G(s->boss_end == 0);
    s->boss_end = end_time();
    CILK_ASSERT_G(s->boss_end >= s->boss_begin);
    CILK_ASSERT_G(s->boss_end >= s->exit_time);
    uint64_t last = s->exit_time > s->boss_begin ? s->exit_time : s->boss_begin;
    s->boss_waiting += (s->boss_end - last);
    s->boss_wait_count++;
    s->boss_begin = 0;
    s->exit_time = 0;
}

void cilk_exit_worker_timing(struct global_state *g) {
    struct global_sched_stats *s = &(g->stats);
    CILK_ASSERT_G(s->exit_time == 0);
    s->exit_time = begin_time();
}

static void sched_stats_reset_worker(__cilkrts_worker *w,
                                     void *data __attribute__((unused))) {
    for (int t = 0; t < NUMBER_OF_STATS; t++) {
        w->l->stats.time[t] = 0;
        w->l->stats.count[t] = 0;
    }
    w->l->stats.steals = 0;
    w->l->stats.repos = 0;
}

#define CSV_COL_DESC "%s,"
#define CSV_HDR_DESC "%s,%s %s,"
#define CSV_WORKER_HDR_DESC "%u,"
#define CSV_FIELD_DESC "%.6f,%ld,"
#define CSV_COUNT_HDR_DESC "%s,"
#define CSV_COUNT_DESC "%ld,"

#define COL_DESC "%15s"
#define HDR_DESC "%18s %8s"
#define WORKER_HDR_DESC "%10s %3u:"
#define FIELD_DESC "%18.6f %8ld"
#define COUNT_HDR_DESC "%8s"
#define COUNT_DESC "%8ld"

static void cilk_sched_stats_print_worker_backtrack_worksections_csv(__cilkrts_worker *w, void *data) {
    FILE *fp = (FILE *)data;
    for (int i = 0; i < w->l->stats.count[INTERVAL_WORK]; ++i) {
        if (i >= SAVED_WORK_STAMPS) {
            break;
        }
        double work_begin_time = nsec_to_sec((w->l->stats.work_begin[i]));
        double work_end_time = nsec_to_sec((w->l->stats.work_end[i]));
        fprintf(fp, "%u,%.8f,%.8f,working\n", w->self, work_begin_time, work_end_time);
        // fprintf(fp, "%u,%lu,%lu\n", w->self, w->l->stats.work_begin[i], w->l->stats.work_end[i]);
    }
    for (int i = 0; i < w->l->stats.count[INTERVAL_INTERUPTED]; ++i) {
        if (i >= SAVED_WORK_STAMPS) {
            break;
        }
        double interupt_begin_time = nsec_to_sec((w->l->stats.interupt_begin[i]));
        double interupt_end_time = nsec_to_sec((w->l->stats.interupt_end[i]));
        fprintf(fp, "%u,%.8f,%.8f,interupted\n", w->self, interupt_begin_time, interupt_end_time);
        // fprintf(fp, "%u,%lu,%lu\n", w->self, w->l->stats.work_begin[i], w->l->stats.work_end[i]);
    }
}


static void cilk_sched_stats_print_worker_backtrack_csv(__cilkrts_worker *w, void *data) {
    FILE *fp = (FILE *)data;
    for (int i = 0; i < w->l->stats.count[INTERVAL_BACKTRACKING]; ++i) {
        double backtrack_time = nsec_to_sec(w->l->stats.backtrack_time[i]);
        double backstrack_steal_time = nsec_to_sec(w->l->stats.backtrack_steal_time[i]);
        fprintf(fp, "%u,%.8f,%.8f,%lu,%lu,%lu,%lu\n", w->self, backtrack_time, backstrack_steal_time, w->l->stats.backtrack_cframes[i], w->l->stats.backtrack_sframes[i], w->l->stats.backtrack_success[i], w->l->stats.backtrack_steal_fails[i]);
    }
}

void cilk_sched_stats_print_backtrack_csv(struct global_state *g) {
    fprintf(stdout, "\n%s\n", "backtrack_values");
    fprintf(stdout, "%s,%s,%s,%s,%s,%s,%s\n", "worker", "time", "start time", "cilk_frames", "stack_frames", "success", "failures");
    for_each_worker(g, &cilk_sched_stats_print_worker_backtrack_csv, stdout);

    fprintf(stdout, "\n%s\n", "work_sections");
    fprintf(stdout, "%s,%s,%s\n", "worker", "start time", "end time");
    for_each_worker(g, &cilk_sched_stats_print_worker_backtrack_worksections_csv, stdout);
}

static void sched_stats_print_worker_csv(__cilkrts_worker *w, void *data) {
    FILE *fp = (FILE *)data;
    fprintf(fp, CSV_WORKER_HDR_DESC, w->self);
    for (int t = 0; t < NUMBER_OF_STATS; t++) {
        double tmp = nsec_to_sec(w->l->stats.time[t]);
        w->g->stats.time[t] += (double)tmp;
        uint64_t tmp_count = w->l->stats.count[t];
        w->g->stats.count[t] += tmp_count;
        fprintf(fp, CSV_FIELD_DESC, tmp, tmp_count);
    }
    w->g->stats.steals += w->l->stats.steals;
    w->g->stats.successful_steals += w->l->stats.successful_steals;
    w->g->stats.failed_steals += w->l->stats.failed_steals;
    w->g->stats.slow_steals += w->l->stats.slow_steals;

    fprintf(stdout, CSV_COUNT_DESC, w->l->stats.steals);
    fprintf(stdout, CSV_COUNT_DESC, w->l->stats.successful_steals);
    fprintf(stdout, CSV_COUNT_DESC, w->l->stats.failed_steals);
    fprintf(stdout, CSV_COUNT_DESC, w->l->stats.slow_steals);
#if CILK_BACKTRACK_CACHE_SIZE
    fprintf(stdout, CSV_COUNT_DESC, w->l->stats.cache_steals);
    fprintf(stdout, CSV_COUNT_DESC, w->l->stats.cache_insertions);
#endif
    fprintf(fp, "\n");
}

void cilk_sched_stats_print_csv(struct global_state *g) {
    for (int t = 0; t < NUMBER_OF_STATS; t++) {
        g->stats.time[t] = 0.0;
        g->stats.count[t] = 0;
    }
    g->stats.steals = 0;
    g->stats.successful_steals = 0;
    g->stats.failed_steals = 0;
    g->stats.slow_steals = 0;

    fprintf(stdout, "\nCSV_STATS_BELOW\n");
    fprintf(stdout, CSV_COL_DESC, "worker");
    for (int t = 0; t < NUMBER_OF_STATS; t++) {
        fprintf(stdout, CSV_HDR_DESC, enum_to_str(t), enum_to_str(t), "count");
    }
    fprintf(stdout, CSV_COUNT_HDR_DESC, "steals");
    fprintf(stdout, CSV_COUNT_HDR_DESC, "success");
    fprintf(stdout, CSV_COUNT_HDR_DESC, "failure");
    fprintf(stdout, COUNT_HDR_DESC, "slow");
#if CILK_BACKTRACK_CACHE_SIZE
    fprintf(stdout, CSV_COUNT_HDR_DESC, "cache_steal");
    fprintf(stdout, CSV_COUNT_HDR_DESC, "cache_in");
#endif
    fprintf(stdout, "\n");

    for_each_worker(g, &sched_stats_print_worker_csv, stdout);

    fprintf(stdout, CSV_COL_DESC, "Total:");
    for (int t = 0; t < NUMBER_OF_STATS; t++) {
        fprintf(stdout, CSV_FIELD_DESC, g->stats.time[t], g->stats.count[t]);
    }
    fprintf(stdout, CSV_COUNT_DESC, g->stats.steals);
    fprintf(stdout, CSV_COUNT_DESC, g->stats.successful_steals);
    fprintf(stdout, CSV_COUNT_DESC, g->stats.failed_steals);
    fprintf(stdout, CSV_COUNT_DESC, g->stats.slow_steals);
#if CILK_BACKTRACK_CACHE_SIZE
    fprintf(stdout, CSV_COUNT_DESC, g->stats.cache_steals);
    fprintf(stdout, CSV_COUNT_DESC, g->stats.cache_insertions);
#endif
    fprintf(stdout, "\n");
    cilk_sched_stats_print_backtrack_csv(g);
    for_each_worker(g, &sched_stats_reset_worker, NULL);
}

static void sched_stats_print_worker_human_readable(__cilkrts_worker *w, void *data) {
    FILE *fp = (FILE *)data;
    fprintf(fp, WORKER_HDR_DESC, "Worker", w->self);
    for (int t = 0; t < (NUMBER_OF_STATS); t++) {
        double tmp = nsec_to_sec(w->l->stats.time[t]);
        w->g->stats.time[t] += (double)tmp;
        uint64_t tmp_count = w->l->stats.count[t];
        w->g->stats.count[t] += tmp_count;
        fprintf(fp, FIELD_DESC, tmp, tmp_count);
    }
    w->g->stats.steals += w->l->stats.steals;
    w->g->stats.successful_steals += w->l->stats.successful_steals;
    w->g->stats.failed_steals += w->l->stats.failed_steals;
    w->g->stats.frames_backtracked += w->l->stats.frames_backtracked;
    w->g->stats.slow_steals += w->l->stats.slow_steals;
#if CILK_BACKTRACK_CACHE_SIZE
    w->g->stats.cache_steals += w->l->stats.cache_steals;
    w->g->stats.cache_insertions += w->l->stats.cache_insertions;
#endif
    fprintf(stderr, COUNT_DESC, w->l->stats.steals);
    fprintf(stderr, COUNT_DESC, w->l->stats.frames_backtracked);
    fprintf(stderr, COUNT_DESC, w->l->stats.successful_steals);
    fprintf(stderr, COUNT_DESC, w->l->stats.failed_steals);
    fprintf(stderr, COUNT_DESC, w->l->stats.slow_steals);
#if CILK_BACKTRACK_CACHE_SIZE
    fprintf(stderr, COUNT_DESC, w->l->stats.cache_steals);
    fprintf(stderr, COUNT_DESC, w->l->stats.cache_insertions);
#endif
    fprintf(fp, "\n");
}

void cilk_sched_stats_print_human_readable(struct global_state *g) {
    for (int t = 0; t < NUMBER_OF_STATS; t++) {
        g->stats.time[t] = 0.0;
        g->stats.count[t] = 0;
    }
    g->stats.steals = 0;
    g->stats.repos = 0;
    g->stats.slow_steals = 0;

    fprintf(stderr, "\nSCHEDULING STATS (SECONDS):\n");
    {
        fprintf(stderr, COL_DESC, "Boss waiting:");
        double tmp = nsec_to_sec(g->stats.boss_waiting);
        fprintf(stderr, FIELD_DESC, tmp, g->stats.boss_wait_count);
        fprintf(stderr, "\n");
        g->stats.boss_waiting = 0;
        g->stats.boss_wait_count = 0;
    }
    fprintf(stderr, COL_DESC, "");
    for (int t = 0; t < (NUMBER_OF_STATS); t++) {
        fprintf(stderr, HDR_DESC, enum_to_str(t), "count");
    }
    fprintf(stderr, COUNT_HDR_DESC, "steals");
    fprintf(stderr, COUNT_HDR_DESC, "bcktrkd");
    fprintf(stderr, COUNT_HDR_DESC, "success");
    fprintf(stderr, COUNT_HDR_DESC, "failure");
    fprintf(stderr, COUNT_HDR_DESC, "slow");
#if CILK_BACKTRACK_CACHE_SIZE
    fprintf(stderr, COUNT_HDR_DESC, "s_cache");
    fprintf(stderr, COUNT_HDR_DESC, "i_cache");
#endif
    fprintf(stderr, "\n");

    for_each_worker(g, &sched_stats_print_worker_human_readable, stderr);

    fprintf(stderr, COL_DESC, "Total:");
    for (int t = 0; t < (NUMBER_OF_STATS); t++) {
        fprintf(stderr, FIELD_DESC, g->stats.time[t], g->stats.count[t]);
    }
    fprintf(stderr, COUNT_DESC, g->stats.steals);
    fprintf(stderr, COUNT_DESC, g->stats.frames_backtracked);
    fprintf(stderr, COUNT_DESC, g->stats.successful_steals);
    fprintf(stderr, COUNT_DESC, g->stats.failed_steals);
    fprintf(stderr, COUNT_DESC, g->stats.slow_steals);
#if CILK_BACKTRACK_CACHE_SIZE
    fprintf(stderr, COUNT_DESC, g->stats.cache_steals);
    fprintf(stderr, COUNT_DESC, g->stats.cache_insertions);
#endif
    fprintf(stderr, "\n");

    for_each_worker(g, &sched_stats_reset_worker, NULL);
}

void cilk_sched_stats_fix_work_worker(__cilkrts_worker *w, void* data) {
    w->l->stats.time[INTERVAL_WORK] -= w->l->stats.time[INTERVAL_INTERUPTED];
}

void cilk_sched_stats_fix_work(struct global_state *g) {
    for_each_worker(g, &cilk_sched_stats_fix_work_worker, NULL);
    g->stats.time[INTERVAL_WORK] -= g->stats.time[INTERVAL_INTERUPTED];
}

void cilk_sched_stats_print(struct global_state *g) {
    char* stats_format_env = getenv("CILKSTATS_FORMAT");
    enum output_format stats_format = (stats_format_env ? atoi(stats_format_env) : OUTPUT_FORMAT_HUMAN_READABLE);
    //cilk_sched_stats_fix_work(g);
    switch (stats_format) {
    case OUTPUT_FORMAT_HUMAN_READABLE:
        cilk_sched_stats_print_human_readable(g);
        break;
    case OUTPUT_FORMAT_CSV:
        cilk_sched_stats_print_csv(g);
        break;
    default:
        cilk_sched_stats_print_human_readable(g);
        break;
    }
//    print_spawn_state_data();
}

/*
void cilk_reset_timing() {
    global_state_t *g = cilkg_get_global_state();
    for(int i = 0; i < NUMBER_OF_STATS; ++i) {
        //resent global counter
        g->stats->time[i] = 0.0;

        //reset worker local counter
        for(int j = 0; j < g->total_workers; ++j) {
            g->workers[j]->l->stats->time[i] = 0;
        }

    }

    LIKWID_MARKER_CLOSE;
    LIKWID_MARKER_INIT;
}
*/
#endif

void __cilkrts_sched_stats_print(void) {
    WHEN_SCHED_STATS(cilk_sched_stats_print(default_cilkrts));
}
