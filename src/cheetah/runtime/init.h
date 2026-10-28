#ifndef _CILK_INIT_H
#define _CILK_INIT_H

#include "cilk-internal.h"

void __cilkrts_internal_invoke_cilkified_root(global_state *g);
void __cilkrts_internal_exit_cilkified_root(global_state *g);

// Used by Cilksan to set nworkers to 1 and force reduction
void __cilkrts_internal_set_nworkers(unsigned int nworkers);
void __cilkrts_internal_set_force_reduce(unsigned int force_reduce);

#endif /* _CILK_INIT_H */
