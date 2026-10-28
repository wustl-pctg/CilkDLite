#include <assert.h>
#include <pthread.h>
#ifdef __linux__
#include <sched.h>
#endif
#include <stdio.h>
#include <string.h>
#include <unwind.h>

#include "cilk-internal.h"
#include "closure.h"
#include "fiber.h"
#include "global.h"
#include "jmpbuf.h"
#include "local.h"
#include "readydeque.h"
#include "scheduler.h"
#include "worker_coord.h"

#include <signal.h>
#include "backtrack.h"

#include "reducer_impl.h"
#include <immintrin.h>

__thread __cilkrts_worker *tls_worker = NULL;
__thread bool is_boss_thread = false;

// ==============================================
// Misc. helper functions
// ==============================================

/***********************************************************
 * Internal random number generator.
 ***********************************************************/
static void rts_srand(__cilkrts_worker *const w, unsigned int seed) {
    w->l->rand_next = seed;
}

static unsigned int rts_rand(local_state *l) {
    l->rand_next = l->rand_next * 1103515245 + 12345;
    return (l->rand_next >> 16);
}

static void worker_change_state(__cilkrts_worker *w,
                                enum __cilkrts_worker_state s) {
    /* TODO: Update statistics based on state change. */
    CILK_ASSERT(w, w->l->state != s);
    w->l->state = s;
#if 0
    /* This is a valuable assertion but there is no way to make it
       work.  The reducer map is not moved out of the worker until
       long after scheduler state has been entered.  */
    if (s != WORKER_RUN) {
        CILK_ASSERT(w, w->reducer_map == NULL);
    }
#endif
}


/* Unused for now but may be helpful later
static void signal_immediate_exception_to_all(__cilkrts_worker *const w) {
    int i, active_size = w->g->nworkers;
    __cilkrts_worker *curr_w;

    for(i=0; i<active_size; i++) {
        curr_w = w->g->workers[i];
        curr_w->exc = EXCEPTION_INFINITY;
    }
    // make sure the exception is visible, before we continue
    Cilk_fence();
}
*/

static void setup_for_execution(__cilkrts_worker *w, Closure *t) {
    cilkrts_alert(SCHED, w, "(setup_for_execution) closure %p", (void *)t);
    Closure_set_status(w, t, CLOSURE_RUNNING);

    /* push the first frame on the current_stack_frame */
    w->current_closure = t;
}

// ANGE: When this is called, either a) a worker is about to pass a sync (though
// not on the right fiber), or b) a worker just performed a provably good steal
// successfully
// JFC: This is called from
// worker_scheduler -> ... -> Closure_return -> provably_good_steal_maybe
// user code -> __cilkrts_sync -> Cilk_sync
static void setup_for_sync(__cilkrts_worker *w, Closure *t) {

    Closure_assert_ownership(w, t);
    // ANGE: this must be true since in case a) we would have freed it in
    // Cilk_sync, or in case b) we would have freed it when we first returned to
    // the runtime before doing the provably good steal.
    CILK_ASSERT(w, w->l->fiber_to_free == NULL);
    CILK_ASSERT(w, t->fiber != t->fiber_child);

    if (t->simulated_stolen == false) {
        // ANGE: note that in case a) this fiber won't get freed for awhile,
        // since we will longjmp back to the original function's fiber and
        // never go back to the runtime; we will only free it either once
        // when we get back to the runtime or when we encounter a case
        // where we need to.
        w->l->fiber_to_free = t->fiber;
        t->fiber = t->fiber_child;
        t->fiber_child = NULL;
    }
    CILK_ASSERT(w, t->fiber);
    // __cilkrts_alert(STEAL | ALERT_FIBER, w,
    //         "(setup_for_sync) set t %p and t->fiber %p", (void *)t,
    //         (void *)t->fiber);
    __cilkrts_set_synced(t);
#if backtrack_jmp
    SP(t) = (uintptr_t) t->orig_rsp;
#else
    SP(t) = (void *)t->orig_rsp;
#endif
    t->orig_rsp = NULL; // unset once we have sync-ed
}

// ==============================================
// TLS related functions
// ==============================================
static pthread_key_t worker_key;

CHEETAH_INTERNAL void __cilkrts_init_tls_variables() {
    int status = pthread_key_create(&worker_key, NULL);
    USE_UNUSED(status);
    CILK_ASSERT_G(status == 0);
}

CHEETAH_INTERNAL void __cilkrts_set_tls_worker(__cilkrts_worker *w) {
    tls_worker = w;
}

// ==============================================
// Closure return protocol related functions
// ==============================================

/* Doing an "unconditional steal" to steal back the call parent closure */
static Closure *setup_call_parent_resumption(__cilkrts_worker *const w,
        Closure *t) {

    deque_assert_ownership(w, w->self);
    Closure_assert_ownership(w, t);

    CILK_ASSERT_POINTER_EQUAL(w, w, __cilkrts_get_tls_worker());
    CILK_ASSERT(w, t->stolen_flag == true);

    Closure_change_status(w, t, CLOSURE_SUSPENDED, CLOSURE_RUNNING);

    return t;
}

void Cilk_set_return(__cilkrts_worker *const w) {

    Closure *t;

    cilkrts_alert(RETURN, w, "(Cilk_set_return)");

    while (!deque_trylock(w, w->self)) {
        #if SIGNAL_FREE_BACKTRACKING
            if (w->waiting_thief) {
                __atomic_store_n(&(w->l->context.fail_flag), 1, __ATOMIC_SEQ_CST);
                __atomic_store_n(&(w->l->context.poll_flag), 1, __ATOMIC_SEQ_CST);
                while (w->l->context.poll_flag) {
                }            
            }
        #endif
    }
    t = deque_peek_bottom(w, w->self);
    Closure_lock(w, t);

    CILK_ASSERT(w, t->status == CLOSURE_RUNNING);
    CILK_ASSERT(w, Closure_has_children(t) == 0);

    // all rmaps from child or right sibling must have been reduced
    CILK_ASSERT(w, t->child_rmap == (cilkred_map *)NULL &&
                t->right_rmap == (cilkred_map *)NULL);
    CILK_ASSERT(w, t->call_parent);
    CILK_ASSERT(w, t->spawn_parent == NULL);
    //CILK_ASSERT(w, (t->frame->flags & CILK_FRAME_SPAWNER) == 0);
    CILK_ASSERT(w, t->simulated_stolen == false);

    Closure *call_parent = t->call_parent;
    Closure *t1 = deque_xtract_bottom(w, w->self);

    USE_UNUSED(t1);
    CILK_ASSERT(w, t == t1);
    CILK_ASSERT(w, t->stolen_flag = true);

    Closure_unlock(w, t);

    Closure_lock(w, call_parent);
    CILK_ASSERT(w, call_parent->fiber == t->fiber);
    t->fiber = NULL;

    Closure_remove_callee(w, call_parent);
    setup_call_parent_resumption(w, call_parent);
    Closure_unlock(w, call_parent);

    if (t->saved_throwing_fiber) {
        cilk_fiber_deallocate_to_pool(w, t->saved_throwing_fiber);
        t->saved_throwing_fiber = NULL;
    }
    Closure_destroy(w, t);
    deque_add_bottom(w, call_parent, w->self);

    deque_unlock_self(w);
}

/***
 * ANGE: this function doesn't do much ... just some assertion
 * checks, marks the parent as ready and returns that.
 ***/
static Closure *unconditional_steal(__cilkrts_worker *const w,
                                    Closure *parent) {

    cilkrts_alert(STEAL, w, "(unconditional_steal) promoted closure %p",
                  (void *)parent);

    Closure_assert_ownership(w, parent);
    CILK_ASSERT(w, parent->simulated_stolen);
    CILK_ASSERT(w, !w->l->provably_good_steal);
    CILK_ASSERT(w, !Closure_has_children(parent));
    CILK_ASSERT(w, parent->status == CLOSURE_SUSPENDED);

    CILK_ASSERT(w, parent->owner_ready_deque == NO_WORKER);
    CILK_ASSERT(w, (parent->fiber == NULL) && parent->fiber_child);
    parent->fiber = parent->fiber_child;
    parent->fiber_child = NULL;
    Closure_make_ready(parent);

    return parent;
}

static Closure *provably_good_steal_maybe(__cilkrts_worker *const w,
        Closure *parent) {

    Closure_assert_ownership(w, parent);
    // cilkrts_alert(STEAL, w, "(provably_good_steal_maybe) cl %p",
    //               (void *)parent);
    CILK_ASSERT(w, !w->l->provably_good_steal);

    if (!Closure_has_children(parent) && parent->status == CLOSURE_SUSPENDED) {
        // cilkrts_alert(STEAL | ALERT_SYNC, w,
        //      "(provably_good_steal_maybe) completing a sync");

        /* do a provably-good steal; this is *really* simple */
        w->l->provably_good_steal = true;

        setup_for_sync(w, parent);
        CILK_ASSERT(w, parent->owner_ready_deque == NO_WORKER);
        Closure_make_ready(parent);

        cilkrts_alert(STEAL | ALERT_SYNC, w,
                      "(provably_good_steal_maybe) returned %p",
                      (void *)parent);

        return parent;
    }

    return NULL;
}

/***
 * Return protocol for a spawned child.
 *
 * Some notes on reducer implementation (which was taken out):
 *
 * If any reducer is accessed by the child closure, we need to reduce the
 * reducer view with the child's right_rmap, and its left sibling's
 * right_rmap (or parent's child_rmap if it's the left most child)
 * before we unlink the child from its sibling closure list.
 *
 * When we modify the sibling links (left_sib / right_sib), we always lock
 * the parent and the child.  When we retrieve the reducer maps from left
 * sibling or parent from their place holders (right_rmap / child_rmap),
 * we always lock the closure from whom we are getting the rmap from.
 * The locking order is always parent first then child, right child first,
 * then left.
 *
 * Once we have done the reduce operation, we try to deposit the rmap from
 * the child to either it's left sibling's right_rmap or parent's
 * child_rmap.  Note that even though we have performed the reduce, by the
 * time we deposit the rmap, the child's left sibling may have changed,
 * or child may become the new left most child.  Similarly, the child's
 * right_rmap may have something new again.  If that's the case, we
 * need to do the reduce again (in deposit_reducer_map).
 *
 * This function returns a closure to be executed next, or NULL if none.
 * The child must not be locked by ourselves, and be in no deque.
 ***/
static Closure *Closure_return(__cilkrts_worker *const w, Closure *child) {

    Closure *res = (Closure *)NULL;
    Closure *const parent = child->spawn_parent;

    CILK_ASSERT(w, child);
    CILK_ASSERT(w, child->join_counter == 0);
    CILK_ASSERT(w, child->status == CLOSURE_RETURNING);
    CILK_ASSERT(w, child->owner_ready_deque == NO_WORKER);
    Closure_assert_alienation(w, child);

    CILK_ASSERT(w, child->has_cilk_callee == 0);
    CILK_ASSERT(w, child->call_parent == NULL);
    CILK_ASSERT(w, parent != NULL);

    cilkrts_alert(RETURN, w, "(Closure_return) child %p, parent %p",
                  (void *)child, (void *)parent);

    /* The frame should have passed a sync successfully meaning it
       has not accumulated any maps from its children and the
       active map is in the worker rather than the closure. */
    CILK_ASSERT(w, !child->child_rmap && !child->user_rmap);

    /* If in the future the worker's map is not created lazily,
       assert it is not null here. */

    /* need a loop as multiple siblings can return while we
       are performing reductions */

    // always lock from top to bottom
    Closure_lock(w, parent);
    Closure_lock(w, child);

    // Deal with reducers and exceptions.  We do both in the same loop to ensure
    // that we end up with a consistent view of reducers and exceptions in
    // parent and sibling closures.  To "reduce" exceptions, deallocate any
    // exception objects and other fibers that have been reduced away.
    while (true) {
        // invariant: a closure cannot unlink itself w/out lock on parent
        // so what this points to cannot change while we have lock on parent

        // Get the right-sibling hypermap and exception.
        cilkred_map *right =
            atomic_load_explicit(&child->right_rmap, memory_order_acquire);
        atomic_store_explicit(&child->right_rmap, NULL, memory_order_relaxed);
        struct closure_exception right_exn = child->right_exn;
        clear_closure_exception(&(child->right_exn));

        // Get the "left" hypermap and exception, which either belongs to a
        // left sibling, if it exists, or the parent, otherwise.
        _Atomic(cilkred_map *) volatile *left_ptr;
        struct closure_exception *left_exn_ptr;
        Closure *const left_sib = child->left_sib;
        if (left_sib != NULL) {
            left_ptr = &left_sib->right_rmap;
            left_exn_ptr = &left_sib->right_exn;
        } else {
            left_ptr = &parent->child_rmap;
            left_exn_ptr = &parent->child_exn;
        }
        cilkred_map *left =
            atomic_load_explicit(left_ptr, memory_order_acquire);
        atomic_store_explicit(left_ptr, NULL, memory_order_relaxed);
        struct closure_exception left_exn = *left_exn_ptr;
        clear_closure_exception(left_exn_ptr);

        // Get the current active hypermap and exception.
        cilkred_map *active = w->reducer_map;
        w->reducer_map = NULL;
        struct closure_exception active_exn = child->user_exn;

        // If we have no hypermaps or exceptions on either the left or right,
        // deposit the active hypermap and exception and break from the loop.
        if (left == NULL && right == NULL && left_exn.exn == NULL &&
                right_exn.exn == NULL) {
            /* deposit views */
            atomic_store_explicit(left_ptr, active, memory_order_release);
            *left_exn_ptr = active_exn;
            break;
        }

        Closure_unlock(w, child);
        Closure_unlock(w, parent);
        // clean up exception objects
        if (left_exn.exn) {
            active_exn = left_exn;
            // can safely delete any exceptions to the right of left_exn.
            if (child->user_exn.exn) {
                _Unwind_DeleteException(
                    (struct _Unwind_Exception *)child->user_exn.exn);
                clear_closure_exception(&child->user_exn);
            }
            if (right_exn.exn) {
                _Unwind_DeleteException(
                    (struct _Unwind_Exception *)right_exn.exn);
                clear_closure_exception(&right_exn);
            }
        } else if (child->user_exn.exn) {
            // can safely delete right_exn.
            if (right_exn.exn) {
                _Unwind_DeleteException(
                    (struct _Unwind_Exception *)right_exn.exn);
                clear_closure_exception(&right_exn);
            }
        } else if (right_exn.exn) {
            // save right_exn.
            active_exn = right_exn;
        }
        child->user_exn = active_exn;

        // merge reducers
        if (left) {
            active = merge_two_rmaps(w, left, active);
        }
        if (right) {
            active = merge_two_rmaps(w, active, right);
        }
        w->reducer_map = active;
        Closure_lock(w, parent);
        Closure_lock(w, child);
    }

    /* The returning closure and its parent are locked. */

    // Execute left-holder logic for stacks.
    if (child->left_sib || parent->fiber_child) {
        // Case where we are not the leftmost stack.
        CILK_ASSERT(w, parent->fiber_child != child->fiber);
        cilk_fiber_deallocate_to_pool(w, child->fiber);
    } else {
        // We are leftmost, pass stack/fiber up to parent.
        // Thus, no stack/fiber to free.
        CILK_ASSERT(w, child->fiber != NULL);
        parent->fiber_child = child->fiber;
    }
    child->fiber = NULL;

    Closure_remove_child(w, parent, child); // unlink child from tree
    // we have deposited our views and unlinked; we can quit now
    // invariant: we can only decide to quit when we see no more maps
    // from the right, we have deposited our own rmap, and unlink from
    // the tree.  All these are done while holding lock on the parent.
    // Before, another worker could deposit more rmap into our
    // right_rmap slot after we decide to quit, but now this cannot
    // occur as the worker depositing the rmap to our right_rmap also
    // must hold lock on the parent to do so.
    Closure_unlock(w, child);
    /*    Closure_unlock(w, parent);*/

    Closure_destroy(w, child);

    /*    Closure_lock(w, parent);*/

    CILK_ASSERT(w, parent->status != CLOSURE_RETURNING);
    // CILK_ASSERT(w, parent->frame->magic == CILK_STACKFRAME_MAGIC);
    CILK_ASSERT(w, parent->join_counter);

    --parent->join_counter;

    if (parent->simulated_stolen) {
        // parent stolen via simulated steal on worker's own deque
        res = unconditional_steal(w, parent); // must succeed
        CILK_ASSERT(w, parent->fiber && (parent->fiber_child == NULL));
    } else {
        res = provably_good_steal_maybe(w, parent);
    }

    if (res) {
        struct closure_exception child_exn = parent->child_exn;
        struct closure_exception active_exn = parent->user_exn;
        clear_closure_exception(&(parent->child_exn));
        clear_closure_exception(&(parent->user_exn));
        // reduce the exception
        if (!child_exn.exn) {
            parent->user_exn = active_exn;
        } else {
            if (active_exn.exn) {
                _Unwind_DeleteException(
                    (struct _Unwind_Exception *)active_exn.exn);
                clear_closure_exception(&active_exn);
            }
            parent->user_exn = child_exn;
            parent->exception_pending_flag = true;
        }

        CILK_ASSERT(w, !w->reducer_map);
        cilkred_map *child =
            atomic_load_explicit(&parent->child_rmap, memory_order_acquire);
        cilkred_map *active = parent->user_rmap;
        atomic_store_explicit(&parent->child_rmap, NULL, memory_order_relaxed);
        parent->user_rmap = NULL;
        w->reducer_map = merge_two_rmaps(w, child, active);

        if (parent->simulated_stolen) {
            atomic_store_explicit(&parent->child_rmap, w->reducer_map,
                                  memory_order_relaxed);
            // force the continuation to create new views
            w->reducer_map = NULL;
        }
    }

    Closure_unlock(w, parent);
    return res;
}

/*
 * ANGE: t is returning; call the return protocol; see comments above
 * Closure_return.  res is either the next closure to execute
 * (provably-good-steal the parent closure), or NULL if nothing should be
 * executed next.
 *
 * Only called from do_what_it_says when the closure->status =
 * CLOSURE_RETURNING
 */
static Closure *return_value(__cilkrts_worker *const w, Closure *t) {
    cilkrts_alert(RETURN, w, "(return_value) closure %p", (void *)t);

    Closure *res = NULL;
    CILK_ASSERT(w, t->status == CLOSURE_RETURNING);
    CILK_ASSERT(w, t->call_parent == NULL);

    if (t->call_parent == NULL) {
        res = Closure_return(w, t);
    } /* else {
     // ANGE: the ONLY way a closure with call parent can reach here
     // is when the user program calls Cilk_exit, leading to global abort
     // Not supported at the moment
     }*/

    cilkrts_alert(RETURN, w, "(return_value) returning closure %p", (void *)t);

    return res;
}

/*
 * ANGE: this is called from the user code (generated by compiler in cilkc)
 *       if Cilk_cilk2c_pop_check returns TRUE (i.e. E >= T).  Two
 *       possibilities: 1. someone stole the last frame from this worker,
 *       hence pop_check fails (E >= T) when child returns.  2. Someone
 *       invokes signal_immediate_exception with the closure currently
 *       running on the worker's deque.  This is only possible with abort.
 *
 *       If this function returns 1, the user code then calls
 *       Cilk_cilk2c_before_return, which destroys the shadow frame and
 *       return back to caller.
 */
void Cilk_exception_handler(char *exn) {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    // CILK_ASSERT(w, w->spawn_counter >= -1);
    Closure *t;

    while (!deque_trylock(w, w->self)) {
        #if SIGNAL_FREE_BACKTRACKING
            if (w->waiting_thief) {
                __atomic_store_n(&(w->l->context.fail_flag), 1, __ATOMIC_SEQ_CST);
                __atomic_store_n(&(w->l->context.poll_flag), 1, __ATOMIC_SEQ_CST);
                while (w->l->context.poll_flag) {
                }            
            }
        #endif
    }
    t = deque_peek_bottom(w, w->self);
    CILK_ASSERT(w, t);
    Closure_lock(w, t);

    cilkrts_alert(EXCEPT, w, "(Cilk_exception_handler) closure %p!", (void *)t);

    CILK_ASSERT(w, t->status == CLOSURE_RUNNING ||
                // for during abort process
                t->status == CLOSURE_RETURNING);

    /* These will not change while w is locked. */

    cilkrts_alert(EXCEPT, w, "(Cilk_exception_handler) this is a steal!");
    if (NULL != exn)
        t->user_exn.exn = exn;

    if (t->status == CLOSURE_RUNNING) {
        CILK_ASSERT(w, Closure_has_children(t) == 0);
        Closure_set_status(w, t, CLOSURE_RETURNING);
    }

#if CILK_BACKTRACK_CACHE_SIZE
    CILK_ASSERT(w, w->l->stored_cache_count < 1);
    w->l->stored_cache_count = 0;
#endif

#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
    *((volatile int*) & (w->spawn_counter)) = 0;
    *((volatile int*) & (w->steal_counter)) = 0;
#endif

    Closure_unlock(w, t);
    
    deque_unlock_self(w);
    sanitizer_unpoison_fiber(t->fiber);
    longjmp_to_runtime(w); // NOT returning back to user code

}

// ==============================================
// Steal related functions
// ==============================================

__attribute__((unused))
static inline bool trivial_stacklet(const __cilkrts_stack_frame *head) {
    assert(head);

    bool is_trivial = (head->flags & CILK_FRAME_SPAWNER);

    return is_trivial;
}

// __attribute__((unused))
// static inline bool singlton_stacklet(const __cilkrts_stack_frame *head) {
//     assert(head);
//     //TODO if Cl->frame != null then it has already been stolen
//     //if cl->frame != youngest, then it's not a singleton
//     //if cl->frame == null then the check is correct
//     bool is_singleton = ((head->call_parent && __cilkrts_stolen(head->call_parent))
//                         || ((!(head->call_parent)) && (head->flags & CILK_FRAME_LAST)));

//     return is_singleton;
// }

/*
 * This return the oldest frame in stacklet that has not been promoted to
 * full frame (i.e., never been stolen), or the closest detached frame
 * if nothing in this stacklet has been promoted.
 */
// static inline __cilkrts_stack_frame *
// oldest_non_stolen_frame_in_stacklet(__cilkrts_stack_frame *head) {

//     __cilkrts_stack_frame *cur = head;
//     while (cur && cur->call_parent && ((cur->call_parent->flags & CILK_FRAME_SPAWNER) == 0) &&
//            __cilkrts_not_stolen(cur->call_parent)) {
//         cur = cur->call_parent;
//     }

//     return cur;
// }


/***
 * ANGE: youngest_cl is the spawning parent that the thief is trying to
 * extract and resume.  Temporarily its call_parent is pointing to the
 * oldest closure on top of victim's deque when the steal occurs.
 * There may be more frames between them (i.e., stacklet contains more
 * than two frames) that require promotion.  This function promotes
 * and suspends them.
 ***/

/***
 * promote the child frame of parent to a full closure.
 * Detach the parent and return it.
 *
 * Assumptions: the parent is running on victim, and we own
 * the locks of both parent and deque[victim].
 * The child keeps running on the same cache of the parent.
 * The parent's join counter is incremented.
 *
 * In order to promote a child frame to a closure,
 * the parent's frame must be the last in its ready queue.
 *
 * Returns the child.
 *
 * ANGE: I don't think this function actually detach the parent.  Someone
 *       calling this function has to do deque_xtract_top on the victim's
 *       deque to get the parent closure.  This is the only time I can
 *       think of, where the ready deque contains more than one frame.
 ***/
static Closure *promote_child(__cilkrts_worker *const w,
                              __cilkrts_worker *const victim_w, Closure *cl,
                              Closure **res, uintptr_t rbp,
                              uintptr_t child_rbp) {

    worker_id pn = victim_w->self;

    deque_assert_ownership(w, pn);
    Closure_assert_ownership(w, cl);

    CILK_ASSERT(w, cl->status == CLOSURE_RUNNING);
    CILK_ASSERT(w, cl->owner_ready_deque == pn);
    CILK_ASSERT(w, cl->next_ready == NULL);

    /* cl may have a call parent: it might be promoted as its containing
     * stacklet is stolen, and it's call parent is promoted into full and
     * suspended
     */
    CILK_ASSERT(w, cl == w->g->root_closure || cl->spawn_parent ||
                cl->call_parent);

    Closure *spawn_parent = NULL;
    /* JFC: Should this load be relaxed or acquire? */

    // ANGE: this must be true if we get this far
    // Note that it can be that H == T here; victim could have done T--
    // after the thief passes Dekker; in which case, thief gets the last
    // frame, and H == T.  Victim won't be able to proceed further until
    // the thief finishes stealing, releasing the deque lock; at which
    // point, the victim will realize that it should return back to runtime.

    // ANGE: if cl's frame is set AND equal to the frame at *HEAD, cl must be
    // either the root frame or have been stolen before.  On the other hand, if
    // cl's frame is not set, the top stacklet may contain one frame (the
    // detached spawn helper resulted from spawning an expression) or more than
    // one frame, where the right-most (oldest) frame is a spawn helper that
    // called a Cilk function (regular cilk_spawn of function).
    // if (cl->frame != NULL && __cilkrts_stolen(cl->frame) { // stolen before
    //     CILK_ASSERT(w, __cilkrts_stolen(frame_to_steal));
    //     spawn_parent = cl;
    // } else if (cl->frame == NULL && singlton_stacklet(frame_to_steal)) {
    //     CILK_ASSERT(w, cl->frame == NULL);
    //     cl->frame = frame_to_steal;
    //     spawn_parent = cl;
    //     __cilkrts_set_stolen(frame_to_steal);
    //} else if (trivial_stacklet(frame_to_steal)) { // spawning expression
    //     CILK_ASSERT(w, __cilkrts_not_stolen(frame_to_steal));
    //     // CILK_ASSERT(w, frame_to_steal->call_parent &&
    //     //                    __cilkrts_stolen(frame_to_steal->call_parent));
    //     // CILK_ASSERT(w, (frame_to_steal->flags & CILK_FRAME_LAST) == 0);
    //     // CILK_ASSERT(w, cl->frame == NULL);
    //     cl->frame = frame_to_steal;
    if (cl->functional_rbp == (char*) rbp) {
        spawn_parent = cl;
        __cilkrts_set_stolen(spawn_parent);
    } else {
        spawn_parent = Closure_create(w);
        Closure_set_status(w, spawn_parent, CLOSURE_RUNNING);
        spawn_parent->stolen_flag = true;
        Closure_add_callee(w, cl, spawn_parent);
        spawn_parent->fiber = cl->fiber;
        spawn_parent->functional_rbp = (char*) rbp;
        Closure_suspend_victim(w, victim_w, cl);
        Closure_unlock(w, cl);
        Closure_lock(w, spawn_parent);
        __cilkrts_set_stolen(spawn_parent);
        __cilkrts_set_stolen(cl);
        *res = spawn_parent;
    }
    //} else { // spawning a function and stacklet never gotten stolen before
    // // cl->frame could either be NULL or some older frame (e.g.,
    // // cl->frame was stolen and resumed, it calls another frame which
    // // spawned, and the spawned frame is the frame_to_steal now). ANGE:
    // // if this is the case, we must create a new Closure representing
    // // the left-most frame (the one to be stolen and resume).
    // spawn_parent = Closure_create(w);
    // Closure_set_status(w, spawn_parent, CLOSURE_RUNNING);
    // spawn_parent->stolen_flag = true;

    // // ANGE: this is only temporary; will reset this after the stack has
    // // been remapped; so lets not set the callee in cl yet ... although
    // // we do need to set the has_callee in cl, so that cl does not get
    // // resumed by some other child performing provably good steal.
    // Closure_add_temp_callee(w, cl, spawn_parent);
    // spawn_parent->call_parent = cl;

    // // suspend cl & remove it from deque
    // Closure_suspend_victim(w, victim_w, cl);
    // Closure_unlock(w, cl);

    // Closure_lock(w, spawn_parent);
    // *res = spawn_parent;
    //}

    CILK_ASSERT(w, spawn_parent->has_cilk_callee == 0);
    Closure *spawn_child = Closure_create(w);

    spawn_child->spawn_parent = spawn_parent;
    spawn_child->functional_rbp = (char*) child_rbp;
    Closure_set_status(w, spawn_child, CLOSURE_RUNNING);

    /***
     * Register this child, which sets up its sibling links.
     * We do this here intead of in finish_promote, because we must setup
     * the sib links for the new child before its pointer escapses.
     ***/
    Closure_add_child(w, spawn_parent, spawn_child);

    ++spawn_parent->join_counter;
#ifdef USING_UNWIND_CACHING
    (*(victim_w->head))->valid_regstate = 0; //Kill the valid marker on the head just to be safe -- shouldn't be necessary?
#endif

    /* insert the closure on the victim processor's deque */
    deque_add_bottom(w, spawn_child, pn);
    victim_w->current_closure = spawn_child;

    /* at this point the child can be freely executed */
    return spawn_child;
}

/***
 * Finishes the promotion process.  The child is already fully promoted
 * and requires no more work (we only use the given pointer to identify
 * the child).  This function does some more work on the parent to make
 * the promotion complete.
 *
 * ANGE: This includes promoting everything along the stolen stacklet
 * into full closures.
 ***/
static void finish_promote(__cilkrts_worker *const w,
                           __cilkrts_worker *const victim_w, Closure *parent,
                           bool has_frames_to_promote) {
    Closure_assert_ownership(w, parent);
    CILK_ASSERT(w, parent->has_cilk_callee == 0);

    __cilkrts_set_unsynced(parent);
    /* Make the parent ready */
    Closure_make_ready(parent);

    return;
}

/***
 * ANGE: This function promotes all frames in the top-most stacklet into
 * its own closures and also creates a new child closure to leave it with
 * the victim.  Normally this is invoked by Closure_steal, but a worker
 * may also invoke Closure steal on itself (for the purpose of race detecting
 * Cilk code with reducers).  Thus, this function is written to check for
 * that --- if w == victim_w, we don't actually create a new fiber for
 * the stolen parent.
 *
 * NOTE: this function assumes that w holds the lock on victim_w's deque
 * and Closure cl and releases them before returning.
 ***/
Closure *extract_top_spawning_closure(__cilkrts_worker *const w,
                                      __cilkrts_worker *const victim_w,
                                      Closure *cl,
                                      uintptr_t rbp,
                                      uintptr_t child_rbp) {
    Closure *res = NULL, *child;
    struct cilk_fiber *parent_fiber = cl->fiber;

    deque_assert_ownership(w, victim_w->self);
    Closure_assert_ownership(w, cl);
    CILK_ASSERT(w, parent_fiber);

    /*
     * if dekker passes, promote the child to a full closure,
     * and steal the parent
     */
    child = promote_child(w, victim_w, cl, &res, rbp, child_rbp);
    cilkrts_alert(STEAL, w,
                  "(Closure_steal) promote gave cl/res/child = %p/%p/%p",
                  (void *)cl, (void *)res, (void *)child);
    victim_w->current_closure = child;
    /* detach the parent */
    if (res == (Closure *)NULL) {
        // ANGE: in this case, the spawning parent to steal / resume
        // is simply cl (i.e., there is only one frame in the stacklet),
        // so we didn't set res in promote_child.
        res = deque_xtract_top(w, victim_w->self);
        CILK_ASSERT(w, cl == res);
    }

    // ANGE: if worker w is stealing from itself, this is a simulated steal
    // only create a new fiber if it's a real steal
    if (w == victim_w) {
        res->fiber = NULL;
    } else {
        res->fiber = cilk_fiber_allocate_from_pool(w);
    }

    // make sure we are not hold lock on child
    Closure_assert_alienation(w, child);
    child->fiber = parent_fiber;

    return res;
}

#if CILK_BACKTRACK_CACHE_SIZE

void print_regstate_cache_sched(__cilkrts_worker * w) {
    printf("Cache for worker %d, count: %d, stolen: %d, popped: %d\n", w->self, w->l->stored_cache_count, w->l->stolen_cache_count, w->l->resumed_cache_count);
    for (int cache_index = 0; cache_index < CILK_BACKTRACK_CACHE_SIZE; cache_index++) {
        printf("\t[%d] - VALID[%s] - RBP: 0x%lx\n", cache_index, (w->l->regstate_cache[cache_index].valid ? "T" : "F"), w->l->regstate_cache[cache_index].regstate.registers[UNW_X86_64_RBP]);
    }
}

static int cache_closure_steal(__cilkrts_worker *const w, __cilkrts_worker *const victim_w, Closure * cl, Closure ** res) {
    int reduced_cache_count = __atomic_fetch_sub(&(victim_w->l->stored_cache_count), 1, __ATOMIC_SEQ_CST); //Check if we can grab from the cache.
    if (reduced_cache_count < 1) return 0; //We lost the race to the worker
    int stolen_cache_index = victim_w->l->stolen_cache_count++;
    __cilkrts_regstate stolen_regstate = victim_w->l->regstate_cache[stolen_cache_index].regstate;
    if (!victim_w->l->regstate_cache[stolen_cache_index].valid) {
        printf("rcc: %d\n", reduced_cache_count);
        print_regstate_cache_sched(victim_w);
    }

    if (victim_w->l->regstate_cache[stolen_cache_index].spawned_cache) {
        CILK_ASSERT(w, victim_w->l->regstate_cache[stolen_cache_index].valid);
        victim_w->l->regstate_cache[stolen_cache_index].valid = false;
        victim_w->l->regstate_cache[stolen_cache_index].spawned_cache = false;
        CILK_ASSERT(w, stolen_regstate.registers[UNW_X86_64_RIP]);
        //print_regstate(&stolen_regstate);
        uintptr_t sbp = stolen_regstate.registers[UNW_X86_64_RBP];
        uintptr_t child_sbp = stolen_regstate.old_rbp;
        void** victim_retaddr = stolen_regstate.ret_addr;
        __atomic_store_n(victim_retaddr, (void*) stolen_regstate.thief_retaddr, __ATOMIC_SEQ_CST);

#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
        *((volatile int*) & (victim_w->steal_counter)) += 1;
#endif
        *res = extract_top_spawning_closure(w, victim_w, cl, sbp, child_sbp);
        cl = *res;

        if (cl->orig_rsp == NULL) {
            cl->orig_rsp = (void*)((uintptr_t) stolen_regstate.registers[UNW_X86_64_RSP]);
        }
        copy_regstate_to_regstate(&stolen_regstate, &(cl->regstate));
        __cilkrts_set_unsynced(cl);
        Closure_make_ready(cl);
        cl->jump_cursor_ready = 1;
        CILK_BACKTRACK_INCR_SLOW_SPAWN(w);
        return 1;
    }

    CILK_ASSERT(w, victim_w->l->regstate_cache[stolen_cache_index].valid);
    CILK_ASSERT(w, !(victim_w->l->regstate_cache[stolen_cache_index].spawned_cache));
    victim_w->l->regstate_cache[stolen_cache_index].valid = false;
    CILK_ASSERT(w, stolen_regstate.registers[UNW_X86_64_RIP]);
    //print_regstate(&stolen_regstate);
    uintptr_t sbp = stolen_regstate.registers[UNW_X86_64_RBP];
    uintptr_t child_sbp = stolen_regstate.old_rbp;
    void** victim_retaddr = stolen_regstate.ret_addr;
    __atomic_store_n(victim_retaddr, (void*) stolen_regstate.pop_addr, __ATOMIC_SEQ_CST);

#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
    *((volatile int*) & (victim_w->steal_counter)) += 1;
#endif
    *res = extract_top_spawning_closure(w, victim_w, cl, sbp, child_sbp);
    cl = *res;

    if (cl->orig_rsp == NULL) {
        cl->orig_rsp = (void*)((uintptr_t) stolen_regstate.registers[UNW_X86_64_RSP]);
    }
    copy_regstate_to_regstate(&stolen_regstate, &(cl->regstate));
    __cilkrts_set_unsynced(cl);
    Closure_make_ready(cl);
    cl->jump_cursor_ready = 1;
    return 1;
}
#endif
/*
 * stealing protocol.  Tries to steal from the victim; returns a
 * stolen closure, or NULL if none.
 */

#define CYCLES_PER_NANOSECOND 2.4

// Function to wait for a given number of nanoseconds
static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void __attribute__((always_inline)) wait_nanoseconds(global_state *rts, int64_t nanoseconds) {
    uint64_t start, end;
    uint64_t cycles_to_wait;

    // Calculate the number of cycles to wait for the given nanoseconds
    cycles_to_wait = (uint64_t)(nanoseconds * CYCLES_PER_NANOSECOND);

    start = rdtsc();
    do {
        end = rdtsc();
    } while ((end - start) < cycles_to_wait && !atomic_load_explicit(&rts->done, memory_order_acquire));
}


static Closure *Closure_steal(__cilkrts_worker *const w, int victim) {
    Closure *cl;
    Closure *res = (Closure *)NULL;
    __cilkrts_worker *victim_w;
    victim_w = w->g->workers[victim];

    // CILK_START_TIMING(w, INTERVAL_SLEEP);
    // struct timespec remaining_time;
    // const struct timespec requested_time = {.tv_sec = 0, .tv_nsec = 100000 };
    // nanosleep(&requested_time, &remaining_time);
    // CILK_STOP_TIMING(w, INTERVAL_SLEEP);

    uint64_t backtrack_time = 10000;
    CILK_START_TIMING(w, INTERVAL_STEALING);
//----- EVENT_STEAL_ATTEMPT
    if (deque_trylock(w, victim) == 0) {
        CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_DEQUE_LOCK);
        return NULL;
    }

    cl = deque_peek_top(w, victim);

    if (cl) {
        if (Closure_trylock(w, cl) == 0) {
            deque_unlock(w, victim);
            CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_CLOSURE_LOCK);
            return NULL;
        }
#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
        if ((victim_w->spawn_counter - victim_w->steal_counter) < 1) {
            CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_GREEDY_HEURISTIC);
            goto give_up;
        }
#endif
        // cilkrts_alert(STEAL, "[%d]: trying steal from W%d; cl=%p",
        // (void *)victim, (void *)cl);
        switch (cl->status) {
        case CLOSURE_RUNNING:
#if CILK_BACKTRACK_CACHE_SIZE
            if (cache_closure_steal(w, victim_w, cl, &res)) {
                CILK_BACKTRACK_INCR_CACHE_STEALS(victim_w);
                deque_unlock(w, victim);
                CILK_ASSERT(w, res);
                CILK_ASSERT(w, res->fiber);
                Closure_assert_ownership(w, res);
                Closure_unlock(w, res);
                __atomic_store_n(&(victim_w->l->context.poll_flag), 0, __ATOMIC_SEQ_CST);
                CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_STEALING_CACHED);
                return res;
            }
            if (w->l->cacheless_steal_buffer > 0) {
                CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_STEALING_CACHED);
                w->l->cacheless_steal_buffer--;
                goto give_up;
            }
#endif
#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
            if ((victim_w->spawn_counter - victim_w->steal_counter) < 1) {
                CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_GREEDY_HEURISTIC);
                goto give_up;
            }
#endif
            backtrack_time = rdtsc();
#if SIGNAL_FREE_BACKTRACKING
            __atomic_store_n(&(victim_w->waiting_thief), 1, __ATOMIC_SEQ_CST);
#else
	    pthread_kill(w->g->threads[victim], SIGUSR1);
	    CILK_START_TIMING(w, INTERVAL_TEMP);
#endif
            while (!(victim_w->l->context.poll_flag)
#if SIGNAL_FREE_BACKTRACKING
&& !w->g->done
#endif
            ) {
#if SIGNAL_FREE_BACKTRACKING
                CILK_ASSERT(w, victim_w->waiting_thief);
#endif
            };
	    CILK_STOP_TIMING(w, INTERVAL_TEMP);
#if SIGNAL_FREE_BACKTRACKING
            __atomic_store_n(&(victim_w->waiting_thief), 0, __ATOMIC_SEQ_CST);
            if (victim_w->l->context.fail_flag || w->g->done) {
                __atomic_store_n(&(victim_w->l->context.poll_flag), 0, __ATOMIC_SEQ_CST);
                __atomic_store_n(&(victim_w->l->context.fail_flag), 0, __ATOMIC_SEQ_CST);
                CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_TRUE_HEURISTIC);
                goto give_up;
            }
#endif
#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
            if ((victim_w->spawn_counter - victim_w->steal_counter) < 1) {
                // if (victim_w->spawn_counter < -1) printf("Failed spawn counter was %d\n", victim_w->spawn_counter);
                // CILK_ASSERT(w, victim_w->spawn_counter >= -1);
                __atomic_store_n(&(victim_w->l->context.poll_flag), 0, __ATOMIC_SEQ_CST);
                CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_TRUE_HEURISTIC);
                backtrack_time = 10000;
                goto give_up;
            }
#endif

            /* send the exception to the worker */
            cilkrts_alert(STEAL, w,
                          "(Closure_steal) can steal from W%d; cl=%p",
                          victim, (void *)cl);
            //Do a backtrace before stealing
            //WHEN_CILK_DEBUG(printf("[%d] Thief attempting to ping worker %d for a steal on frame with rsp 0x%lx\n", w->self, victim_w->self, (uintptr_t) res->orig_rsp);)
            #if CILK_BACKTRACK_STACK_WALKER
                w->l->context.current_cursor.rsp = victim_w->l->context.stored_rsp;
                w->l->context.current_cursor.rbp = victim_w->l->context.stored_rbp;
                w->l->context.current_cursor.rip = victim_w->l->context.stored_rip;
            #else
                unw_init_local(&(w->l->context.current_cursor), &(victim_w->l->context.current_context));
            #endif

            //printf("CL original RSP: 0x%p\n", res->orig_rsp);
            //do_identifying_backtrace(victim, &(victim_w->l->context.current_context), &(victim_w->l->context.current_cursor), res);
            CILK_START_TIMING(w, INTERVAL_BACKTRACKING_SUCCESS);
            CILK_START_TIMING(w, INTERVAL_BACKTRACKING_FAILURE);
            CILK_START_TIMING(w, INTERVAL_BACKTRACKING);
            backtracking_return_state return_state = do_jumpbuf_set_backtrace(w, victim_w, &(w->l->context.current_cursor), cl, &res);
            // CILK_ASSERT(w, return_state == SUCCESS);
            if (return_state != SUCCESS) {
                CILK_BACKTRACK_STOP_TIMING(w, 0);
                CILK_STOP_TIMING(w, INTERVAL_BACKTRACKING_FAILURE);
                CILK_DROP_TIMING(w, INTERVAL_BACKTRACKING_SUCCESS);
                CILK_INCR_BACKTRACK_FAILED(w);
                backtrack_time = (rdtsc() - backtrack_time)/CYCLES_PER_NANOSECOND;
                switch (return_state) {
                    case NONE:
                        CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_BACKTRACK_EMPTY);
                        break;
                    case NULL_RIP:
                        CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_BACKTRACK_NULL_RIP);
                        break;
                    case POP_STATE:
                        CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_BACKTRACK_POP_STATE);
                        break;
                    case OUTSIDE_FIBER:
                        CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_BACKTRACK_OUTSIDE_FIBER);
                        break;
                    case MISMATCHED_FRAME:
                        CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_BACKTRACK_MISMATCHED_FRAME);
                        break;
                    default: 
                        CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_BACKTRACK_EMPTY);
                        break;
                }
                goto give_up;
            } //resumes other worker
            CILK_BACKTRACK_STOP_TIMING(w, 1); //Special call handles special backtrack accounting
            CILK_STOP_TIMING(w, INTERVAL_BACKTRACKING_SUCCESS);
            CILK_DROP_TIMING(w, INTERVAL_BACKTRACKING_FAILURE);
            CILK_INCR_BACKTRACK_SUCCESS(w);
            //Finish the backtrace

            // at this point, more steals can happen from the victim.
            victim_w->steal_delay = 0;
            deque_unlock(w, victim);

            CILK_ASSERT(w, res);
            CILK_ASSERT(w, res->fiber);
            Closure_assert_ownership(w, res);
            cilkrts_alert(STEAL, w,
                          "(Closure_steal) success; res %p has "
                          "fiber %p; child %p has fiber %p",
                          (void *)res, (void *)res->fiber,
                          (void *)res->right_most_child,
                          (void *)res->right_most_child->fiber);
            Closure_unlock(w, res);
            CILK_STOP_TIMING(w, INTERVAL_STEALING);
            if (victim_w->l->is_slow_spawn) {
                victim_w->l->is_slow_spawn = false;
                CILK_BACKTRACK_INCR_SLOW_SPAWN(w);
            }
            return res;
            break;

        case CLOSURE_RETURNING: /* ok, let it leave alone */
            CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_FRAME_TYPE);
give_up:
            // MUST unlock the closure before the queue;
            // see rule D in the file PROTOCOLS
            Closure_unlock(w, cl);
            deque_unlock(w, victim);
#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
#else
            CILK_ASSERT(w, !victim_w->l->context.poll_flag);
            uint64_t wait_time = backtrack_time * 5 * (w->g->nworkers);
            wait_nanoseconds(w->g, wait_time);
#endif
            return res;
            break;

        default:
            // It's possible that this steal attempt peeked the root closure
            // from the top of a deque while a new Cilkified region was
            // starting.
            CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_FRAME_TYPE);
            if (cl != w->g->root_closure)
                cilkrts_bug(victim_w, "Bug: %s closure in ready deque",
                            Closure_status_to_str(cl->status));
            return res;
        }
    } else {
        // victim_w->steal_delay += 10000;
        // uint64_t delay = victim_w->steal_delay;
        deque_unlock(w, victim);
        CILK_SWAP_STOP_TIMING(w, INTERVAL_STEALING, INTERVAL_FAILED_DEQUE_EMPTY);
        //----- EVENT_STEAL_EMPTY_DEQUE
        return res;
    }
    CILK_ASSERT(w, 0); //unreachable
    return res;
}

/***
 * Protocol for promoting a worker's own deque.
 *
 * This is used by *sequential* cilksan race detector when detecting races for
 * code that uses reducers, invoked from compiled code to simulate that a
 * continuation of a spawn statement has been stolen.  Specifically this
 * function should be invoked in the spawn helper after detach.  Upon
 * invocation, the worker will promote its own deque but do not allocate a new
 * fiber for the parent Closure (i.e., the "stolen" continuation), as the
 * parent will not be resumed until the worker returns from the spawned child.
 ***/
void promote_own_deque(__cilkrts_worker *w) {

    if (deque_trylock(w, w->self) == 0) {
        cilkrts_bug(
            w, "Bug: failed to acquire deque lock when promoting own deque");
        return;
    }

    bool done = false;
    while (!done) {
        Closure *cl = deque_peek_top(w, w->self);
        CILK_ASSERT(w, cl);
        CILK_ASSERT(w, cl->status == CLOSURE_RUNNING);

        if (Closure_trylock(w, cl) == 0) {
            deque_unlock(w, w->self);
            cilkrts_bug(
                w,
                "Bug: failed to acquire deque lock when promoting own deque");
            return;
        }
        if (/*do_dekker_on(w, w, cl)*/ true) { //TODO: FIX
            CILK_ASSERT(w, 0);
            // unfortunately this function releases both locks
            Closure *res = extract_top_spawning_closure(w, w, cl, 0, 0); // this is broken, passing a bad rbp
            CILK_ASSERT(w, res);
            CILK_ASSERT(w, res->fiber == NULL);

            // ANGE: if cl is not the spawning parent, then
            // there is more frames in the stacklet to promote
            bool has_frames_to_promote = (cl != res);
            // ANGE: finish the promotion process in finish_promote
            finish_promote(w, w, res, has_frames_to_promote);

            Closure_set_status(w, res, CLOSURE_SUSPENDED);
            res->simulated_stolen = true;
            Closure_unlock(w, res);

        } else {
            Closure_unlock(w, cl);
            deque_unlock(w, w->self);
            done = true; // we can break out; no more frames to promote
        }
    }
}

// ==============================================
// Scheduling functions
// ==============================================

CHEETAH_INTERNAL_NORETURN
void longjmp_to_user_code(__cilkrts_worker *w, Closure *t) {
    CILK_ASSERT(w, w->l->state == WORKER_RUN);

    struct cilk_fiber *fiber = t->fiber;

    CILK_ASSERT(w, fiber || (t->simulated_stolen && fiber == NULL));

    if (w->l->provably_good_steal) {
        // in this case, we simply longjmp back into the original fiber
        // the SP(sf) has been updated with the right orig_rsp already

        // NOTE: this is a hack to disable these asserts if we are
        // longjmping to the personality function. (CILK_FRAME_EXCEPTING is
        // only set in the personality function.)
        if ((t->exception_pending_flag & CILK_FRAME_EXCEPTING) == 0) {
            CILK_ASSERT(w, t->orig_rsp == NULL);
            CILK_ASSERT(w, in_fiber(fiber, (char *)SP(t)));
        }
        t->exception_pending_flag = true;

        w->l->provably_good_steal = false;
        set_fiber_ownership(fiber, w);
    } else { // this is stolen work; the fiber is a new fiber
        // This is the first time we run the root closure in this Cilkified
        // region.  The closure has been completely setup at this point by
        // invoke_cilkified_root().  We just need jump to the user code.
        volatile bool *initialized = &w->g->root_closure_initialized;
        if (t == w->g->root_closure && *initialized == false) {
            *initialized = true;
        } else if (!t->simulated_stolen) {
            void *new_rsp = sysdep_reset_stack_for_resume(fiber, t);
            USE_UNUSED(new_rsp);
#if backtrack_jmp
            CILK_ASSERT(w, SP(t) == (uintptr_t) new_rsp);
#else
            CILK_ASSERT(w, SP(t) == new_rsp);
#endif
        }
    }
    CILK_ASSERT(w, owns_fiber(fiber, w));
#ifdef USING_UNWIND_STEALS
    if (t->jump_cursor_ready  /*&& t != w->g->invoke_main*/) {
        t->jump_cursor_ready = 0;
        //Set rsp to match new fiber
        t->regstate.registers[UNW_X86_64_RSP] = (uintptr_t) SP(t);
        CILK_SWITCH_TIMING(w, INTERVAL_SCHED, INTERVAL_WORK);
        // sysdep_restore_fp_state(sf);
        jump_to_regstate(&(t->regstate));
        //unw_resume_rsp(cursor, SP(sf));
        CILK_ASSERT(w, 0); //should never come back here as resume doesn't return
    }
#endif
    CILK_SWITCH_TIMING(w, INTERVAL_SCHED, INTERVAL_WORK);
    sanitizer_start_switch_fiber(fiber);
    sysdep_longjmp_to_cl(t);
}

__attribute__((noreturn)) void longjmp_to_runtime(__cilkrts_worker *w) {
    cilkrts_alert(SCHED | ALERT_FIBER, w, "(longjmp_to_runtime)");

    CILK_SWITCH_TIMING(w, INTERVAL_WORK, INTERVAL_SCHED);
    /* Can't change to WORKER_SCHED yet because the reducer map
       may still be set. */
    sanitizer_start_switch_fiber(NULL);
    __builtin_longjmp(w->l->rts_ctx, 1);
}

/* This function implements a sync in user code, including the implicit
   sync at the end of a function.  It is only called if compiled code
   finds CILK_FRAME_UNSYCHED is set.  It returns SYNC_READY if there
   are no children and execution can continue.  Otherwise it returns
   SYNC_NOT_READY to suspend the frame. */
int Cilk_sync(__cilkrts_worker *const w) {
    // CILK_ASSERT(w, w->spawn_counter >= -1);

    // cilkrts_alert(SYNC, w, "(Cilk_sync) frame %p", (void *)frame);

    Closure *t;
    int res = SYNC_READY;

    //----- EVENT_CILK_SYNC

    while (!deque_trylock(w, w->self)) {
        #if SIGNAL_FREE_BACKTRACKING
            if (w->waiting_thief) {
                __atomic_store_n(&(w->l->context.fail_flag), 1, __ATOMIC_SEQ_CST);
                __atomic_store_n(&(w->l->context.poll_flag), 1, __ATOMIC_SEQ_CST);
                while (w->l->context.poll_flag) {
                }            
            }
        #endif
    }
    t = deque_peek_bottom(w, w->self);

    Closure_lock(w, t);
    /* assert we are really at the top of the stack */
    // if (!Closure_at_top_of_stack(w)) {
    //     printf("[W%d] RBP: %p - Frame functional_rbp: %p\n", w->self, get_current_rbp(), t->functional_rbp);
    // }
    //CILK_ASSERT(w, Closure_at_top_of_stack(w));
    CILK_ASSERT(w, !(t->simulated_stolen) || !Closure_has_children(t));

    // reset_closure_frame(w, t);
    CILK_ASSERT(w, w == __cilkrts_get_tls_worker());
    CILK_ASSERT(w, t->status == CLOSURE_RUNNING);
    CILK_ASSERT(w, __cilkrts_stolen(t));
    CILK_ASSERT(w, t->has_cilk_callee == 0);
    // CILK_ASSERT(w, w, t->frame->magic == CILK_STACKFRAME_MAGIC);

    // each sync is executed only once; since we occupy user_rmap only
    // when sync fails, the user_rmap should remain NULL at this point.
    CILK_ASSERT(w, t->user_rmap == (cilkred_map *)NULL);

    // ANGE: we might have passed a sync successfully before and never
    // gotten back to runtime but returning to another ancestor that needs
    // to sync ... in which case we might have a fiber to free, but it's
    // never the same fiber that we are on right now.
    if (w->l->fiber_to_free) {
        CILK_ASSERT(w, w->l->fiber_to_free != t->fiber);
        // we should free this fiber now and we can as long as we are not on
        // it
        cilk_fiber_deallocate_to_pool(w, w->l->fiber_to_free);
        w->l->fiber_to_free = NULL;
    }

    if (Closure_has_children(t)) {
        cilkrts_alert(SYNC, w,
                      "(Cilk_sync) Closure %p has outstanding children",
                      (void *)t);

        // if we are syncing from the personality function (i.e. if an
        // exception in the continuation was thrown), we still need this
        // fiber for unwinding.
        if (t->user_exn.exn == NULL) {
            w->l->fiber_to_free = t->fiber;
        } else {
            t->saved_throwing_fiber = t->fiber;
        }
        t->fiber = NULL;
        // place holder for reducer map; the view in tlmm (if any) are
        // updated by the last strand in Closure t before sync; need to
        // reduce these when successful provably good steal occurs
        cilkred_map *reducers = w->reducer_map;
        w->reducer_map = NULL;
        Closure_suspend(w, t);
        t->user_rmap = reducers; /* set this after state change to suspended */
        res = SYNC_NOT_READY;
    } else {
        cilkrts_alert(SYNC, w, "(Cilk_sync) closure %p sync successfully",
                      (void *)t);
        setup_for_sync(w, t);
    }

    Closure_unlock(w, t);
    deque_unlock_self(w);

    if (res == SYNC_READY) {
        struct closure_exception child_exn = t->child_exn;
        if (child_exn.exn) {
            if (t->user_exn.exn) {
                _Unwind_DeleteException(
                    (struct _Unwind_Exception *)t->user_exn.exn);
                clear_closure_exception(&t->user_exn);
            }
            t->user_exn = child_exn;
            clear_closure_exception(&(t->child_exn));
            t->exception_pending_flag = true;
        }
        cilkred_map *child_rmap =
            atomic_load_explicit(&t->child_rmap, memory_order_acquire);
        if (child_rmap) {
            atomic_store_explicit(&t->child_rmap, NULL, memory_order_relaxed);
            /* reducer_map may be accessed without lock */
            w->reducer_map = merge_two_rmaps(w, child_rmap, w->reducer_map);
        }
        if (t->simulated_stolen)
            t->simulated_stolen = false;

        sanitizer_start_switch_fiber(t->fiber);
    }

    return res;
}

static void do_what_it_says(__cilkrts_worker *w, Closure *t) {
    do {
        cilkrts_alert(SCHED, w, "(do_what_it_says) closure %p", (void *)t);
        Closure_lock(w, t);

        switch (t->status) {
        case CLOSURE_READY:
            // ANGE: anything we need to free must have been freed at this point
            CILK_ASSERT(w, w->l->fiber_to_free == NULL);

            cilkrts_alert(SCHED, w, "(do_what_it_says) CLOSURE_READY");
            /* just execute it */
            setup_for_execution(w, t);
            // t->fiber->resume_sf = f; // I THINK this works
            Closure_unlock(w, t);

#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
            *((volatile int*) & (w->spawn_counter)) = 0;
            *((volatile int*) & (w->steal_counter)) = 0;
#endif

            // MUST unlock the closure before locking the queue
            // (rule A in file PROTOCOLS)
            deque_lock_self(w);
            deque_add_bottom(w, t, w->self);
            deque_unlock_self(w);

            /* now execute it */
            cilkrts_alert(SCHED, w, "(do_what_it_says) Jump into user code");

            // CILK_ASSERT(w, w->l->runtime_fiber != t->fiber);
            // cilk_fiber_suspend_self_and_resume_other(w->l->runtime_fiber,
            // t->fiber);
            // cilkrts_alert(SCHED, w, "(do_what_it_says) Back from user
            // code");
            // longjmp invalidates non-volatile variables
            /* reset the spawn counter */
            __cilkrts_worker *volatile w_save = w;
            if (__builtin_setjmp(w->l->rts_ctx) == 0) {
                worker_change_state(w, WORKER_RUN);
                longjmp_to_user_code(w, t);
            } else {
                w = w_save;
                CILK_ASSERT_POINTER_EQUAL(w, w, __cilkrts_get_tls_worker());
                sanitizer_finish_switch_fiber();
                worker_change_state(w, WORKER_SCHED);
                // CILK_ASSERT(w, t->fiber == w->l->fiber_to_free);
                if (w->l->fiber_to_free) {
                    cilk_fiber_deallocate_to_pool(w, w->l->fiber_to_free);
                }
                w->l->fiber_to_free = NULL;

                // Attempt to get a closure from the bottom of our deque.
                deque_lock_self(w);
                t = deque_xtract_bottom(w, w->self);
                deque_unlock_self(w);
            }

            break; // ?

        case CLOSURE_RETURNING:
            cilkrts_alert(SCHED, w, "(do_what_it_says) CLOSURE_RETURNING");
            // the return protocol assumes t is not locked, and everybody
            // will respect the fact that t is returning
            Closure_unlock(w, t);
            t = return_value(w, t);

            break; // ?

        default:
            cilkrts_bug(w, "do_what_it_says invalid status %d", t->status);
            cilkrts_bug(w, "do_what_it_says() closure status %s",
                        Closure_status_to_str(t->status));
            break;
        }
        if (t) {
            WHEN_SCHED_STATS(w->l->stats.repos++);
        }
    } while (t);
}

// Thin wrapper around do_what_it_says to allow the boss thread to execute the
// Cilk computation until it would enter the work-stealing loop.
void do_what_it_says_boss(__cilkrts_worker *w, Closure *t) {

    do_what_it_says(w, t);

    // At this point, the boss has run out of work to do.  Rather than become a
    // thief itself, the boss wakes up the root worker to become a thief.

    CILK_STOP_TIMING(w, INTERVAL_SCHED);
    worker_change_state(w, WORKER_IDLE);
#if BOSS_THIEF
    worker_scheduler(w);
#else
    __builtin_longjmp(w->g->boss_ctx, 1);
#endif
}

// Update the index-to-worker map to swap self with the worker at the target
// index.
static void swap_worker_with_target(global_state *g, worker_id self,
                                    worker_id target_index) {
    worker_id self_index = g->worker_to_index[self];
    worker_id target_worker = g->index_to_worker[target_index];

    // Update the index-to-worker map.
    g->index_to_worker[self_index] = target_worker;
    g->index_to_worker[target_index] = self;

    // Update the worker-to-index map.
    g->worker_to_index[target_worker] = self_index;
    g->worker_to_index[self] = target_index;
}

// Called by a thief thread.  Causes the thief thread to try to sleep, that is,
// to wait for a signal to resume work-stealing.
static bool try_to_disengage_thief(global_state *g, worker_id self,
                                   uint64_t disengaged_deprived) {
    // Try to grab the lock on the index structure.
    if (!cilk_mutex_try(&g->index_lock)) {
        return false;
    }

    // Increment the number of disengaged thieves and decrement number of deprived
    // thieves.
    const uint64_t disengaged_mask = ((uint64_t) - 1) << 32;
    uint64_t disengaged = disengaged_deprived & disengaged_mask;
    uint64_t new_disengaged_deprived =
        ((disengaged + (1UL << 32)) & disengaged_mask) |
        ((disengaged_deprived - 1) & ~disengaged_mask);
    // Try to update the number of disengaged workers.  This step synchronizes
    // with parallel calls to reengage thieves, calls to reengage thieves, and
    // updates to the number of deprived workers.
    // First atomically update the number of disengaged workers.
    if (atomic_compare_exchange_strong_explicit(
                &g->disengaged_deprived, &disengaged_deprived,
                new_disengaged_deprived, memory_order_release,
                memory_order_acquire)) {
        // Update the index-to-worker map.
        worker_id last_index = g->nworkers - (new_disengaged_deprived >> 32);
        if (g->worker_to_index[self] < last_index) {
            swap_worker_with_target(g, self, last_index);
        }
        // Release the lock on the index structure
        cilk_mutex_unlock(&g->index_lock);

        // Disengage this thread.
        thief_disengage(g);

        // The thread is now reengaged.  Grab the lock on the index structure.
        cilk_mutex_lock(&g->index_lock);

        // Decrement the number of disengaged workers.
        while (true) {
            // Atomically decrement the number of disengaged workers.
            uint64_t disengaged_deprived = atomic_load_explicit(
                                               &g->disengaged_deprived, memory_order_acquire);
            disengaged = disengaged_deprived & disengaged_mask;
            new_disengaged_deprived = ((disengaged - (1UL << 32)) & disengaged_mask) |
                                      ((disengaged_deprived + 1) & ~disengaged_mask);
            if (atomic_compare_exchange_strong_explicit(
                        &g->disengaged_deprived, &disengaged_deprived,
                        new_disengaged_deprived, memory_order_release,
                        memory_order_acquire)) {
                // Update the index structure.
                last_index = g->nworkers - (disengaged_deprived >> 32);
                if (g->worker_to_index[self] > last_index) {
                    swap_worker_with_target(g, self, last_index);
                }

                // Release the lock on the index structure.
                cilk_mutex_unlock(&g->index_lock);
                return true;
            }
        }
    } else {
        // Release the lock on the index structure.
        cilk_mutex_unlock(&g->index_lock);
        return false;
    }
}

// Attempt to disengage this thief thread.  The __cilkrts_worker parameter is only
// used for debugging.
static bool maybe_disengage_thief(global_state *g, worker_id self,
                                  unsigned int nworkers, __cilkrts_worker *w) {
    // Check the number of active and deprived workers, and disengage this worker
    // if there are too many deprived workers.
    while (true) {
        // Check if this deprived thread should sleep.
        uint64_t disengaged_deprived =
            atomic_load_explicit(&g->disengaged_deprived, memory_order_acquire);
        const uint64_t disengaged_mask = ((uint64_t) - 1) << 32;
        uint32_t disengaged = (uint32_t)(disengaged_deprived >> 32);
        uint32_t deprived = (uint32_t)(disengaged_deprived & ~disengaged_mask);

        CILK_ASSERT(w, disengaged < nworkers);
        CILK_ASSERT(w, deprived < nworkers);
        int32_t active =
            (int32_t)nworkers - (int32_t)disengaged - (int32_t)deprived;
        CILK_ASSERT(w, active >= 1);
        // TODO: Investigate whether it's better to keep the number of deprived
        // workers less than the number of active workers.
        if (active < (int32_t)deprived) {
            // Too many deprived thieves.  Try to disengage this worker.  If it
            // fails, repeat the loop.
            if (try_to_disengage_thief(g, self, disengaged_deprived)) {
                // The thief was successfully disengaged.  It has since been
                // taken out of disengage.
                return true;
            }
        } else {
            // We have enough active workers to keep this worker out of disengage,
            // but this worker was still unable to steal work.  Put this thief
            // to sleep for a while using the conventional way.
            // In testing, a nanosleep(0) takes approximately 50 us.
            const struct timespec sleeptime = {.tv_sec = 0, .tv_nsec = 50000};
            /* const struct timespec sleeptime = {.tv_sec = 0, .tv_nsec =
             * 25000}; */
            nanosleep(&sleeptime, NULL);
            break;
        }
    }
    return false;
}

// Threshold for number of consective failed steal attempts to declare a
// thief as deprived.  Must be a power of 2.
#define DEPRIVED_THRESHOLD 128

// Number of attempted steals the thief should do each time it copies the
// worker state.  ATTEMPTS must divide DEPRIVED_THRESHOLD.
#define ATTEMPTS 4

static unsigned int go_to_sleep_maybe(global_state *const rts, worker_id self,
                                      unsigned int nworkers,
                                      __cilkrts_worker *const w,
                                      Closure *const t, unsigned int fails) {
    // Threshold for number of consecutive failed steal attempts to try
    // disengaging this worker.  Must be a multiple of DEPRIVED_THRESHOLD and a
    // power of 2.
    // return 0;
    const unsigned int DISENGAGE_THRESHOLD = 4 * DEPRIVED_THRESHOLD;
    // Threshold for number of failed steal attempts to put this thief to sleep
    // for an extended amount of time.  Must be larger than DISENGAGE_THRESHOLD.
    const unsigned int SLEEP_THRESHOLD = 32 * DEPRIVED_THRESHOLD;

    if (t) {
        if (fails >= DEPRIVED_THRESHOLD) {
            // This thief is no longer deprived.  Decrement the number
            // of deprived thieves.
            atomic_fetch_sub_explicit(&rts->disengaged_deprived, 1,
                                      memory_order_release);

            // Request to reengage at most 2 thieves.
            // TODO: Investigate whether it's better to keep the number
            // less than the number of active workers.
            request_more_thieves(rts, 2);
        }
        fails = 0;

    } else {
        CILK_START_TIMING(w, INTERVAL_SLEEP);
        fails += ATTEMPTS;

        // Every DEPRIVED_THRESHOLD consecutive failed steal attempts,
        // update the set of deprived workers, and maybe disengage this
        // worker if there are too many deprived workers.
        if (fails % DEPRIVED_THRESHOLD == 0) {
            if (fails > (1 << 25)) {
                // Prevent the fail count from exceeding this maximum, so we
                // don't have to worry about the fail count overflowing.
                //
                // This maximum bound is chosen based on the maximum sleep
                // time when fails > SLEEP_THRESHOLD, which specifies the
                // time to sleep in nanoseconds.  Because the specification
                // to nanosleep() disallows times with more than 1e9
                // nanoseconds, we set the maximum fails value here
                // accordinly and, in this case, simply sleep for 1 second.
                fails = (1 << 25);
                const struct timespec sleeptime = {.tv_sec = 1, .tv_nsec = 0};
                nanosleep(&sleeptime, NULL);
            } else if (DEPRIVED_THRESHOLD == fails) {
                // This thief is now considered deprived.  Increment the
                // number of deprived workers.
                atomic_fetch_add_explicit(&rts->disengaged_deprived, 1,
                                          memory_order_release);
            } else if (fails % DISENGAGE_THRESHOLD == 0) {
#if BOSS_THIEF
                if (is_boss_thread) {
                    // The boss thread should never disengage.  Sleep instead.
                    const struct timespec sleeptime = {.tv_sec = 0,
                              .tv_nsec = 50000
                    };
                    nanosleep(&sleeptime, NULL);
                } else
#endif
                    if (maybe_disengage_thief(rts, self, nworkers, w)) {
                        // The semaphore for reserving workers may have been
                        // non-zero due to past successful steals, rather than a
                        // recent successful steal.  Decrement fails so we try
                        // to disengage this again sooner, in case there is
                        // still nothing to steal.
                        fails -= (DISENGAGE_THRESHOLD / 2);
                    }

            } else if (fails > SLEEP_THRESHOLD) {
                // This thief has failed a lot of consecutive steal
                // attempts, but it's not disengaged.  Sleep for increasing
                // lengths of time.
                const struct timespec sleeptime = {.tv_sec = 0,
                          .tv_nsec = 16 * fails
                };
                nanosleep(&sleeptime, NULL);
            } else if (fails % DISENGAGE_THRESHOLD != 0) {
                // This thief has failed many consecutive steal attempts,
                // but it's not disengaged.  Sleep for a short time.
                const struct timespec sleeptime = {.tv_sec = 0,
                          .tv_nsec = 50000
                };
                nanosleep(&sleeptime, NULL);
            }
        }
        CILK_STOP_TIMING(w, INTERVAL_SLEEP);
    }

    return fails;
}

void worker_scheduler(__cilkrts_worker *w) {
    Closure *t = NULL;
    CILK_ASSERT(w, w == __cilkrts_get_tls_worker());

    CILK_START_TIMING(w, INTERVAL_SCHED);
    worker_change_state(w, WORKER_SCHED);
    global_state *rts = w->g;
    worker_id self = w->self;

    // Get this worker's local_state pointer, to avoid rereading it
    // unnecessarily during the work-stealing loop.  This optimization helps
    // reduce sharing on the worker structure.
    local_state *l = w->l;

    // Get the number of workers.  We don't currently support changing the
    // number of workers dynamically during execution of a Cilkified region.
    unsigned int nworkers = rts->nworkers;
    // Initialize count of consecutive failed steal attempts.  Effectively,
    // every worker is active upon entering this routine.
    unsigned int fails = 0;
    // Get pointers to the local and global copies of the index-to-worker map.
    worker_id *local_index_to_worker = l->index_to_worker;

    while (!atomic_load_explicit(&rts->done, memory_order_acquire)) {
        /* A worker entering the steal loop must have saved its reducer map into
           the frame to which it belongs. */
        CILK_ASSERT(w, !w->reducer_map);

        CILK_STOP_TIMING(w, INTERVAL_SCHED);

        while (!t && !atomic_load_explicit(&rts->done, memory_order_acquire)) {
            CILK_START_TIMING(w, INTERVAL_SCHED);
            CILK_START_TIMING(w, INTERVAL_IDLE);
            // Get the set of workers we can steal from and a local copy of the
            // index-to-worker map.  We'll attempt a few steals using these
            // local copies to minimize memory traffic.
            uint64_t disengaged_deprived = atomic_load_explicit(
                                               &rts->disengaged_deprived, memory_order_relaxed);
            uint32_t disengaged = (uint32_t)(disengaged_deprived >> 32);
            uint32_t stealable = nworkers - disengaged;
            // TODO: Technically, ATTEMPTS should scale with the number of
            // workers, to amortize the memcpy, which takes O(P)-time.  However,
            // in testing, ATTEMPTS = 4 works well even on large worker counts
            // (e.g., 96) in a NUMA environment.  I suspect the total cost of
            // the memcpy is too small for such worker counts to worry about.
            memcpy(local_index_to_worker, rts->index_to_worker,
                   sizeof(worker_id) * stealable);

            int attempt = ATTEMPTS;
            do {
                // Choose a random victim not equal to self.
                worker_id victim =
                    local_index_to_worker[rts_rand(l) % stealable];
                // while (((victim < 20) != (w->self < 20)) || victim == self) {
                //     if (((victim < 20) != (w->self < 20))) {
                //         if ((rts_rand(l) % 100 )) break;
                //     }
                //     victim = local_index_to_worker[rts_rand(l) % stealable];
                // }
                while (victim == self) {
                    victim = local_index_to_worker[rts_rand(l) % stealable];
                }
                // Attempt to steal from that victim.
                t = Closure_steal(w, victim);
//                 if (!t) {
//                     // Pause inside this busy loop.
// #ifdef __SSE__
//                     __builtin_ia32_pause();
// #endif
// #ifdef __aarch64__
//                     __builtin_arm_yield();
// #endif
//                 }
            } while (!t && --attempt > 0);

#if SCHED_STATS
            if (t) { // steal successful
                WHEN_SCHED_STATS(w->l->stats.steals++);
                CILK_STOP_TIMING(w, INTERVAL_SCHED);
                CILK_DROP_TIMING(w, INTERVAL_IDLE);
            } else { // steal unsuccessful
                CILK_STOP_TIMING(w, INTERVAL_IDLE);
                CILK_DROP_TIMING(w, INTERVAL_SCHED);
                cilk_increment_failed_steals(w);
            }
#endif
            fails = go_to_sleep_maybe(rts, self, nworkers, w, t, fails);
        }
        CILK_START_TIMING(w, INTERVAL_SCHED);
        // If one Cilkified region stops and another one starts, then a worker
        // can reach this point with t == NULL and w->g->done == false.  Check
        // that t is not NULL before calling do_what_it_says.
        if (t) {
            // if provably-good steal happens, do_what_it_says will return
            // the next closure to execute
            do_what_it_says(w, t);
            t = NULL;
#if CILK_BACKTRACK_CACHE_SIZE
            w->l->cacheless_steal_buffer = CILK_BACKTRACK_CACHELESS_STEAL_BUFFER_SIZE;
#endif
        }
    }

    if (fails >= DEPRIVED_THRESHOLD) {
        // If this worker was deprived, decrement the number of deprived
        // workers, essentially making this worker active.
        atomic_fetch_sub_explicit(&rts->disengaged_deprived, 1,
                                  memory_order_release);
    }
    CILK_STOP_TIMING(w, INTERVAL_SCHED);
    worker_change_state(w, WORKER_IDLE);
#if BOSS_THIEF
    if (is_boss_thread) {
        __builtin_longjmp(w->g->boss_ctx, 1);
    }
#endif
}

#ifdef USING_UNWIND_STEALS
void sigtrap_handler(int sig, siginfo_t * siginfo, void * context)
{
#if CILK_STATS
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    CILK_START_TIMING(w, INTERVAL_SIGNAL_HANDLER);
#endif
    __record_regstate(context);
#if CILK_STATS
    CILK_STOP_TIMING(w, INTERVAL_SIGNAL_HANDLER);
#endif
}
#endif
void *scheduler_thread_proc(void *arg) {
#ifdef USING_UNWIND_STEALS
    struct sigaction sa;
    sa.sa_sigaction = sigtrap_handler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGUSR1, &sa, NULL);
#endif
    __cilkrts_worker *w = (__cilkrts_worker *)arg;
    cilkrts_alert(BOOT, w, "scheduler_thread_proc");
    __cilkrts_set_tls_worker(w);

#if BOSS_THIEF
    CILK_ASSERT(w, w->self != 0);
#endif
    // Initialize the worker's fiber pool.  We have each worker do this itself
    // to improve the locality of the initial fibers.
    cilk_fiber_pool_per_worker_init(w);

    // Avoid redundant lookups of these commonly accessed worker fields.
    const worker_id self = w->self;
    global_state *rts = w->g;

    // Initialize worker's random-number generator.
    rts_srand(w, (self + 1) * 162347);

    CILK_START_TIMING(w, INTERVAL_SLEEP_UNCILK);
    do {
        // Wait for g->start == 1 to start executing the work-stealing loop.  We
        // use a condition variable to wait on g->start, because this approach
        // seems to result in better performance.
#if !BOSS_THIEF
        if (self == rts->exiting_worker) {
            root_worker_wait(rts, self);
        } else {
#endif
            thief_wait(rts);
#if !BOSS_THIEF
        }
#endif
        CILK_STOP_TIMING(w, INTERVAL_SLEEP_UNCILK);

        // Check if we should exit this scheduling function.
        if (rts->terminate) {
            return NULL;
        }

        // Start the new Cilkified region using the last worker that finished a
        // Cilkified region.  This approach ensures that the new Cilkified
        // region starts on an available worker with the worker state that was
        // updated by any operations that occurred outside of Cilkified regions.
        // Such operations, for example might have updated the left-most view of
        // a reducer.
        if (!atomic_load_explicit(&rts->done, memory_order_acquire)) {
            worker_scheduler(w);
        }

        // At this point, some worker will have finished the Cilkified region,
        // meaning it recordied its ID in g->exiting_worker and set g->done = 1.
        // That worker's state accurately reflects the execution of the
        // Cilkified region, including all updates to reducers.  Wait for that
        // worker to exit the work-stealing loop, and use it to wake-up the
        // original Cilkifying thread.
        if (self == rts->exiting_worker) {
            // Mark the computation as no longer cilkified, to signal the thread
            // that originally cilkified the execution.
            CILK_EXIT_WORKER_TIMING(rts);
            CILK_START_TIMING(w, INTERVAL_SLEEP_UNCILK);
            signal_uncilkified(rts);
#if BOSS_THIEF
            unsigned int fail = 0;
            while (fail++ < 2048 &&
                    !atomic_load_explicit(&rts->disengaged_thieves_futex,
                                          memory_order_acquire)) {
#ifdef __SSE__
                __builtin_ia32_pause();
#endif
#ifdef __aarch64__
                __builtin_arm_yield();
#endif
            }
#endif // BOSS_THIEF
        } else {
            CILK_START_TIMING(w, INTERVAL_SLEEP_UNCILK);
            // Busy-wait for a while to amortize the cost of syscalls to put
            // thief threads to sleep.
            unsigned int fail = 0;
            while (fail++ < 2048 &&
                    !atomic_load_explicit(&rts->disengaged_thieves_futex,
                                          memory_order_acquire)) {
#ifdef __SSE__
                __builtin_ia32_pause();
#endif
#ifdef __aarch64__
                __builtin_arm_yield();
#endif
            }
        }
    } while (true);
}
