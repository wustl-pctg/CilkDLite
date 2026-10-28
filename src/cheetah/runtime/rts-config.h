#ifndef _CONFIG_H
#define _CONFIG_H

/* Functions defined in the library and visible outside the library. */
#ifndef CHEETAH_API
#if defined __BSD__ || defined __linux__ /* really, if using ELF */
#define CHEETAH_API __attribute((visibility("protected")))
#else
#define CHEETAH_API
#endif
#endif
/* Functions defined in the library and not visible outside the library. */
#ifndef CHEETAH_INTERNAL
#define CHEETAH_INTERNAL __attribute((visibility("hidden")))
#endif

#ifndef CHEETAH_INTERNAL_NORETURN
#define CHEETAH_INTERNAL_NORETURN __attribute((noreturn, visibility("hidden")))
#endif

#ifndef CHEETAH_NORETURN
#define CHEETAH_NORETURN __attribute((noreturn))
#endif
#define __CILKRTS_VERSION 0x0

#define __CILKRTS_ABI_VERSION 3

#ifndef CILK_DEBUG
#define CILK_DEBUG 0
#endif

#ifndef CILK_ENABLE_ASAN_HOOKS
#define CILK_ENABLE_ASAN_HOOKS 0
#endif

#ifndef CILK_STATS
#define CILK_STATS 0
#endif

#ifndef CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
#define  CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC 1
#endif

#define CILK_BACKTRACK_ENABLE_SLOW_SPAWN 0

#ifndef CILK_BACKTRACK_CACHE_SIZE
#define CILK_BACKTRACK_CACHE_SIZE 0
#endif

#ifndef SIGNAL_FREE_BACKTRACKING
#define SIGNAL_FREE_BACKTRACKING 0
#endif

#ifndef CILK_BACKTRACK_STACK_WALKER
#define CILK_BACKTRACK_STACK_WALKER 1
#endif

#define CILK_BACKTRACK_CACHELESS_STEAL_BUFFER_SIZE 0


#define SPAWN_STATE_HASHMAP 1
#define SPAWN_STATE_LIST_MAX_LENGTH 2000

#define BOSS_THIEF 1

#define CILK_CACHE_LINE 64

#define PROC_SPEED_IN_GHZ 2.2

#if defined __linux__
#define CILK_PAGE_SIZE 0 /* page size not available at compile time */
#elif defined __APPLE__
#define CILK_PAGE_SIZE 4096 /* Apple implies x86 or ARM */
#else
#include <machine/param.h>
#endif



#define backtrack_jmp 1
#if backtrack_jmp
#define __builtin_setjmp(ctx) backtrack_setjmp(&ctx)
#define __builtin_longjmp(ctx, val) backtrack_longjmp(&ctx, val)
#define jmpbuf __cilkrts_regstate
#endif

#define MIN_NUM_PAGES_PER_STACK 4
#define MAX_NUM_PAGES_PER_STACK 2000

/* The largest known stack alignment requirement is for AVX-512
   which may access memory in aligned 64 byte units. */
#define MAX_STACK_ALIGN 64

#define DEFAULT_NPROC 0 // 0 for # of cores available
#define DEFAULT_DEQ_DEPTH 1024
#define DEFAULT_STACK_SIZE 0x100000 // 1 MBytes
#define DEFAULT_FIBER_POOL_CAP 3  // initial per-worker fiber pool capacity
#define DEFAULT_REDUCER_LIMIT 1024
#define DEFAULT_FORCE_REDUCE 0 // do not self steal to force reduce

#define MAX_CALLBACKS 32 // Maximum number of init or exit callbacks
#define USING_UNWIND_STEALS
#define STEAL_FAIL_DELAY 500000
#ifdef USING_UNWIND_STEALS
#define USING_UNWIND_STEAL_STRUCTS
#endif
#endif                   // _CONFIG_H
