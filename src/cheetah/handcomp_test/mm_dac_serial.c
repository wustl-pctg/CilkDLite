#include <stdio.h>
#include <stdlib.h>

#include "../runtime/cilk2c.h"
#include "../runtime/cilk2c_inlined.c"
#include "ktiming.h"
#include "getoptions.h"

#ifndef TIMING_COUNT
#define TIMING_COUNT 1
#endif

#define CHECK_RESULT 1
#define THRESHOLD 16
#define TRUE 1
#define FALSE 0

#define DEFINE_SPAWN_LABELS(label) \
    extern char __CILK_SPAWN_PREFIX__FAST_ ## label ## _call_start[];\
    extern char __CILK_SPAWN_PREFIX__SLOW_ ## label ## _call_start[];\
    extern char __CILK_SPAWN_PREFIX__SLOW_ ## label ## _call_end[];\
    extern char __CILK_SPAWN_PREFIX__SLOW_ ## label ## _cont[];

#define INITIALIZE_SPAWN_LABELS(label) \
    errval = __cilkrts_register_spawn_state( \
                     (uintptr_t) &__CILK_SPAWN_PREFIX__FAST_ ## label ## _call_start[0], \
                     (uintptr_t) &__CILK_SPAWN_PREFIX__SLOW_ ## label ## _call_start[0], \
                     (uintptr_t) &__CILK_SPAWN_PREFIX__SLOW_ ## label ## _call_end[0], \
                     (uintptr_t) &__CILK_SPAWN_PREFIX__SLOW_ ## label ## _cont[0]); \
    if (errval) printf("Error %d loading spawn states with label " #label "in mm_dac.c\n", errval);

extern size_t ZERO;
void __attribute__((weak)) dummy(void *p) { return; }

unsigned int randomSeed = 1;

DEFINE_SPAWN_LABELS(mm_dac_lbl_1)
DEFINE_SPAWN_LABELS(mm_dac_lbl_2)
DEFINE_SPAWN_LABELS(mm_dac_lbl_3)
DEFINE_SPAWN_LABELS(mm_dac_lbl_4)
DEFINE_SPAWN_LABELS(mm_dac_lbl_5)
DEFINE_SPAWN_LABELS(mm_dac_lbl_6)

__attribute__((constructor(200))) void register_spawns() {
    int errval;
    INITIALIZE_SPAWN_LABELS(mm_dac_lbl_1)
    INITIALIZE_SPAWN_LABELS(mm_dac_lbl_2)
    INITIALIZE_SPAWN_LABELS(mm_dac_lbl_3)
    INITIALIZE_SPAWN_LABELS(mm_dac_lbl_4)
    INITIALIZE_SPAWN_LABELS(mm_dac_lbl_5)
    INITIALIZE_SPAWN_LABELS(mm_dac_lbl_6)
}


static void mm_dac_serial(int *C, const int *A, const int *B, int n, int length) {

    if(length < THRESHOLD) {
        // Use a loop for small matrices
        for (int i = 0; i < length; i++)
            for (int j = 0; j < length; j++)
                for (int k = 0; k < length; k++)
                    C[i * n + j] += A[i * n + k] * B[k * n + j];
        return;
    }

    // Partition the matrices
    int mid = length >> 1;

    int *C00 = C;
    int *C01 = C + mid;
    int *C10 = C + n * mid;
    int *C11 = C + n * mid + mid;

    int const *A00 = A;
    int const *A01 = A + mid;
    int const *A10 = A + n * mid;
    int const *A11 = A + n * mid + mid;

    int const *B00 = B;
    int const *B01 = B + mid;
    int const *B10 = B + n * mid;
    int const *B11 = B + n * mid + mid;

    mm_dac_serial(C00, A00, B00, n, mid);
    mm_dac_serial(C01, A00, B01, n, mid);
    mm_dac_serial(C10, A10, B00, n, mid);
    mm_dac_serial(C11, A10, B01, n, mid);

    mm_dac_serial(C00, A01, B10, n, mid);
    mm_dac_serial(C01, A01, B11, n, mid);
    mm_dac_serial(C10, A11, B10, n, mid);
    mm_dac_serial(C11, A11, B11, n, mid);
}
#define MM_DAC_FAST_CLONE(label, arg1, arg2, arg3, arg4, arg5) asm volatile goto (\
                                                                                "mov %0, %%rdi \t\n" \
                                                                                "mov %1, %%rsi \t\n" \
                                                                                "mov %2, %%rdx \t\n" \
                                                                                "mov %3, %%ecx \t\n" \
                                                                                "mov %4, %%r8d  \t\n" \
                                                                                "call %P5 \n\t"\
                                                                                "__CILK_SPAWN_PREFIX__FAST_" #label "_call_start: \n\t" \
                                                                                : \
                                                                                : "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "i"(mm_dac) \
                                                                                :   "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", \
                                                                                    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", \
                                                                                    "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", \
                                                                                    "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6", \
                                                                                    "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)" \
                                                                                : SLOW_ ## label \
                                                                                );



#define MM_DAC_SLOW_CLONE(label, arg1, arg2, arg3, arg4, arg5)  asm volatile goto (\
                                                                                "mov %0, %%rdi \t\n" \
                                                                                "mov %1, %%rsi \t\n" \
                                                                                "mov %2, %%rdx \t\n" \
                                                                                "mov %3, %%ecx \t\n" \
                                                                                "mov %4, %%r8d  \t\n" \
                                                                                "call %P5 \n\t"\
                                                                                "__CILK_SPAWN_PREFIX__SLOW_" #label "_call_start: \n\t" \
                                                                                "jmp %l[SLOW_" #label "] \n\t"\
                                                                                "__CILK_SPAWN_PREFIX__SLOW_" #label "_call_end: \n\t" \
                                                                                : \
                                                                                : "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "i"(mm_dac) \
                                                                                :   "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", \
                                                                                    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", \
                                                                                    "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", \
                                                                                    "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6", \
                                                                                    "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)" \
                                                                                : SLOW_ ## label \
                                                                                ); \
                                                                                __cilk_self_epilogue();\
                                                                                asm volatile("__CILK_SPAWN_PREFIX__SLOW_" #label "_cont:"); \
                                                                                SLOW_ ## label: \
                                                                                __asm__ volatile("" : : :   "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",\
                                                                                    "xmm0","xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",\
                                                                                    "xmm8","xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",\
                                                                                    "mm0","mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6",\
                                                                                    "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");\


/**
 * Recursive implementation of matrix multiply.
 * This code will not work on non-square matrices.
 * Effect: Compute C+=A*B,
 * where C, A, and B are the starting addresses of submatrices with dimension
 * length x length.  Argument n is the original input matrix length.
 **/
static void mm_dac(int *C,  const int *A,  const int *B, int n, int length) {

    if (length < THRESHOLD) {
        // Use a loop for small matrices
        for (int i = 0; i < length; i++)
            for (int j = 0; j < length; j++)
                for (int k = 0; k < length; k++)
                    C[i * n + j] += A[i * n + k] * B[k * n + j];
        return;
    }

    // Partition the matrices
    int mid = length >> 1;

    int *C00 = C;
    int *C01 = C + mid;
    int *C10 = C + n * mid;
    int *C11 = C + n * mid + mid;

    int const *A00 = A;
    int const *A01 = A + mid;
    int const *A10 = A + n * mid;
    int const *A11 = A + n * mid + mid;

    int const *B00 = B;
    int const *B01 = B + mid;
    int const *B10 = B + n * mid;
    int const *B11 = B + n * mid + mid;

    asm goto volatile("": : : : mm_dac_slow_clone);

    MM_DAC_FAST_CLONE(mm_dac_lbl_1, C00, A00, B00, n, mid);

    MM_DAC_FAST_CLONE(mm_dac_lbl_2, C01, A00, B01, n, mid);

    MM_DAC_FAST_CLONE(mm_dac_lbl_3, C10, A10, B00, n, mid);

    mm_dac(C11, A10, B01, n, mid);


    MM_DAC_FAST_CLONE(mm_dac_lbl_4, C00, A01, B10, n, mid);

    MM_DAC_FAST_CLONE(mm_dac_lbl_5, C01, A01, B11, n, mid);

    MM_DAC_FAST_CLONE(mm_dac_lbl_6, C10, A11, B10, n, mid);

    mm_dac(C11, A11, B11, n, mid);

    return;

mm_dac_slow_clone:

    MM_DAC_SLOW_CLONE(mm_dac_lbl_1, C00, A00, B00, n, mid);

    MM_DAC_SLOW_CLONE(mm_dac_lbl_2, C01, A00, B01, n, mid);

    MM_DAC_SLOW_CLONE(mm_dac_lbl_3, C10, A10, B00, n, mid);

    mm_dac(C11, A10, B01, n, mid);

    __CILK_FUNC_SYNC;

    MM_DAC_SLOW_CLONE(mm_dac_lbl_4, C00, A01, B10, n, mid);

    MM_DAC_SLOW_CLONE(mm_dac_lbl_5, C01, A01, B11, n, mid);

    MM_DAC_SLOW_CLONE(mm_dac_lbl_6, C10, A11, B10, n, mid);

    mm_dac(C11, A11, B11, n, mid);

    /* cilk_sync */
    __CILK_FUNC_SYNC;

    __CILK_FUNC_SUFFIX;
}


static void rand_matrix(int *dest, int n) {
    for (int i = 0; i < n * n; ++i)
        dest[i] = rand_r(&randomSeed) & 0xff;
}

static void zero_matrix(int *dest, int n) {
    for (int i = 0; i < n * n; ++i)
        dest[i] = 0;
}

#if CHECK_RESULT
static int are_equal_matrices(const int *a, const int *b, int n) {
    for (int i = 0; i < n * n; ++i)
        if (a[i] != b[i])
            return FALSE;
    return TRUE;
}
#endif

static void test_mm(int n, int check) {
    clockmark_t begin, end;
    uint64_t running_time[TIMING_COUNT];

    int *A = (int *) malloc(sizeof(int) * (n * n));
    int *B = (int *) malloc(sizeof(int) * (n * n));
    int *C = (int *) malloc(sizeof(int) * (n * n));

    rand_matrix(A, n);
    rand_matrix(B, n);

    for (int i = 0; i < TIMING_COUNT; i++) {
        zero_matrix(C, n);
        begin = ktiming_getmark();
        mm_dac(C, A, B, n, n);
        end = ktiming_getmark();
        running_time[i] = ktiming_diff_nsec(&begin, &end);
    }
    print_runtime(running_time, TIMING_COUNT);

    if (check) {
        fprintf(stderr, "Checking result ...\n");
        int * Cs = (int*) malloc(sizeof(int) * (n * n));
        zero_matrix(Cs, n);
        mm_dac_serial(Cs, A, B, n, n);
        if (!are_equal_matrices(C, Cs, n)) {
            fprintf(stderr, "MM_dac test FAILED.\n");
        } else {
            fprintf(stderr, "MM_dac test passed.\n");
        }
        free(Cs);
    }

    free(C);
    free(B);
    free(A);
}

// return true iff n = 2^k (or 0).
static int is_power_of_2(int n) {
    return (n & (n - 1)) == 0;
}


const char *specifiers[] = {"-n", "-c", "-h", 0};
int opt_types[] = {LONGARG, BOOLARG, BOOLARG, 0};

int main(int argc, char *argv[]) {
    cilkify(default_cilkrts);
    __cilkrts_worker * w = __cilkrts_get_tls_worker();
    w->g->root_closure->oldest_frame_rbp = get_current_rbp();
    w->g->root_closure->functional_rbp = get_current_rbp();

    long size;
    int help, check;

    /* standard benchmark options */
    size = 1024;
    check = 0;
    help = 0;

    get_options(argc, argv, specifiers, opt_types, &size, &check, &help);

    if (help) {
        fprintf(stderr, "Usage: mm_dac [cilk options] -n <size> [-c|-h]\n");
        fprintf(stderr, "   when -c is set, check result against sequential MM (slow).\n");
        fprintf(stderr, "   when -h is set, print this message and quit.\n");
        exit(0);
    }

    if (!is_power_of_2(size)) {
        fprintf(stderr, "Input size must be a power of 2 \n");
        exit(1);
    }
    test_mm(size, check);
    uncilkify(default_cilkrts);

    return 0;
}
