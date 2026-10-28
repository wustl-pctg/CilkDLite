#ifndef TIMING_COUNT
#define TIMING_COUNT 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/cilk2c.h"
#include "../runtime/cilk2c_inlined.c"
#include "ktiming.h"

extern char __CILK_SPAWN_PREFIX__FAST_lbl_nqueens_1_call_start[];
extern char __CILK_SPAWN_PREFIX__SLOW_lbl_nqueens_1_cont[];
extern char __CILK_SPAWN_PREFIX__SLOW_lbl_nqueens_1_call_start[];
extern char __CILK_SPAWN_PREFIX__SLOW_lbl_nqueens_1_call_end[];
extern size_t ZERO;

__attribute__((constructor(200))) void register_spawns() {
    int errval = __cilkrts_register_spawn_state(
                     (uintptr_t) &__CILK_SPAWN_PREFIX__FAST_lbl_nqueens_1_call_start[0],
                     (uintptr_t) &__CILK_SPAWN_PREFIX__SLOW_lbl_nqueens_1_call_start[0],
                     (uintptr_t) &__CILK_SPAWN_PREFIX__SLOW_lbl_nqueens_1_call_end[0],
                     (uintptr_t) &__CILK_SPAWN_PREFIX__SLOW_lbl_nqueens_1_cont[0]);
    if (errval) printf("Error %d loading spawn states in fib.c\n", errval);
}
void __attribute__((weak)) dummy(void *p) { return; }

// int * count;

/*
 * nqueen  4 = 2
 * nqueen  5 = 10
 * nqueen  6 = 4
 * nqueen  7 = 40
 * nqueen  8 = 92
 * nqueen  9 = 352
 * nqueen 10 = 724
 * nqueen 11 = 2680
 * nqueen 12 = 14200
 * nqueen 13 = 73712
 * nqueen 14 = 365596
 * nqueen 15 = 2279184
 */

/*
 * <a> contains array of <n> queen positions.  Returns 1
 * if none of the queens conflict, and returns 0 otherwise.
 */
static int ok (int n, char *a) {

    int i, j;
    char p, q;

    for (i = 0; i < n; i++) {
        p = a[i];
        for (j = i + 1; j < n; j++) {
            q = a[j];
            if (q == p || q == p - (j - i) || q == p + (j - i))
                return 0;
        }
    }

    return 1;
}

static int nqueens(int n, int j, char *a) {

    char *b;
    int i;
    int *count;
    int solNum = 0;

    if (n == j) {
        return 1;
    }

    count = (int *) alloca(n * sizeof(int));
    (void) memset(count, 0, n * sizeof (int));

    __CILK_FUNC_PREFIX;

    for (i = 0; i < n; i++) {

        /***
         * Strictly speaking, this (alloca after spawn) is frowned
         * up on, but in this case, this is ok, because b returned by
         * alloca is only used in this iteration; later spawns don't
         * need to be able to access copies of b from previous iterations
         ***/
        b = (char *) alloca((j + 1) * sizeof (char));
        memcpy(b, a, j * sizeof (char));
        b[j] = i;

        if (ok (j + 1, b)) {

            /* count[i] = cilk_spawn nqueens(n, j + 1, b); */
            __CILK_FUNC_SPAWN(nqueens_lbl_1,
                              count[i] = nqueens(n, j + 1, b);
                             );
        }
    }
    /* cilk_sync */
    __CILK_FUNC_SYNC;

    for (i = 0; i < n; i++) {
        solNum += count[i];
    }

    __CILK_FUNC_SUFFIX;

    return solNum;
}


// int nqueens_zero(int n, int j, char *a) {
//     char *b;
//     int i;
//     int *count;
//     int solNum = 0;
//     if (n == j) {
//         return 1;
//     }

//     count = (int *) alloca(n * sizeof(int));
//     (void) memset(count, 0, n * sizeof (int));

//     asm goto volatile("": : : : nqueens_slow_clone);
//     for (i = 0; i < n; i++) {

//         /***
//          * Strictly speaking, this (alloca after spawn) is frowned
//          * up on, but in this case, this is ok, because b returned by
//          * alloca is only used in this iteration; later spawns don't
//          * need to be able to access copies of b from previous iterations
//          ***/
//         b = (char *) alloca((j + 1) * sizeof (char));
//         memcpy(b, a, j * sizeof (char));
//         b[j] = i;

//         if (ok (j + 1, b)) {

//             /* count[i] = cilk_spawn nqueens(n, j + 1, b); */
//             asm goto volatile(
//                 "mov %0, %%edi \n\t"
//                 "mov %1, %%esi \n\t"
//                 "add $1, %%esi \n\t"
//                 "mov %2, %%rdx \n\t"
//                 "call %P3\n\t"
//                 "__CILK_SPAWN_PREFIX__FAST_lbl_nqueens_1_call_start:\n\t"
//                 :
//                 : "r" (n), "r"(j), "r"(b), "i"(nqueens_zero)
//                 : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
//                 "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
//                 "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
//                 "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6",
//                 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"
//                 : SLOW_lbl_nqueens_1, nqueens_slow_clone_3);
//             asm goto volatile(
//                 "mov %%eax, %0\n\t"
//                 : "=m"(count[i])
//                 :
//                 : "rax"
//                 : SLOW_lbl_nqueens_1
//             );
//         }
//     }
//     /* cilk_sync */

//     for (i = 0; i < n; i++) {
//         solNum += count[i];
//     }


//     return solNum;

// nqueens_slow_clone:

//     for (i = 0; i < n; i++) {

//         /***
//          * Strictly speaking, this (alloca after spawn) is frowned
//          * up on, but in this case, this is ok, because b returned by
//          * alloca is only used in this iteration; later spawns don't
//          * need to be able to access copies of b from previous iterations
//          ***/
        
//         b = (char *) alloca((j + 1) * sizeof (char));
//         memcpy(b, a, j * sizeof (char));
//         b[j] = i;

//         if (ok (j + 1, b)) {
//             __CILK_FUNC_SPAWN(SLOW_lbl_nqueens_1,
//                               asm goto volatile(
//                                   "mov %1, %%edi\n\t"
//                                   "mov %2, %%esi \n\t"
//                                   "add $1, %%esi \n\t"
//                                   "mov %3, %%rdx\n\t"
//                                   "call %P4\n\t"
//                                   "__CILK_SPAWN_PREFIX__SLOW_lbl_nqueens_1_call_start:\n\t"
//                                   "mov %%eax,  %0\n\t"
//                                   "jmp %l[SLOW_lbl_nqueens_1]\n\t"
//                                   "__CILK_SPAWN_PREFIX__SLOW_lbl_nqueens_1_call_end:\n\t"
//                                   : "=m"(count[i])
//                                   : "r" (n), "r"(j), "r"(b), "i"(nqueens_zero)
//                                   : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
//                                   "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
//                                   "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
//                                   "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6",
//                                   "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"
//                                   : SLOW_lbl_nqueens_1, nqueens_slow_clone_3
//                               );
//                               nqueens_slow_clone_3:
//                               asm goto volatile(
//                                   "mov %%eax, %0\n\t"
//                                   : "=m"(count[i])
//                                   :
//                                   : "rax"
//                                   : SLOW_lbl_nqueens_1
//                               );
//                              );
//         }
//     }
//     /* cilk_sync */
//     __CILK_FUNC_SYNC;

//     for (i = 0; i < n; i++) {
//         solNum += count[i];
//     }

//     __CILK_FUNC_SUFFIX;

//     return solNum;



// }

int main(int argc, char *argv[]) {

    int n = 13;
    char *a;
    int res;
    cilkify(default_cilkrts);
    __cilkrts_worker * w = __cilkrts_get_tls_worker();

    if (argc < 2) {
        fprintf (stderr, "Usage: %s <n>\n", argv[0]);
        fprintf (stderr, "Use default board size, n = 13.\n");
        exit(0);
    } else {
        n = atoi (argv[1]);
        printf ("Running %s with n = %d.\n", argv[0], n);
    }

    a = (char *) alloca (n * sizeof (char));
    res = 0;

#if TIMING_COUNT
    clockmark_t begin, end;
    uint64_t elapsed[TIMING_COUNT];

    for (int i = 0; i < TIMING_COUNT; i++) {
        begin = ktiming_getmark();
        res = nqueens_zero(n, 0, a);
        end = ktiming_getmark();
        elapsed[i] = ktiming_diff_nsec(&begin, &end);
    }
    print_runtime(elapsed, TIMING_COUNT);
#else
    res = nqueens_zero(n, 0, a);
#endif

    if (res == 0) {
        printf("No solution found.\n");
    } else {
        printf("Total number of solutions : %d\n", res);
    }
    uncilkify(default_cilkrts);
    return 0;
}
