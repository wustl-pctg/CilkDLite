#include "cilkfor.h"
#include "backtrack.h"
#include "cilk2c_inlined.c"
#ifdef DO_CILK_FOR

extern size_t ZERO;
void __attribute__((weak)) dummy(void *p) { return; }

void cilk_for_recursive(uint64_t low, 
                        uint64_t high,
                        __cilk_abi_f64_t body, 
                        void *data, 
                        uint64_t grain)
{
    __CILK_FUNC_PREFIX;
tail_recurse:;
    // Cilkscreen should not report this call in a stack trace
    // This needs to be done everytime the worker resumes
    uint64_t count = high - low;
    // Invariant: count > 0, grain >= 1
    if (count > grain)
    {
        // Invariant: count >= 2
        uint64_t mid = low + count / 2;
        // The worker is valid only until the first spawn and is expensive to
        // retrieve (using '__cilkrts_get_tls_worker') after the spawn.  The
        // '__cilkrts_stack_frame' is more stable, but isn't initialized until
        // the first spawn.  Thus, we want to grab the address of the
        // '__cilkrts_stack_frame' after it is initialized but before the
        // spawn detaches.  The only place we can do that is within the
        // argument list of the spawned function, hence the call to
        // capture_spawn_arg_stack_frame().
        __CILK_FUNC_SPAWN(for_loop_lbl,
            cilk_for_recursive(low, mid, body, data, grain);
        );
        low = mid;

        goto tail_recurse;
    }

    // Call the cilk_for loop body lambda function passed in by the compiler to
    // execute one grain
    body(data, low, high);
    __CILK_FUNC_SYNC;
    __CILK_FUNC_SUFFIX;
}

void __cilkrts_cilk_for_64(__cilk_abi_f64_t body,
                           void *data,
                           __int64_t count,
                           int grain) {
    cilk_for_recursive(0,
                       count,
                       body,
                       data,
                       grain);
}
#endif