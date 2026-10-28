#include "spawn-table.h"
#include "rts-config.h"
#include "cilk-internal.h"
#include "closure.h"
#include "debug.h"
#include "jmpbuf.h"


#if SPAWN_STATE_HASHMAP

__spawn_state_collection initialize_spawn_state_collection() {
    __spawn_state_collection ssc;
    ssc.items = calloc(SPAWN_STATE_HASHMAP_INITIAL_CAPACITY, sizeof(__spawn_state_registration));
    ssc.pop_existence = calloc(SPAWN_STATE_HASHMAP_INITIAL_CAPACITY, sizeof(uintptr_t));;
    ssc.count = 0;
    ssc.capacity = SPAWN_STATE_HASHMAP_INITIAL_CAPACITY;
    return ssc;
}

void __cilkrts_resize_spawn_states() {
    __spawn_state_registration* old_states = default_cilkrts->spawn_states.items;
    int old_len = default_cilkrts->spawn_states.capacity;
    free(default_cilkrts->spawn_states.pop_existence);
    default_cilkrts->spawn_states.capacity = default_cilkrts->spawn_states.capacity * 2;
    default_cilkrts->spawn_states.items = calloc(default_cilkrts->spawn_states.capacity, sizeof(__spawn_state_registration));
    default_cilkrts->spawn_states.pop_existence = calloc(default_cilkrts->spawn_states.capacity, sizeof(uintptr_t));
    default_cilkrts->spawn_states.count = 1; //Reset count as we're going to reinsert all items, but we need to remember that we have a value queued to insert

    for (int index = 0; index < old_len; index++) {
        if (old_states[index].fast_call_address) __cilkrts_register_spawn_state(old_states[index].fast_call_address, old_states[index].fast_call_address, old_states[index].slow_return_address, old_states[index].slow_thief_address);
    }
    free(old_states);
}

int __cilkrts_hash_spawn_state(uintptr_t ptr, int capacity) {
    return ptr % capacity;
}

int __cilkrts_register_spawn_state(uintptr_t _fast_call_address, uintptr_t _slow_call_address, uintptr_t _slow_return_address, uintptr_t _slow_thief_address) {
    //If we outgrow our load factor, double the size
    if ((default_cilkrts->spawn_states.count++ * SPAWN_STATE_HASHMAP_LOAD_FACTOR) >=  default_cilkrts->spawn_states.capacity) __cilkrts_resize_spawn_states();
    int location = __cilkrts_hash_spawn_state(_fast_call_address, default_cilkrts->spawn_states.capacity);
    //linear probing with overwrite on same key
    while(default_cilkrts->spawn_states.items[location].fast_call_address && (default_cilkrts->spawn_states.items[location].fast_call_address != _fast_call_address)) location = (location + 1)%default_cilkrts->spawn_states.capacity;
    default_cilkrts->spawn_states.items[location].fast_call_address = _fast_call_address;
    default_cilkrts->spawn_states.items[location].slow_return_address = _slow_return_address;
    default_cilkrts->spawn_states.items[location].slow_thief_address = _slow_thief_address;

    int pop_location = __cilkrts_hash_spawn_state(_slow_return_address, default_cilkrts->spawn_states.capacity);
    while(default_cilkrts->spawn_states.pop_existence[pop_location] && (default_cilkrts->spawn_states.pop_existence[pop_location] != _slow_return_address)) pop_location = (pop_location + 1)%default_cilkrts->spawn_states.capacity;
    default_cilkrts->spawn_states.pop_existence[pop_location] = _slow_return_address;
    // printf("0x%lx, ", _fast_call_address);
    return 0;
}

struct __spawn_state_registration* check_for_spawn_state_in_list(uintptr_t ip, __cilkrts_worker* const w) {
    int location = __cilkrts_hash_spawn_state(ip, default_cilkrts->spawn_states.capacity);
    //linear probing
    while(default_cilkrts->spawn_states.items[location].fast_call_address && default_cilkrts->spawn_states.items[location].fast_call_address != ip){
        WHEN_SCHED_STATS(default_cilkrts->spawn_states.items[location].collisions++;)
        location = (location + 1)%default_cilkrts->spawn_states.capacity;
    } 
    //return NULL if we don't find it
    if (!default_cilkrts->spawn_states.items[location].fast_call_address) return NULL;
    CILK_ASSERT(w, default_cilkrts->spawn_states.items[location].fast_call_address == ip);
    WHEN_SCHED_STATS(default_cilkrts->spawn_states.items[location].seen++;)
    return &(default_cilkrts->spawn_states.items[location]);
}


bool check_for_pop_state_in_list(uintptr_t ip, __cilkrts_worker* const w) {
    int location = __cilkrts_hash_spawn_state(ip, default_cilkrts->spawn_states.capacity);
    //linear probing
    while(default_cilkrts->spawn_states.pop_existence[location] && default_cilkrts->spawn_states.pop_existence[location] != ip) location = (location + 1)%default_cilkrts->spawn_states.capacity;
    //return NULL if we don't find it
    if (!default_cilkrts->spawn_states.pop_existence[location]) return false;
    CILK_ASSERT(w, default_cilkrts->spawn_states.pop_existence[location] == ip);
    return true;
}
#if SCHED_STATS
void print_spawn_state_data() {
    __spawn_state_registration* states = default_cilkrts->spawn_states.items;
    int len = default_cilkrts->spawn_states.capacity;
    printf("Spawn,count,collisions\n");
    for(int i = 0; i < len; i++) {
        if (states[i].fast_call_address) printf("0x%ld,%d,%d\n", states[i].fast_call_address, states[i].seen, states[i].collisions);
    }
}
#else
void print_spawn_state_data() {
}
#endif


#else 

__spawn_state_collection initialize_spawn_state_collection() {
    __spawn_state_collection ssc;
    ssc.length = 0;
    ssc.items = calloc(SPAWN_STATE_LIST_MAX_LENGTH, sizeof(__spawn_state_registration));
    return ssc;
}

int __cilkrts_register_spawn_state(uintptr_t _fast_call_address, uintptr_t _slow_call_address, uintptr_t _slow_return_address, uintptr_t _slow_thief_address) {
    int location = default_cilkrts->spawn_states.length++;
    if (location >= SPAWN_STATE_LIST_MAX_LENGTH) return 1;
    default_cilkrts->spawn_states.items[location].fast_call_address = _fast_call_address;
    default_cilkrts->spawn_states.items[location].slow_return_address = _slow_return_address;
    default_cilkrts->spawn_states.items[location].slow_thief_address = _slow_thief_address;
    return 0;
}

struct __spawn_state_registration* check_for_spawn_state_in_list(uintptr_t ip, __cilkrts_worker* const w) {
    for (int i = 0; i < SPAWN_STATE_LIST_MAX_LENGTH; i++) {
        if (w->g->spawn_states.items[i].fast_call_address == 0) break;
        if (w->g->spawn_states.items[i].fast_call_address == ip) {
            //printf("ip: %lx did match fast_call_address %lx or slow_call_address %lx\n", ip, w->g->spawn_state_list[i].fast_call_address, w->g->spawn_state_list[i].slow_call_address );
            return &(w->g->spawn_states.items[i]);
        } else {
            //printf("ip: %lx did not match fast_call_address %lx or slow_call_address %lx\n", ip, w->g->spawn_state_list[i].fast_call_address, w->g->spawn_state_list[i].slow_call_address );
        }
    }
    return NULL;
}


bool check_for_pop_state_in_list(uintptr_t ip, __cilkrts_worker* const w) {
    for (int i = 0; i < SPAWN_STATE_LIST_MAX_LENGTH; i++) {
        if (w->g->spawn_states.items[i].fast_call_address == 0) break;
        if (w->g->spawn_states.items[i].slow_return_address == ip) {
            //printf("ip: %lx did match fast_call_address %lx or slow_call_address %lx\n", ip, w->g->spawn_state_list[i].fast_call_address, w->g->spawn_state_list[i].slow_call_address );
            return true;
        } else {
            //printf("ip: %lx did not match fast_call_address %lx or slow_call_address %lx\n", ip, w->g->spawn_state_list[i].fast_call_address, w->g->spawn_state_list[i].slow_call_address );
        }
    }
    return false;
}

void print_spawn_state_data(){

}


#endif