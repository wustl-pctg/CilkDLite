// =============================================================================
// This file contains the compiler-runtime ABI.  This file is compiled to LLVM
// bitcode, which the compiler then includes and inlines when it compiles a Cilk
// program.
// =============================================================================
#include <stdatomic.h>
#include <stdio.h>
#include <unwind.h>

#include "cilk-internal.h"
#include "cilk2c.h"
#include "debug.h"
#include "fiber.h"
#include "global.h"
#include "init.h"
#include "readydeque.h"
#include "scheduler.h"
#include "sched_stats.h"

#ifdef ENABLE_CILKRTS_PEDIGREE
extern __cilkrts_pedigree cilkrts_root_pedigree_node;
extern uint64_t DPRNG_PRIME;
extern uint64_t* dprng_m_array;
extern uint64_t dprng_m_X;

uint64_t __cilkrts_dprng_swap_halves(uint64_t x);
uint64_t __cilkrts_dprng_mix(uint64_t x);
uint64_t __cilkrts_dprng_mix_mod_p(uint64_t x);
uint64_t __cilkrts_dprng_sum_mod_p(uint64_t a, uint64_t b);
void __cilkrts_init_dprng(void);

uint64_t __cilkrts_get_dprand(void) {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    __cilkrts_bump_worker_rank();
    return __cilkrts_dprng_mix_mod_p(w->current_stack_frame->dprng_dotproduct);
}
#endif

__attribute__((naked))
__attribute__((noinline))
__attribute__((returns_twice))
int backtrack_setjmp_bitcode(__cilkrts_regstate* regstate) {
    asm volatile ("movq %rax, 0x00(%rdi)"); //0
    asm volatile ("movq %rdx, 0x08(%rdi)"); //1
    asm volatile ("movq %rcx, 0x10(%rdi)"); //2
    asm volatile ("movq %rbx, 0x18(%rdi)"); //3
    asm volatile ("movq %rsi, 0x20(%rdi)"); //4
    asm volatile ("movq %rdi, 0x28(%rdi)"); //5    
    asm volatile ("movq %rbp, 0x30(%rdi)"); //6
    asm volatile ("leaq 0x8(%rsp), %rax"); //7
    asm volatile ("mov %rax, 0x38(%rdi)"); //7
    asm volatile ("movq %r8,  0x40(%rdi)"); //8
    asm volatile ("movq %r9,  0x48(%rdi)"); //9
    asm volatile ("movq %r10, 0x50(%rdi)"); //10
    asm volatile ("movq %r11, 0x58(%rdi)"); //11
    asm volatile ("movq %r12, 0x60(%rdi)"); //12
    asm volatile ("movq %r13, 0x68(%rdi)"); //13
    asm volatile ("movq %r14, 0x70(%rdi)"); //14
    asm volatile ("movq %r15, 0x78(%rdi)"); //15
    asm volatile ("movq 0x0(%rsp), %rax"); //16
    asm volatile ("movq %rax, 0x80(%rdi)"); //16
    asm volatile ("xorq %rax, %rax"); //set rax to 0 - will be set to 1 if we're jumping in from a provably good steal
    asm volatile ("retq");
}


// Begin a Cilkified region.  The routine runs on a Cilkifying thread to
// transfer the execution of this function to the workers in global_state g.
// This routine must be inlined for correctness.


#ifdef ENABLE_CILKRTS_PEDIGREE
__attribute__((always_inline)) __cilkrts_pedigree __cilkrts_get_pedigree(void) {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    if (w == NULL) {
        return cilkrts_root_pedigree_node;
    } else {
        __cilkrts_pedigree ret_ped;
        ret_ped.parent = &(w->current_stack_frame->pedigree);
        ret_ped.rank = w->current_stack_frame->rank;
        return ret_ped;
    }
}

__attribute__((always_inline)) void __cilkrts_bump_worker_rank(void) {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    if (w == NULL) {
        cilkrts_root_pedigree_node.rank++;
    } else {
        w->current_stack_frame->rank++;
    }
    w->current_stack_frame->dprng_dotproduct = __cilkrts_dprng_sum_mod_p(
                w->current_stack_frame->dprng_dotproduct,
                dprng_m_array[w->current_stack_frame->dprng_depth]);
}
#endif

__attribute__((always_inline)) int
__cilk_prepare_spawn() {
#ifdef USING_UNWIND_STEALS
    int res = 0;
#else
    int res = __builtin_setjmp(sf->ctx);
    if (res != 0) {
        sanitizer_finish_switch_fiber();
    }
#endif
    return res;
}

// // Detach the given Cilk stack frame, allowing other Cilk workers to steal the
// // parent frame.
// __attribute__((always_inline)) void
// __cilkrts_detach() {
//     __cilkrts_worker *w = get_tls_worker(sf);
//     cilkrts_alert(CFRAME, w, "__cilkrts_detach %p", (void *)sf);

//     CILK_ASSERT(w, CHECK_CILK_FRAME_MAGIC(w->g, sf));
//     CILK_ASSERT(w, sf->worker == __cilkrts_get_tls_worker());
//     CILK_ASSERT(w, w->current_stack_frame == sf);

//     struct __cilkrts_stack_frame *parent = sf->call_parent;
//     sf->flags |= CILK_FRAME_DETACHED;
//     struct __cilkrts_stack_frame **tail =
//         atomic_load_explicit(&w->tail, memory_order_relaxed);
//     CILK_ASSERT(w, (tail + 1) < w->ltq_limit);

//     // store parent at *tail, and then increment tail
//     *tail++ = parent;
//     /* Release ordering ensures the two preceding stores are visible. */
//     atomic_store_explicit(&w->tail, tail, memory_order_release);
// }

#if SIGNAL_FREE_BACKTRACKING
void __record_registers(__cilkrts_worker *w) {
    // CILK_SWITCH_TIMING(w, INTERVAL_WORK, INTERVAL_INTERUPTED);
#if CILK_BACKTRACK_STACK_WALKER
    w->l->context.stored_rip = (uintptr_t) NULL;
    w->l->context.stored_rsp = (uintptr_t) get_current_rsp();
    w->l->context.stored_rbp = (uintptr_t) get_current_rbp();
#else
    unw_getcontext(&(w->l->context.current_context));
#endif

    __sync_fetch_and_add(&(w->l->context.poll_flag), 1);
    while (w->l->context.poll_flag) {
    }
    // CILK_SWITCH_TIMING(w, INTERVAL_INTERUPTED, INTERVAL_WORK);
}
#endif

__attribute__((always_inline)) void
__cilkrts_detach_self() {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    cilkrts_alert(CFRAME, w, "__cilkrts_detach");
}

__attribute__((always_inline)) void __cilk_sync() {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    Closure * cl = w->g->deques[w->self].bottom;
    CILK_ASSERT(w, cl);
    CILK_ASSERT(w, cl->stolen_flag);
    if (!cl->synced_flag) {

        #if SIGNAL_FREE_BACKTRACKING
            if (w->waiting_thief) {
                __record_registers(w);
            }
        #endif
        if (__builtin_setjmp(cl->ctx) == 0) {
            __cilkrts_sync(cl);
        } else {
            sanitizer_finish_switch_fiber();
        }
    }
}

__attribute__((always_inline)) void
__cilk_sync_nothrow() {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    Closure * cl = w->g->deques[w->self].bottom;
    CILK_ASSERT(w, cl);
    CILK_ASSERT(w, cl->stolen_flag);
    if (!cl->synced_flag) {
        #if SIGNAL_FREE_BACKTRACKING
            if (w->waiting_thief) {
                __record_registers(w);
            }
        #endif
        if (__builtin_setjmp(cl->ctx) == 0) {
            __cilkrts_sync(cl);
        } else {
            sanitizer_finish_switch_fiber();
        }
    }
}

__attribute__((always_inline)) void
__cilkrts_pop_frame() {
    Cilk_exception_handler(NULL);
}

//return 1 if stolen, 0 if not
__attribute__((always_inline, returns_twice)) uint8_t
__cilkrts_mark_slow_spawn() {
#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
#if CILK_BACKTRACK_ENABLE_SLOW_SPAWN
    __cilkrts_worker * w = __cilkrts_get_tls_worker();
    if (backtrack_setjmp(&(w->l->regstate_cache[0].regstate)) == 0) {
        CILK_ASSERT(w, w->l->regstate_cache[0].regstate.registers[UNW_X86_64_RIP]);
        w->l->regstate_cache[0].regstate.pop_addr = (uintptr_t) 0x0707070707070707; //shouldn't use the pop_addr
        w->l->regstate_cache[0].regstate.registers[UNW_X86_64_RAX] = 1;
        CILK_ASSERT(w, w->l->stored_cache_count == 0);
        return 0;
    } else {
        return 1;
    }
#endif
#endif
    return 0;
}


//return 1 if stolen, 0 if not
__attribute__((always_inline)) void
__cilkrts_enable_slow_spawn() {
#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
#if CILK_BACKTRACK_ENABLE_SLOW_SPAWN
    deque_lock_self(w);
    CILK_ASSERT(w, w->l->stored_cache_count <= 0);
    w->l->regstate_cache[0].valid = 1;
    w->l->regstate_cache[0].spawned_cache = 1;
    w->l->regstate_cache[0].regstate.ret_addr = ((void**) (get_current_rbp()))+1;
    w->l->regstate_cache[0].regstate.old_rbp = *((uintptr_t*) get_current_rbp());
    struct __spawn_state_registration* ssr = check_for_spawn_state_in_list(*((uintptr_t*) w->l->regstate_cache[0].regstate.ret_addr), w);
    CILK_ASSERT(w, ssr);
    w->l->regstate_cache[0].regstate.thief_retaddr = ssr->slow_thief_address;
    w->l->stolen_cache_count = 0;
    w->l->resumed_cache_count = 0;
    w->l->stored_cache_count = 1;
    deque_unlock_self(w);
#endif
    *((volatile int*) & (w->spawn_counter)) += 1;
#endif
    return;
}


__attribute__((always_inline)) void
__cilkrts_leave_slow_spawn() {
#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    *((volatile int*) & (w->spawn_counter)) -= 1;
    CILK_ASSERT(w, w->spawn_counter >= -1);
#if CILK_BACKTRACK_ENABLE_SLOW_SPAWN
    int remaining_cache = __atomic_fetch_sub(&(w->l->stored_cache_count), 1, __ATOMIC_SEQ_CST);
    if (remaining_cache != 1) {
        CILK_ASSERT(w, remaining_cache == 0);
        Cilk_exception_handler(NULL);
    } else {
        w->l->regstate_cache[0].valid = false;
        w->l->regstate_cache[0].spawned_cache = false;
    }
#endif
#endif
}

#define STRINGIFY_2(a) #a
#define STRINGIFY(a) STRINGIFY_2(a)

// Enter a new Cilk function, i.e., a function that contains a cilk_spawn.  This
// function must be inlined for correctness.
__attribute__((always_inline)) void
__cilkrts_enter_frame() {
        // asm volatile("#ENTER_FRAME\n" : : : "rbx", "rdi", "rsi", "rdi", "r12", "r13", "r14", "r15",
        //                    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
        //                   "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
        //                   "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6",
        //                   "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"
        //                   );
#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    // if (!check_for_spawn_state_in_list((uintptr_t) __builtin_return_address(0), w)) printf("%p not spawning\n", __builtin_return_address(0));
    // printf("%p spawning\n", __builtin_return_address(0));
    *((volatile int*) & (w->spawn_counter)) += 1;
#endif

#if SIGNAL_FREE_BACKTRACKING
    if (__builtin_expect(w->waiting_thief, 0)) {
        __record_registers(w);
    }
#endif
    // __cilkrts_worker *w = __cilkrts_get_tls_worker();
    // // sf->flags = 0;
    // if (NULL == w) {
    //     cilkify(default_cilkrts);
    //     w = __cilkrts_get_tls_worker();
    //     w->g->root_closure->oldest_frame_rbp = get_current_rbp();
    //     w->g->root_closure->functional_rbp = get_current_rbp();
    // }
    // printf("H: %d T: %d -- enter_frame -- rbp:%p\n", w->head, w->tail, get_current_rbp());
    return;
}


__attribute__((always_inline)) void
__cilkrts_leave_frame() {
#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
    {
        __cilkrts_worker *w = __cilkrts_get_tls_worker();
        *((volatile int*) & (w->spawn_counter)) -= 1;
        CILK_ASSERT(w, w->spawn_counter >= -1);
// #if SIGNAL_FREE_BACKTRACKING
//         if (w->waiting_thief) {
//             __record_registers(w);
//         }
// #endif
    
    }
#endif
// #if CILK_BACKTRACK_CACHE_SIZE
//     {
//         register __cilkrts_worker *w = __cilkrts_get_tls_worker();
//         register int cache_index = CILK_BACKTRACK_CACHE_SIZE-(w->l->resumed_cache_count + 1); //We want the cache index taken from the top.
//         if (w->l->regstate_cache[cache_index].regstate.registers[UNW_X86_64_RBP] == (uintptr_t) get_current_rbp()) {
//             w->l->resumed_cache_count += 1;
//             register int reduced_cache_count = __atomic_fetch_sub(&(w->l->stored_cache_count), 1, __ATOMIC_SEQ_CST);
//             //printf("Resumed frame\n");
//             if (reduced_cache_count < 0) { //If this is negative, we're stealing something invalid - need to jump to runtime.
//                 Cilk_exception_handler(NULL);
//             }
//             CILK_ASSERT(w, w->l->regstate_cache[cache_index].valid);
//             w->l->regstate_cache[cache_index].valid = false;
//         } else {
//             //printf("Failed\n\trcc: %d\n\tregrbp: 0x%lx\n\tcurrpb: 0x%lx", stored_cache_count, w->l->regstate_cache[cache_index].regstate.registers[UNW_X86_64_RBP], (uintptr_t) get_current_rbp());
//         }
//     }
// #endif
}

// __attribute__((always_inline)) void
// __cilkrts_leave_frame_helper() {
//     __cilkrts_worker *w = get_tls_worker(sf);
//     cilkrts_alert(CFRAME, w, "__cilkrts_leave_frame_helper %p", (void *)sf);

//     CILK_ASSERT(w, CHECK_CILK_FRAME_MAGIC(w->g, sf));
//     CILK_ASSERT(w, sf->worker == __cilkrts_get_tls_worker());
//     // WHEN_CILK_DEBUG(sf->magic = ~CILK_STACKFRAME_MAGIC);

//     // Pop this frame off the cactus stack.  This logic used to be in
//     // __cilkrts_pop_frame, but has been manually inlined to avoid reloading the
//     // worker unnecessarily.
//     w->current_stack_frame = sf->call_parent;
//     sf->call_parent = NULL;

//     CILK_ASSERT(w, sf->flags & CILK_FRAME_DETACHED);

//     __cilkrts_stack_frame **tail =
//             atomic_load_explicit(&w->tail, memory_order_relaxed);
//     --tail;
//     /* The store of tail must precede the load of exc in global order.  See
//        comment in do_dekker_on. */
//     atomic_store_explicit(&w->tail, tail, memory_order_seq_cst);
//     __cilkrts_stack_frame **exc =
//             atomic_load_explicit(&w->exc, memory_order_seq_cst);
//     /* Currently no other modifications of flags are atomic so this one isn't
//        either.  If the thief wins it may run in parallel with the clear of
//        DETACHED.  Does it modify flags too? */
//     sf->flags &= ~CILK_FRAME_DETACHED;
//     if (__builtin_expect(exc > tail, 0)) {
//         Cilk_exception_handler(NULL);
//         // If Cilk_exception_handler returns this thread won the race and can
//         // return to the parent function.
//     }
//     // CILK_ASSERT(w, *(w->tail) == w->current_stack_frame);
// }

__attribute__((always_inline)) void
__cilkrts_pop_frame_self() {
// #if SIGNAL_FREE_BACKTRACKING
//     __cilkrts_worker *w = __cilkrts_get_tls_worker();
//     if (w->waiting_thief) {
//         __record_registers(w);
//     }
// #endif
    Cilk_exception_handler(NULL);
}

__attribute__((always_inline)) void
__cilk_parent_epilogue() {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    // cilkrts_alert(CFRAME, w, "__cilkrts_leave_frame %p", (void *)sf);

    // CILK_ASSERT(w, CHECK_CILK_FRAME_MAGIC(w->g, sf));
    // CILK_ASSERT(w, sf->worker == __cilkrts_get_tls_worker());
    // // WHEN_CILK_DEBUG(sf->magic = ~CILK_STACKFRAME_MAGIC);

    // // Pop this frame off the cactus stack.  This logic used to be in
    // // __cilkrts_pop_frame, but has been manually inlined to avoid reloading the
    // // worker unnecessarily.
    // w->current_stack_frame = sf->call_parent;
    // sf->call_parent = NULL;

    // // Check if sf is the final stack frame, and if so, terminate the Cilkified
    // // region.
    Closure * cl = w->g->deques[w->self].bottom;


    // // A detached frame would never need to call Cilk_set_return, which performs
    // // the return protocol of a full frame back to its parent when the full
    // // frame is called (not spawned).  A spawned full frame returning is done
    // // via a different protocol, which is triggered in Cilk_exception_handler.
    if (cl && cl->call_parent) { // if this frame corresponds to a called frame instead of a spawned one
        // #if SIGNAL_FREE_BACKTRACKING
        //     if (w->waiting_thief) {
        //         __record_registers(w);
        //     }
        // #endif
        w->returning_closure = cl;
        w->returning_closure_call_parent = cl->call_parent;
        CILK_ASSERT(w, cl->call_parent);
        CILK_ASSERT(w, cl->functional_rbp == get_current_rbp());
        CILK_ASSERT(w, w->spawn_counter == w->steal_counter);
        cilkrts_alert(RETURN, w,
                      "__cilkrts_leave_frame parent is call_parent!");
        // leaving a full frame; need to get the full frame of its call
        // parent back onto the deque
        Cilk_set_return(w);
    }
}

__attribute__((always_inline)) void
__cilk_self_epilogue() {
    __cilkrts_pop_frame_self();
}

// __attribute__((always_inline)) void
// __cilk_helper_epilogue() {
//     __cilkrts_leave_frame_helper(sf);
// }

__attribute__((always_inline))
void __cilkrts_enter_landingpad(int32_t sel) {
    // Don't do anything special during cleanups.
    if (sel == 0)
        return;
    CILK_ASSERT(__cilkrts_get_tls_worker(), 0); //Broken function
    // if (0 == __builtin_setjmp(sf->ctx))
    //     __cilkrts_cleanup_fiber(sf, sel);
}


/// Computes a grainsize for a cilk_for loop, using the following equation:
///
///     grainsize = min(2048, ceil(n / (8 * nworkers)))
#define __cilkrts_grainsize_fn_impl(NAME, INT_T)                               \
    __attribute__((always_inline)) INT_T NAME(INT_T n) {                       \
        INT_T small_loop_grainsize = n / (8 * cilkg_nproc);                    \
        if (small_loop_grainsize <= 1)                                         \
            return 1;                                                          \
        INT_T large_loop_grainsize = 2048;                                     \
        return large_loop_grainsize < small_loop_grainsize                     \
                   ? large_loop_grainsize                                      \
                   : small_loop_grainsize;                                     \
    }
#define __cilkrts_grainsize_fn(SZ)                                             \
    __cilkrts_grainsize_fn_impl(__cilkrts_cilk_for_grainsize_##SZ, uint##SZ##_t)

__attribute__((always_inline)) uint8_t
__cilkrts_cilk_for_grainsize_8(uint8_t n) {
    uint8_t small_loop_grainsize = n / (8 * cilkg_nproc);
    if (small_loop_grainsize <= 1)
        return 1;
    return small_loop_grainsize;
}

__cilkrts_grainsize_fn(16) __cilkrts_grainsize_fn(32) __cilkrts_grainsize_fn(64)
