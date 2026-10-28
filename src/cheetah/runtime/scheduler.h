#ifndef _RSCHED_H
#define _RSCHED_H

#include "cilk-internal.h"
#include "closure.h"

#define SYNC_READY 0
#define SYNC_NOT_READY 1

#define EXCEPTION_INFINITY (int)(-1)

CHEETAH_INTERNAL void do_what_it_says_boss(__cilkrts_worker *w, Closure *t);

CHEETAH_INTERNAL void __cilkrts_init_tls_variables();
CHEETAH_INTERNAL void __cilkrts_set_tls_worker(__cilkrts_worker *w);

CHEETAH_INTERNAL int Cilk_sync(__cilkrts_worker *const ws);

void Cilk_set_return(__cilkrts_worker *const ws);
void Cilk_exception_handler(char *exn);

CHEETAH_INTERNAL_NORETURN void longjmp_to_runtime(__cilkrts_worker *w);
CHEETAH_INTERNAL void worker_scheduler(__cilkrts_worker *ws);
CHEETAH_INTERNAL void *scheduler_thread_proc(void *arg);
CHEETAH_INTERNAL Closure* extract_top_spawning_closure(__cilkrts_worker *const w,
                                             __cilkrts_worker *const victim_w,
                                             Closure *cl,
                                             uintptr_t rbp,
                                             uintptr_t child_rbp);

CHEETAH_INTERNAL void promote_own_deque(__cilkrts_worker *w);

#endif
