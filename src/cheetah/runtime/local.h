#ifndef _CILK_LOCAL_H
#define _CILK_LOCAL_H

#include <stdbool.h>
#include "backtrack.h"


struct worker_context {
    unw_context_t current_context;
    walker_state current_cursor;
    uintptr_t stored_rsp;
    uintptr_t stored_rbp;
    uintptr_t stored_rip;
    int end_of_stack; //used to pretend that this is a __Unwind_Context
    volatile int poll_flag;
    volatile int fail_flag;
};

#define PERSONALITY_CACHE_SIZE 100

struct spawn_item {
    uintptr_t location;
    int personality_val;
    int use_count;
};

struct local_state {
    unsigned short state; /* __cilkrts_worker_state */
    bool lock_wait;
    bool provably_good_steal;
    unsigned int rand_next;
    // Local copy of the index-to-worker map.
    worker_id *index_to_worker;

    jmpbuf rts_ctx;
    struct cilk_fiber_pool fiber_pool;
    struct cilk_im_desc im_desc;
    struct cilk_fiber *fiber_to_free;
    struct sched_stats stats;
    struct worker_context context;
    struct spawn_item* spawn_cache;
    int cache_count;
    void* old_held_func;
    void* held_func;
    void* caller_func;
    void* callee_func;
    int thief_id;

    int is_slow_spawn;
#if CILK_BACKTRACK_CACHE_SIZE
    bool cache_ready;
    int stolen_cache_count; //The number of cache partitions that have been taken by theives
    volatile int stored_cache_count; //The current remaining number of cache items.  This should only be accessed with synchronization
    int resumed_cache_count; //The number of cache partitions that have been resumed by the worker
    __cilkrts_regstate_cache_item regstate_cache[CILK_BACKTRACK_CACHE_SIZE];
    int cacheless_steal_buffer;
#endif
};

#endif /* _CILK_LOCAL_H */
