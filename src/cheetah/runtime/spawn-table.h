#ifndef _SPAWNTABLE_H
#define _SPAWNTABLE_H
#include <stdint.h>
#include "cilk-internal.h"
#include "sched_stats.h"
#include "rts-config.h"

typedef struct __spawn_state_registration {
    uintptr_t fast_call_address;
    uintptr_t slow_return_address;
    uintptr_t slow_thief_address;
    WHEN_SCHED_STATS( int seen; )
    WHEN_SCHED_STATS( int collisions; )
} __spawn_state_registration;


#if SPAWN_STATE_HASHMAP

#define SPAWN_STATE_HASHMAP_LOAD_FACTOR 4
#define SPAWN_STATE_HASHMAP_INITIAL_CAPACITY 50

//We actually have a hashmap and a hashset to look up registrations by address
//and to check for pop existence
typedef struct __spawn_state_collection {
    __spawn_state_registration* items;
    uintptr_t* pop_existence;
    int count;
    int capacity;
} __spawn_state_collection;

#else 

typedef struct __spawn_state_collection {
    __spawn_state_registration* items;
    int length;
} __spawn_state_collection;

#endif


__spawn_state_collection initialize_spawn_state_collection();

int __cilkrts_register_spawn_state(uintptr_t _fast_call_address, uintptr_t _slow_call_address, uintptr_t _slow_return_address, uintptr_t _slow_theif_address);
__spawn_state_registration* check_for_spawn_state_in_list(uintptr_t ip, __cilkrts_worker* const w);
bool check_for_pop_state_in_list(uintptr_t ip, __cilkrts_worker* const w);

void print_spawn_state_data();


#endif