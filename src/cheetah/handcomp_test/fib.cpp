#include <stdio.h>
#include <stdlib.h>
#include <stdexcept>
extern "C" {
    #include "../runtime/cilk2c.h"
    #include "../runtime/cilk2c_inlined.c"
    #include "ktiming.h"
}
#ifndef TIMING_COUNT 
#define TIMING_COUNT 1 
#endif

using namespace std;
/* 
 * fib 39: 63245986
 * fib 40: 102334155
 * fib 41: 165580141 
 * fib 42: 267914296
   
int fib(int n) {
    int x, y, _tmp;

    if(n < 2) {
        return n;
    }
    
    x = cilk_spawn fib(n - 1);
    y = fib(n - 2);
    cilk_sync;

    return x+y;
}
*/

extern size_t ZERO;
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

int fib(int n) {
    // int dummy_throw;
    // __asm__ volatile("movl $0, %0" : "=r"(dummy_throw));
    // if (dummy_throw) throw 7;   
    int x = 0;
    int y = 0;
    int _tmp;
    if(n < 2)
        return n;
    __CILK_FUNC_PREFIX;    /* x = spawn fib(n-1) */
    __CILK_FUNC_SPAWN(lbl_fib_1,
        try {
            *(volatile int *)&x = fib(n-1);            
        } catch (...) {
            printf("Resumption code here\n");
        }
    );
    y = fib(n-2);
    /* cilk_sync */
    __CILK_FUNC_SYNC;
    _tmp = x + y;

    __CILK_FUNC_SUFFIX;

    return _tmp;
}

int main(int argc, char * args[]) {
    int i;
    int n, res;
    clockmark_t begin, end; 
    uint64_t running_time[TIMING_COUNT];
    if(argc != 2) {
        fprintf(stderr, "Usage: fib [<cilk-options>] <n>\n");
        exit(1);
    }
    
    n = atoi(args[1]);

    for(i = 0; i < TIMING_COUNT; i++) {
        begin = ktiming_getmark();
        res = fib(n);
        end = ktiming_getmark();
        running_time[i] = ktiming_diff_nsec(&begin, &end);
    }
    printf("Result: %d\n", res);
    print_runtime(running_time, TIMING_COUNT); 

    return 0;
}
