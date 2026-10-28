#pragma once
#include <stdint.h>
#define CHEETAH_INTERNAL_FUNCTION static inline __attribute__((always_inline))
#include "cilk2c.h"

// #define DO_CILK_FOR
#ifdef DO_CILK_FOR
typedef void (*__cilk_abi_f64_t)(void*, int64_t, int64_t);

void cilk_for_recursive(uint64_t low,
                        uint64_t high,
                        __cilk_abi_f64_t body,
                        void* data,
                        uint64_t grain);

void __cilkrts_cilk_for_64(__cilk_abi_f64_t body,
                           void *data,
                           __int64_t count,
                           int grain);
#endif