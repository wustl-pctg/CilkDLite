#ifndef _CILK2C_H
#define _CILK2C_H

#include "cilk-internal.h"
#include "init.h"
#include "global.h"
#include <stdlib.h>

#ifndef CHEETAH_INTERNAL_FUNCTION
#define CHEETAH_INTERNAL_FUNCTION CHEETAH_INTERNAL
#endif

// Returns 1 if the current exection is running on Cilk workers, 0 otherwise.
CHEETAH_API int __cilkrts_running_on_workers(void);

// ABI functions inlined by the compiler (provided as a bitcode file after
// compiling runtime) are defined in cilk2c_inline.c.
// ABI functions not inlined by the compiler are defined in cilk2c.c.

static inline __attribute__((always_inline)) char* get_current_rbp() {
    char* this_rbp;
    __asm__ volatile("movq %%rbp, %0" : "=r"(this_rbp));
    return this_rbp;
};
static inline __attribute__((always_inline)) char* get_current_rsp() {
    char* this_rsp;
    __asm__ volatile("movq %%rsp, %0" : "=r"(this_rsp));
    return this_rsp;
};
static inline __attribute__((always_inline)) char* get_current_rip() {
    char* this_rip;
    __asm__ volatile("movq %%rip, %0" : "=r"(this_rip));
    return this_rip;
};

static inline __attribute__((unused, always_inline)) void
cilkify(global_state *g) {
#ifdef ENABLE_CILKRTS_PEDIGREE
    __cilkrts_init_dprng();
#endif

    // After inlining, the setjmp saves the processor state, including the frame
    // pointer, of the Cilk function.
    if (__builtin_setjmp(g->root_closure->ctx) == 0) {
        sysdep_save_fp_ctrl_state_raw(&(g->mxcsr));
        __cilkrts_internal_invoke_cilkified_root(g);
    } else {
        sanitizer_finish_switch_fiber();
    }
    g->root_closure->functional_rbp = get_current_rbp();
}

// End a Cilkified region.  This routine runs on one worker in global_state g
// who finished executing the Cilkified region, in order to transfer control
// back to the original thread that began the Cilkified region.  This routine
// must be inlined for correctness.
static inline __attribute__((unused, always_inline)) void
uncilkify(global_state *g) {
    // The setjmp will save the processor state at the end of the Cilkified
    // region.  The Cilkifying thread will longjmp to this point.
    if (__builtin_setjmp(g->root_closure->ctx) == 0) {
        sysdep_save_fp_ctrl_state_raw(&(g->mxcsr));
        // Finish this Cilkified region, and transfer control back to the
        // original thread that performed cilkify.
        __cilkrts_internal_exit_cilkified_root(g);
    } else {
        sanitizer_finish_switch_fiber();
    }
}
// Inserted at the entry of a spawning function that is not itself a spawn
// helper.  Initializes the stack frame sf allocated for that function.
CHEETAH_INTERNAL_FUNCTION void __cilkrts_enter_frame();

// Inserted at the entry of a spawn helper, i.e., a function that must have been
// spawned.  Initializes the stack frame sf allocated for that function.
CHEETAH_INTERNAL_FUNCTION void __cilkrts_enter_frame_helper();

// Prepare to perform a spawn.  This function may return once or twice,
// returning 0 the first time and 1 the second time.  This function is intended
// to be used follows:
//
//   if (0 == __cilk_spawn_prepare(sf)) {
//     spawn_helper(args);
//   }
CHEETAH_INTERNAL_FUNCTION int __cilk_prepare_spawn();

// Called in the spawn helper immediately before the spawned computation.
// Enables the parent function to be stollen.
CHEETAH_INTERNAL_FUNCTION void __cilkrts_detach();

// Called in the spawn helper immediately before the spawned computation.
// Enables the parent function to be stollen.
CHEETAH_INTERNAL_FUNCTION void __cilkrts_detach_self();

// Check if the runtime is storing an exception we need to handle later, and
// raises that exception if so.
CHEETAH_API void __cilkrts_check_exception_raise();

// Check if the runtime is storing an exception we need to handle later, and
// resumes unwinding with that exception if so.
CHEETAH_API void __cilkrts_check_exception_resume();

// Performs runtime operations to handle a cilk_sync.
__attribute__((noreturn, nothrow))
CHEETAH_API void __cilkrts_sync(Closure *cl);

// Implements a cilk_sync when the cilk_sync might produce an exception that
// needs to be handled.
CHEETAH_INTERNAL_FUNCTION void __cilk_sync();

// Implements a cilk_sync when the cilk_sync is guaranteed not to produce an
// exception that needs to be handled.
CHEETAH_INTERNAL_FUNCTION void __cilk_sync_nothrow();

// Deprecated?  Removes the current stack frame from the bottom of the stack.
// (This logic has been manually inlined into __cilkrts_leave_frame,
// __cilkrts_leave_frame_helper, and __cilkrts_pause_frame.)
CHEETAH_INTERNAL_FUNCTION void __cilkrts_pop_frame();

// Deprecated?  Removes the current stack frame from the bottom of the stack.
// (This logic has been manually inlined into __cilkrts_leave_frame,
// __cilkrts_leave_frame_helper, and __cilkrts_pause_frame.)
CHEETAH_INTERNAL_FUNCTION void __cilkrts_pop_frame_self();


// Inserted on return from a spawning function that is not itself a spawn
// helper.  Performs Cilk's return protocol for such functions.
CHEETAH_INTERNAL_FUNCTION void __cilkrts_leave_frame();

// Inserted on return from a spawn-helper function.  Performs Cilk's return
// protocol for such functions.
CHEETAH_INTERNAL_FUNCTION void __cilkrts_leave_frame_helper();

// Performs all necessary operations on return from a spawning function that is
// not itself a spawn helper.
CHEETAH_INTERNAL_FUNCTION void __cilk_parent_epilogue();

// Performs all necessary operations on return from a spawning function that is
// not itself a spawn helper.
CHEETAH_INTERNAL_FUNCTION void __cilk_self_epilogue();


// Performs all necessary operations on return from a spawn-helper function.
CHEETAH_INTERNAL_FUNCTION void __cilk_helper_epilogue();

// Performs all necessary runtime updates when execution enters a landingpad in
// a spawning function.
CHEETAH_INTERNAL_FUNCTION
void __cilkrts_enter_landingpad(int32_t sel);

// Called from __cilkrts_enter_landingpad to optionally fix the current stack
// pointer and cleanup a fiber that was previously saved for exception handling.
CHEETAH_API void __cilkrts_cleanup_fiber(int32_t sel);

// Performs Cilk's return protocol on an exceptional return (i.e., a resume)
// from a spawn-helper function.
CHEETAH_INTERNAL_FUNCTION void __cilkrts_pause_frame(char *exn);

// Inserted on an exceptional return (i.e., a resume) from a spawn-helper
// function.
CHEETAH_INTERNAL_FUNCTION void __cilk_pause_frame(char *exn);

// Compute the grainsize for a cilk_for loop at runtime, based on the number n
// of loop iterations.
CHEETAH_INTERNAL_FUNCTION uint8_t __cilkrts_cilk_for_grainsize_8(uint8_t n);
CHEETAH_INTERNAL_FUNCTION uint16_t __cilkrts_cilk_for_grainsize_16(uint16_t n);
CHEETAH_INTERNAL_FUNCTION uint32_t __cilkrts_cilk_for_grainsize_32(uint32_t n);
CHEETAH_INTERNAL_FUNCTION uint64_t __cilkrts_cilk_for_grainsize_64(uint64_t n);

// Not marked as CHEETAH_API as it may be deprecated soon
unsigned __cilkrts_get_nworkers(void);
//CHEETAH_API int64_t* __cilkrts_get_pedigree(void);
//void __cilkrts_pedigree_bump_rank(void);
#endif
