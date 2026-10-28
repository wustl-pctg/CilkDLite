#ifndef BENCHMARK_REGISTER_SPAWNS_H
#define BENCHMARK_REGISTER_SPAWNS_H

#include "../runtime/cilk2c.h"
#include <stdio.h>
#include <string.h>

extern const unsigned char _SPAWN_ADDR_MAP_START_[];
extern const unsigned char _SPAWN_ADDR_MAP_END_[];

/* The backtracking compiler emits a function address and record count followed
 * by 12 32-bit words per spawn. Match the existing research benchmark ABI. */
__attribute__((constructor(200))) static void register_benchmark_spawns(void) {
    const unsigned char *cursor = _SPAWN_ADDR_MAP_START_;
    while (cursor < _SPAWN_ADDR_MAP_END_) {
        uintptr_t function;
        uint64_t count;
        if ((size_t)(_SPAWN_ADDR_MAP_END_ - cursor) < 16) abort();
        memcpy(&function, cursor, 8);
        memcpy(&count, cursor + 8, 8);
        cursor += 16;
        for (uint64_t i = 0; i < count; ++i) {
            uint32_t record[12];
            if ((size_t)(_SPAWN_ADDR_MAP_END_ - cursor) < sizeof(record)) abort();
            memcpy(record, cursor, sizeof(record));
            cursor += sizeof(record);
            uintptr_t retpad = function + record[3];
            uintptr_t landpad = function + record[7];
            uintptr_t poppad = function + record[11];
            if (__cilkrts_register_spawn_state(retpad, retpad, poppad, landpad)) {
                fputs("Could not register benchmark spawn metadata\n", stderr);
                abort();
            }
        }
    }
}
#endif
