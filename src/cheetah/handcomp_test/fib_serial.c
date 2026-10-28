#include <stdio.h>
#include <stdlib.h>

#include "../runtime/cilk2c.h"
#include "../runtime/cilk2c_inlined.c"
#include "ktiming.h"

#ifndef TIMING_COUNT
#define TIMING_COUNT 1
#endif

/*
 * fib 39: 63245986
 * fib 40: 102334155
 * fib 41: 165580141
 * fib 42: 267914296
*/


int fib(int n) {
    int x, y, _tmp;

    if(n < 2) {
        return n;
    }

    x = fib(n - 1);
    y = fib(n - 2);

    return x+y;
}

extern char __CILK_SPAWN_PREFIX__FAST_lbl_fib_1_cont[];
extern char __CILK_SPAWN_PREFIX__FAST_lbl_fib_1_call_start[];
extern char __CILK_SPAWN_PREFIX__FAST_lbl_fib_1_call_end[];
extern char __CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_cont[];
extern char __CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_call_start[];
extern char __CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_call_end[];
extern size_t ZERO;

__attribute__((constructor(200))) void register_spawns() {
    int errval = __cilkrts_register_spawn_state(
                     (uintptr_t) &__CILK_SPAWN_PREFIX__FAST_lbl_fib_1_call_start[0],
                     (uintptr_t) &__CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_call_start[0],
                     (uintptr_t) &__CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_call_end[0],
                     (uintptr_t) &__CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_cont[0]);
    if (errval) printf("Error %d loading spawn states in fib.c\n", errval);
}
void __attribute__((weak)) dummy(void *p) { return; }


// int fib(int n) {
//     int x = 0;
//     int y, _tmp;
//     if(n < 2)
//         return n;


//     dummy(alloca(ZERO));
//     __cilkrts_stack_frame sf;
//     __cilkrts_enter_frame(&sf);


//     /* x = spawn fib(n-1) */


//     sf.pc = &&DO_WHEN_STOLEN;
//     __cilk_prepare_spawn(&sf);
//     __cilkrts_detach_self(&sf);
//     *(volatile int *)&x = fib(n-1);
//     __cilk_self_epilogue(&sf);
//     DO_WHEN_STOLEN:
//     sf.flags &= ~CILK_FRAME_SPAWNER;


//     y = fib(n - 2);

//     /* cilk_sync */
//     __cilk_sync_nothrow(&sf);
//     _tmp = x + y;

//     __cilk_parent_epilogue(&sf);

//     return _tmp;
// }

int fib_parallel(int n) {
    int x = 0;
    int y = 0;
    int _tmp;
    if (n < 2)
        return n;
    asm goto volatile("": : : : fib_slow_clone);
    asm goto volatile(
        "lea -0x1(%1), %%edi\n\t"
        "call %P2\n\t"
        "__CILK_SPAWN_PREFIX__FAST_lbl_fib_1_call_start:\n\t"
        "mov %%eax, %0\n\t"
        : "=r"(x)
        : "r" (n), "i"(fib)
        :   "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
        "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
        "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6",
        "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"
        : SLOW_lbl_fib_1
    );
    y = fib(n - 2);
    /* cilk_sync */
    _tmp = x + y;
    return _tmp;

fib_slow_clone:
    //__CILK_FUNC_PREFIX;
    __CILK_FUNC_SPAWN(SLOW_lbl_fib_1,
                      asm goto volatile(
                          "lea -0x1(%1), %%edi\n\t"
                          "call %P2\n\t"
                          "__CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_call_start:\n\t"
                          "mov %%eax, %0\n\t"
                          "jmp %l[SLOW_lbl_fib_1]\n\t"
                          "__CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_call_end:\n\t"
                          "mov %%eax, %0 \n\t"
                          : "=m"(x)
                          : "r" (n), "i"(fib)
                          :   "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
                          "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                          "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
                          "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6",
                          "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"
                          : SLOW_lbl_fib_1
                      );
    );
    y = fib(n - 2);
    /* cilk_sync */
    __CILK_FUNC_SYNC;
    _tmp = x + y;

    __CILK_FUNC_SUFFIX;

    return _tmp;
}

int main(int argc, char * args[]) {
    // cilkify(default_cilkrts);
    // __cilkrts_worker * w = __cilkrts_get_tls_worker();
    // w->g->root_closure->oldest_frame_rbp = get_current_rbp();
    // w->g->root_closure->functional_rbp = get_current_rbp();
    int i;
    int n, res;
    clockmark_t begin, end;
    uint64_t running_time[TIMING_COUNT];
    // printf("FAST_lbl_fib_1_call_start: %p\n", &__CILK_SPAWN_PREFIX__FAST_lbl_fib_1_call_start[0]);

    // printf("SLOW_lbl_fib_1_cont: %p\n", &__CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_cont[0]);
    // printf("SLOW_lbl_fib_1_call_start: %p\n", &__CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_call_start[0]);
    // printf("SLOW_lbl_fib_1_call_end: %p\n", &__CILK_SPAWN_PREFIX__SLOW_lbl_fib_1_call_end[0]);

    if (argc != 2) {
        fprintf(stderr, "Usage: fib [<cilk-options>] <n>\n");
        exit(1);
    }

    n = atoi(args[1]);

    for (i = 0; i < TIMING_COUNT; i++) {
        begin = ktiming_getmark();
        res = fib(n);
        end = ktiming_getmark();
        running_time[i] = ktiming_diff_nsec(&begin, &end);
    }
    printf("Result: %d\n", res);

    print_runtime(running_time, TIMING_COUNT);
    // uncilkify(default_cilkrts);
    return 0;
}
