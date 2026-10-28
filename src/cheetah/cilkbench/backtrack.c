#include "backtrack.h"
#include <stdio.h>
#include "cilk-internal.h"
#include "scheduler.h"
#include "closure.h"
#include <libunwind.h>
#include <limits.h>

int __cilkrts_register_spawn_state(uintptr_t _fast_call_address, uintptr_t _slow_call_address, uintptr_t _slow_return_address, uintptr_t _slow_thief_address) {
    int location = default_cilkrts->spawn_state_list_length++;
    if (location >= SPAWN_STATE_LIST_MAX_LENGTH) return 1;
    default_cilkrts->spawn_state_list[location].fast_call_address = _fast_call_address;
    default_cilkrts->spawn_state_list[location].slow_call_address = _slow_call_address;
    default_cilkrts->spawn_state_list[location].slow_return_address = _slow_return_address;
    default_cilkrts->spawn_state_list[location].slow_thief_address = _slow_thief_address;
    return 0;
}


void print_deque(__cilkrts_stack_frame **head, __cilkrts_stack_frame **tail) {
    printf(" -------------- PRINTING FULL DEQUE ------------------\n");
    __cilkrts_stack_frame* current_frame = *tail;
    while (tail > head) {
        while (current_frame != *tail) {
            printf("%sCilk Frame %p with call parent %p and stolen flag %s ",
                   (current_frame->flags & CILK_FRAME_SPAWNER ? "Spawner " : ""),
                   current_frame,
                   current_frame->call_parent,
                   (current_frame->flags & CILK_FRAME_STOLEN ? "true\n" : "false\n"));
            current_frame = current_frame->call_parent;
        }
        printf("Deque[%p] %s Cilk Frame %p with call parent %p and stolen flag %s ",
               tail,
               (current_frame->flags & CILK_FRAME_SPAWNER ? "Spawner " : ""),
               current_frame,
               current_frame->call_parent,
               (current_frame->flags & CILK_FRAME_STOLEN ? "true\n" : "false\n"));
        tail--;
        current_frame = current_frame->call_parent;
    }
    printf("----------------DEQUE OVER --------------------\n");

}

#ifdef USING_UNWIND_STEALS


__attribute__((noreturn)) void jump_to_regstate(__cilkrts_regstate* regstate) {
    asm volatile ("movq 0x0(%rdi), %rax"); //0
    asm volatile ("movq 0x08(%rdi), %rdx"); //1
    // asm volatile ("movq 0x10(%rdi), %rcx"); //2
    asm volatile ("movq 0x18(%rdi), %rbx"); //3
    asm volatile ("movq 0x20(%rdi), %rsi"); //4
    //skip over rdi for now since we need it to set other regs
    asm volatile ("movq 0x30(%rdi), %rbp"); //6
    asm volatile ("movq 0x38(%rdi), %rsp"); //7
    asm volatile ("movq 0x40(%rdi), %r8");  //8
    asm volatile ("movq 0x48(%rdi), %r9");  //9
    asm volatile ("movq 0x50(%rdi), %r10"); //10
    asm volatile ("movq 0x58(%rdi), %r11"); //11
    asm volatile ("movq 0x60(%rdi), %r12"); //12
    asm volatile ("movq 0x68(%rdi), %r13"); //13
    asm volatile ("movq 0x70(%rdi), %r14"); //14
    asm volatile ("movq 0x78(%rdi), %r15"); //15
    asm volatile ("movq 0x80(%rdi), %rcx");  //put rip in rcx so we can push then return to it
    asm volatile ("push %rcx");
    asm volatile ("movq 0x10(%rdi), %rcx");
    asm volatile ("movq 0x28(%rdi), %rdi");
    asm volatile ("retq");
    exit(1);
}

//sets RAX to 1 so that when we longjmp to the setjmp it properly alerts it of the jmp
__attribute__((noreturn)) void backtrack_longjmp(__cilkrts_regstate* regstate, int retval) {
    CILK_ASSERT_G(regstate->registers[UNW_X86_64_RIP]);
    regstate->registers[UNW_X86_64_RAX] = retval;
    jump_to_regstate(regstate);
}

//Saves state other than RIP (RSP and RBP values are invalid)
__attribute__((naked, noinline, returns_twice)) int backtrack_setjmp(__cilkrts_regstate* regstate) {
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


int compare_preserved_regstates(__cilkrts_regstate* start, __cilkrts_regstate* finish) {
    if (start->registers[UNW_X86_64_RBX] != finish->registers[UNW_X86_64_RBX]) return false;
    if (start->registers[UNW_X86_64_RBP] != finish->registers[UNW_X86_64_RBP]) return false;
    if (start->registers[UNW_X86_64_R12] != finish->registers[UNW_X86_64_R12]) return false;
    if (start->registers[UNW_X86_64_R13] != finish->registers[UNW_X86_64_R13]) return false;
    if (start->registers[UNW_X86_64_R14] != finish->registers[UNW_X86_64_R14]) return false;
    if (start->registers[UNW_X86_64_R15] != finish->registers[UNW_X86_64_R15]) return false;
    return true;
}


void print_reg_from_regstate(__cilkrts_regstate* regstate, unw_regnum_t regnum)
{
    switch (regnum) {
    case UNW_X86_64_RAX : printf("\tValue for register UNW_X86_64_RAX - 0x%lx \n", regstate->registers[regnum]); break; //0
    case UNW_X86_64_RDX : printf("\tValue for register UNW_X86_64_RDX - 0x%lx \n", regstate->registers[regnum]); break; //1
    case UNW_X86_64_RCX : printf("\tValue for register UNW_X86_64_RCX - 0x%lx \n", regstate->registers[regnum]); break; //2
    case UNW_X86_64_RBX : printf("\tValue for register UNW_X86_64_RBX - 0x%lx \n", regstate->registers[regnum]); break; //3
    case UNW_X86_64_RSI : printf("\tValue for register UNW_X86_64_RSI - 0x%lx \n", regstate->registers[regnum]); break; //4
    case UNW_X86_64_RDI : printf("\tValue for register UNW_X86_64_RDI - 0x%lx \n", regstate->registers[regnum]); break; //5
    case UNW_X86_64_RBP : printf("\tValue for register UNW_X86_64_RBP - 0x%lx \n", regstate->registers[regnum]); break; //6
    case UNW_X86_64_RSP : printf("\tValue for register UNW_X86_64_RSP - 0x%lx \n", regstate->registers[regnum]); break; //7
    case UNW_X86_64_R8  : printf("\tValue for register UNW_X86_64_R8  - 0x%lx \n", regstate->registers[regnum]); break; //8
    case UNW_X86_64_R9  : printf("\tValue for register UNW_X86_64_R9  - 0x%lx \n", regstate->registers[regnum]); break; //9
    case UNW_X86_64_R10 : printf("\tValue for register UNW_X86_64_R10 - 0x%lx \n", regstate->registers[regnum]); break; //10
    case UNW_X86_64_R11 : printf("\tValue for register UNW_X86_64_R11 - 0x%lx \n", regstate->registers[regnum]); break; //11
    case UNW_X86_64_R12 : printf("\tValue for register UNW_X86_64_R12 - 0x%lx \n", regstate->registers[regnum]); break; //12
    case UNW_X86_64_R13 : printf("\tValue for register UNW_X86_64_R13 - 0x%lx \n", regstate->registers[regnum]); break; //13
    case UNW_X86_64_R14 : printf("\tValue for register UNW_X86_64_R14 - 0x%lx \n", regstate->registers[regnum]); break; //14
    case UNW_X86_64_R15 : printf("\tValue for register UNW_X86_64_R15 - 0x%lx \n", regstate->registers[regnum]); break; //15
    case UNW_X86_64_RIP : printf("\tValue for register UNW_X86_64_RIP - 0x%lx \n", regstate->registers[regnum]); break; //16
    }
}

void print_regstate(__cilkrts_regstate* regstate) {
    printf("Regstate at %p\n", regstate);
    printf("\tOld RBP: 0x%lx\n", regstate->old_rbp);
    printf("\tPopaddr: 0x%lx\n", regstate->pop_addr);
    printf("\tRetAdder: %p\n", regstate->ret_addr);
    for (x86_64_regnum_t regnum = UNW_X86_64_RAX; regnum <= UNW_TDEP_LAST_REG; regnum++) {
        print_reg_from_regstate(regstate, regnum);
    }
}

void print_conflict_reg(unw_regnum_t regnum, unw_word_t v1, unw_word_t v2) {
    switch (regnum) {
    case UNW_X86_64_RAX : printf("                    Conflicting values for register UNW_X86_64_RAX - 0x%lx 0x%lx \n", v1, v2); break; //0
    case UNW_X86_64_RDX : printf("                    Conflicting values for register UNW_X86_64_RDX - 0x%lx 0x%lx \n", v1, v2); break; //1
    case UNW_X86_64_RCX : printf("                    Conflicting values for register UNW_X86_64_RCX - 0x%lx 0x%lx \n", v1, v2); break; //2
    case UNW_X86_64_RBX : printf("-- !!PRESERVED!! -- Conflicting values for register UNW_X86_64_RBX - 0x%lx 0x%lx \n", v1, v2); break; //3
    case UNW_X86_64_RSI : printf("                    Conflicting values for register UNW_X86_64_RSI - 0x%lx 0x%lx \n", v1, v2); break; //4
    case UNW_X86_64_RDI : printf("                    Conflicting values for register UNW_X86_64_RDI - 0x%lx 0x%lx \n", v1, v2); break; //5
    case UNW_X86_64_RBP : printf("-- !!PRESERVED!! -- Conflicting values for register UNW_X86_64_RBP - 0x%lx 0x%lx \n", v1, v2); break; //6
    case UNW_X86_64_RSP : printf("-- !!PRESERVED!! -- Conflicting values for register UNW_X86_64_RSP - 0x%lx 0x%lx \n", v1, v2); break; //7
    case UNW_X86_64_R8  : printf("                    Conflicting values for register UNW_X86_64_R8  - 0x%lx 0x%lx \n", v1, v2); break; //8
    case UNW_X86_64_R9  : printf("                    Conflicting values for register UNW_X86_64_R9  - 0x%lx 0x%lx \n", v1, v2); break; //9
    case UNW_X86_64_R10 : printf("                    Conflicting values for register UNW_X86_64_R10 - 0x%lx 0x%lx \n", v1, v2); break; //10
    case UNW_X86_64_R11 : printf("                    Conflicting values for register UNW_X86_64_R11 - 0x%lx 0x%lx \n", v1, v2); break; //11
    case UNW_X86_64_R12 : printf("-- !!PRESERVED!! -- Conflicting values for register UNW_X86_64_R12 - 0x%lx 0x%lx \n", v1, v2); break; //12
    case UNW_X86_64_R13 : printf("-- !!PRESERVED!! -- Conflicting values for register UNW_X86_64_R13 - 0x%lx 0x%lx \n", v1, v2); break; //13
    case UNW_X86_64_R14 : printf("-- !!PRESERVED!! -- Conflicting values for register UNW_X86_64_R14 - 0x%lx 0x%lx \n", v1, v2); break; //14
    case UNW_X86_64_R15 : printf("-- !!PRESERVED!! -- Conflicting values for register UNW_X86_64_R15 - 0x%lx 0x%lx \n", v1, v2); break; //15
    case UNW_X86_64_RIP : printf("-- !!PRESERVED!! -- Conflicting values for register UNW_X86_64_RIP - 0x%lx 0x%lx \n", v1, v2); break; //16
    }
}
#if CILK_BACKTRACK_CACHE_SIZE

void print_regstate_cache(__cilkrts_worker* w) {
    printf("Cache for worker %d, count: %d, stolen: %d, popped: %d\n", w->self, w->l->stored_cache_count, w->l->stolen_cache_count, w->l->resumed_cache_count);
    for (int cache_index = 0; cache_index < CILK_BACKTRACK_CACHE_SIZE; cache_index++) {
        printf("\t[%d] - VALID[%s] - RBP: 0x%lx\n", cache_index, (w->l->regstate_cache[cache_index].valid ? "T" : "F"), w->l->regstate_cache[cache_index].regstate.registers[UNW_X86_64_RBP]);
    }
}
#endif
void copy_reg(unw_cursor_t* src, unw_cursor_t* dst, unw_regnum_t regnum)
{
    unw_word_t pc;
    if (unw_get_reg(src, regnum, &pc)) {
        //printf(" -- error: failed to read\n");
    }
    int retval = unw_set_reg(dst, regnum, pc);
    if (retval && regnum != UNW_X86_64_RSP) {
        printf(" -- error: couldn't set value of register with number: %d", regnum);
    }
}

void copy_cursor(walker_state* src, walker_state* dst) {
    #if CILK_BACKTRACK_STACK_WALKER
    dst->rip = src->rip;
    dst->rsp = src->rsp;
    dst->rbp = src->rbp;
    #else
    for (x86_64_regnum_t regnum = UNW_X86_64_RAX; regnum <= UNW_TDEP_LAST_REG; regnum++) {
        copy_reg(src, dst, regnum);
    }
    #endif
}

void copy_reg_to_regstate(unw_cursor_t* src, __cilkrts_regstate* dst, unw_regnum_t regnum)
{
    unw_word_t pc;
//    unw_word_t pc2;
    unw_get_reg(src, regnum, &pc);
    // if(unw_get_reg(src, regnum, &pc)) {
    //     printf(" -- error: failed to read\n");
    // }
    // if (pc != pc2) {
    //     print_conflict_reg(regnum, pc, pc2);
    // }
    dst->registers[regnum] = pc;
}

void copy_regstate_to_regstate(__cilkrts_regstate* src, __cilkrts_regstate* dst)
{
    for (x86_64_regnum_t regnum = UNW_X86_64_RAX; regnum <= UNW_TDEP_LAST_REG; regnum++) {
        dst->registers[regnum] = src->registers[regnum];
    }
}


void copy_cursor_to_regstate(walker_state* src, __cilkrts_regstate* dst) {
    #if CILK_BACKTRACK_STACK_WALKER
    dst->registers[UNW_X86_64_RIP] = src->rip;
    dst->registers[UNW_X86_64_RSP] = src->rsp;
    dst->registers[UNW_X86_64_RBP] = src->rbp;
    #else
    for (x86_64_regnum_t regnum = UNW_X86_64_RAX; regnum <= UNW_TDEP_LAST_REG; regnum++) {
        copy_reg_to_regstate(src, dst, regnum);
    }
    if (!(dst->registers[UNW_X86_64_RIP])) {
        print_regstate(dst);
    }
    #endif
}

// void compare_regstate_to_jmpbuf(__cilkrts_regstate* src, jmpbuf jump_buffer) {
//     printf("Jumpbuff - [RBP: 0x%lx, RSP: 0x%lx, RIP: 0x%lx]\nRegstate - [RBP: 0x%lx, RSP: 0x%lx, RIP: 0x%lx]\n\n",
//            (uintptr_t) JMPBUF_FP(jump_buffer), (uintptr_t) JMPBUF_SP(jump_buffer), (uintptr_t) JMPBUF_PC(jump_buffer),
//            src->registers[UNW_X86_64_RBP], src->registers[UNW_X86_64_RSP], src->registers[UNW_X86_64_RIP]
//           );
// }

void __record_regstate(ucontext_t *uc) {
    __cilkrts_worker * w = __cilkrts_get_tls_worker();
    CILK_SWITCH_TIMING(w, INTERVAL_WORK, INTERVAL_INTERUPTED);
    w->l->context.stored_rip = uc->uc_mcontext.gregs[REG_RIP];
    w->l->context.stored_rsp = uc->uc_mcontext.gregs[REG_RSP];
    w->l->context.stored_rbp = uc->uc_mcontext.gregs[REG_RBP];
#if CILK_BACKTRACK_STACK_WALKER
#else
    w->l->context.current_context = *uc;
#endif
    __sync_fetch_and_add(&(w->l->context.poll_flag), 1);
    while (w->l->context.poll_flag) {
    }
    CILK_SWITCH_TIMING(w, INTERVAL_INTERUPTED, INTERVAL_WORK);
}

struct __spawn_state_registration* check_for_spawn_state_in_list(uintptr_t ip, __cilkrts_worker* const w) {
    for (int i = 0; i < SPAWN_STATE_LIST_MAX_LENGTH; i++) {
        if (w->g->spawn_state_list[i].fast_call_address == 0) break;
        if (w->g->spawn_state_list[i].fast_call_address == ip || w->g->spawn_state_list[i].slow_call_address == ip) {
            //printf("ip: %lx did match fast_call_address %lx or slow_call_address %lx\n", ip, w->g->spawn_state_list[i].fast_call_address, w->g->spawn_state_list[i].slow_call_address );
            return &(w->g->spawn_state_list[i]);
        } else {
            //printf("ip: %lx did not match fast_call_address %lx or slow_call_address %lx\n", ip, w->g->spawn_state_list[i].fast_call_address, w->g->spawn_state_list[i].slow_call_address );
        }
    }
    return NULL;
}


bool check_for_pop_state_in_list(uintptr_t ip, __cilkrts_worker* const w) {
    for (int i = 0; i < SPAWN_STATE_LIST_MAX_LENGTH; i++) {
        if (w->g->spawn_state_list[i].fast_call_address == 0) break;
        if (w->g->spawn_state_list[i].slow_return_address == ip) {
            //printf("ip: %lx did match fast_call_address %lx or slow_call_address %lx\n", ip, w->g->spawn_state_list[i].fast_call_address, w->g->spawn_state_list[i].slow_call_address );
            return true;
        } else {
            //printf("ip: %lx did not match fast_call_address %lx or slow_call_address %lx\n", ip, w->g->spawn_state_list[i].fast_call_address, w->g->spawn_state_list[i].slow_call_address );
        }
    }
    return false;
}

#if CILK_BACKTRACK_CACHE_SIZE
__attribute__((noinline)) void cache_pop_function_helper() {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    int reduced_cache_count = __atomic_fetch_sub(&(w->l->stored_cache_count), 1, __ATOMIC_SEQ_CST);
    if (reduced_cache_count < 1) {
        Cilk_exception_handler(NULL);
    }
    w->l->resumed_cache_count += 1;
    int cache_index = CILK_BACKTRACK_CACHE_SIZE - (w->l->resumed_cache_count);
    *(w->l->regstate_cache[cache_index].regstate.ret_addr) = (void *) w->l->regstate_cache[cache_index].regstate.resume_addr;
    if (!w->l->regstate_cache[cache_index].valid) print_regstate_cache(w);
    CILK_ASSERT(w, w->l->regstate_cache[cache_index].valid);
    w->l->regstate_cache[cache_index].valid = false;
    return;
}

__attribute__((naked, noinline)) void cache_pop_function() {
    asm volatile ("subq $8, %rsp");  //Move the stack pointer back to allow space for the return pointer to be written into this space
    asm volatile ("push %rax"); //save rax as it's potentially used to carry return values
    asm volatile ("callq cache_pop_function_helper");
    asm volatile ("popq %rax");
    asm volatile ("retq");
}
#endif


__attribute__((naked, noinline)) void holding_function() {
    asm volatile ("subq $8, %rsp");
    asm volatile ("pause");
    asm volatile ("retq");
}

#define PRINT_BACKTRACK 0
#define BACKTRACK_CACHE_DEBUG 0

struct cilk_fiber {
    char *alloc_low;         // first byte of mmap-ed region
    char *stack_low;         // lowest usable byte of stack
    char *stack_high;        // one byte above highest usable byte of stack
    char *alloc_high;        // last byte of mmap-ed region
    worker_id worker_num;
};

//This holds the information about each C frame as we backtrack
struct c_frame_data {
    uintptr_t rbp;
    uintptr_t rsp;
    uintptr_t rip;
    bool is_signal;
    bool is_in_fiber;
    bool valid;
    bool handpassed;
};

struct spawn_frame_data {
    struct c_frame_data child;
    struct c_frame_data parent;
    __spawn_state_registration spawn_state;
    __cilkrts_regstate regstate;
    bool valid;
};

//This struct will keep track of the state as we progress through the backtracking process
struct backtracking_state {
    uintptr_t signal_rsp;
    uintptr_t signal_rip;
    uintptr_t signal_rbp;
    struct c_frame_data child_child_child_frame;
    struct c_frame_data child_child_frame;
    struct c_frame_data child_frame;
    struct c_frame_data current_frame;
    struct spawn_frame_data latest_spawn;
    struct spawn_frame_data previous_spawn;
#if CILK_BACKTRACK_CACHE_SIZE
    __cilkrts_regstate latest_uncached_regstate;
    bool uncached_regstate;
#endif
    void** held_retptr;
    void* held_retfunc;
    int seen_frame;
    int frame_count;
};

typedef struct backtracking_state backtracking_state;

void initialize_backtracking_state(backtracking_state* backtracking_state, __cilkrts_worker *const victim_w) {
    backtracking_state->signal_rsp = victim_w->l->context.stored_rsp;
    backtracking_state->signal_rip = victim_w->l->context.stored_rip;
    backtracking_state->signal_rbp = victim_w->l->context.stored_rbp;
    backtracking_state->child_child_child_frame.valid = false;
    backtracking_state->child_child_frame.valid = false;
    backtracking_state->child_frame.valid = false;
    backtracking_state->current_frame.valid = false;
    backtracking_state->latest_spawn.valid = false;
    backtracking_state->previous_spawn.valid = false;
    backtracking_state->seen_frame = 0;
    backtracking_state->frame_count = 0;
    backtracking_state->held_retptr = NULL;
    backtracking_state->held_retfunc = NULL;
#if CILK_BACKTRACK_CACHE_SIZE
    backtracking_state->uncached_regstate = false;
#endif
}

void store_current_c_frame_values(walker_state* cursor, backtracking_state* backtracking_state) {
    #if CILK_BACKTRACK_STACK_WALKER
        backtracking_state->current_frame.rbp = cursor->rbp;
        backtracking_state->current_frame.rsp = cursor->rsp;
        backtracking_state->current_frame.rip = cursor->rip;
    #else
    backtracking_state->current_frame.is_signal = unw_is_signal_frame(cursor);
    unw_get_reg(cursor, UNW_X86_64_RBP, &(backtracking_state->current_frame.rbp));
    unw_get_reg(cursor, UNW_REG_IP, &(backtracking_state->current_frame.rip));
    unw_get_reg(cursor, UNW_REG_SP, &(backtracking_state->current_frame.rsp));
    #endif
    backtracking_state->current_frame.valid = true;
    backtracking_state->current_frame.handpassed = false;
}

void store_current_spawn_frame_value(__cilkrts_worker *const w, walker_state* cursor, backtracking_state* backtracking_state, __spawn_state_registration* tmp_spawn) {
    if (backtracking_state->latest_spawn.valid) backtracking_state->previous_spawn = backtracking_state->latest_spawn;
    copy_cursor_to_regstate(cursor, &(backtracking_state->latest_spawn.regstate));
    backtracking_state->latest_spawn.child = backtracking_state->child_frame;
    backtracking_state->latest_spawn.parent = backtracking_state->current_frame;
    backtracking_state->latest_spawn.regstate.old_rbp = backtracking_state->child_frame.rbp;
    backtracking_state->latest_spawn.regstate.ret_addr = ((void**) backtracking_state->current_frame.rsp) - 1;
    backtracking_state->latest_spawn.regstate.pop_addr = tmp_spawn->slow_return_address;
    backtracking_state->latest_spawn.regstate.resume_addr = (uintptr_t) * (backtracking_state->latest_spawn.regstate.ret_addr);
    backtracking_state->latest_spawn.spawn_state = *tmp_spawn;
    CILK_ASSERT(w, backtracking_state->latest_spawn.regstate.resume_addr == backtracking_state->current_frame.rip);
#if CILK_BACKTRACK_CACHE_SIZE
    *(backtracking_state->latest_spawn.regstate.ret_addr) = cache_pop_function;
#endif
    backtracking_state->latest_spawn.valid = true;
}

#if CILK_BACKTRACK_CACHE_SIZE
void initialize_victim_cache(__cilkrts_worker *const victim_w) {
    victim_w->l->stolen_cache_count = 0;
    victim_w->l->resumed_cache_count = CILK_BACKTRACK_CACHE_SIZE;
    victim_w->l->stored_cache_count = 0;
    for (int cache_index = 0; cache_index < CILK_BACKTRACK_CACHE_SIZE; cache_index++) {
        //CILK_ASSERT(w, !(victim_w->l->regstate_cache[cache_index].valid));
        victim_w->l->regstate_cache[cache_index].valid = false;
        victim_w->l->regstate_cache[cache_index].spawned_cache = false;
    }
}

void increment_victim_cache(__cilkrts_worker *const victim_w) {
    victim_w->l->resumed_cache_count -= 1;
    __atomic_fetch_add(&(victim_w->l->stored_cache_count), 1, __ATOMIC_SEQ_CST);
    CILK_BACKTRACK_INCR_CACHE_INSERT(victim_w);
}

void shift_victim_cache(__cilkrts_worker *const victim_w, backtracking_state* backtracking_state) {
    backtracking_state->latest_uncached_regstate = victim_w->l->regstate_cache[CILK_BACKTRACK_CACHE_SIZE - 1].regstate;
    *(backtracking_state->latest_uncached_regstate.ret_addr) = (void*) backtracking_state->latest_uncached_regstate.resume_addr;
    backtracking_state->uncached_regstate = true;
}

void insert_new_cache_spawn(__cilkrts_worker *const victim_w, backtracking_state* backtracking_state) {
    for (int cache_index = CILK_BACKTRACK_CACHE_SIZE - 1; cache_index > 0; cache_index--) {
        victim_w->l->regstate_cache[cache_index] = victim_w->l->regstate_cache[cache_index - 1];
    }
    victim_w->l->regstate_cache[0].regstate = backtracking_state->latest_spawn.regstate;
    victim_w->l->regstate_cache[0].valid = true;
    victim_w->l->regstate_cache[0].regstate.registers[UNW_X86_64_RIP] = backtracking_state->latest_spawn.spawn_state.slow_thief_address;
}
#endif

void __attribute__((noinline)) test_func() {
    printf("nice");
}

int test_step_cursor_from_frame(__cilkrts_worker *const w, struct c_frame_data* old_frame, struct c_frame_data* new_frame) {
    uintptr_t old_rbp = old_frame->rbp;
    CILK_ASSERT(w, old_rbp);
    // The new RBP is the saved RBP in the current stack frame.
    WHEN_CILK_DEBUG(uintptr_t new_rbp = *(uintptr_t*)old_rbp;)
    
    // Exit if bad RBP
    CILK_ASSERT(w, new_rbp);

    // The return address (the caller's RIP) is located 8 bytes above old_rbp.
    WHEN_CILK_DEBUG(uintptr_t new_rip = *(uintptr_t*)(old_rbp + sizeof(uintptr_t));)

    // The stack pointer for the caller's frame is typically old_rbp + 16.
    // This points to the position just after the saved RBP and RIP on the stack.
    WHEN_CILK_DEBUG(uintptr_t new_rsp = old_rbp + (2 * sizeof(uintptr_t));)

    // Update the cursor to the caller's frame.
    CILK_ASSERT(w, new_frame->rbp == new_rbp);
    CILK_ASSERT(w, new_frame->rsp == new_rsp);
    CILK_ASSERT(w, new_frame->rip == new_rip);
    return 1;
}

int step_state(walker_state* cursor) {
    #if CILK_BACKTRACK_STACK_WALKER
        if (cursor->rbp == 0) {
            return 0;
        }
        uintptr_t old_rbp = cursor->rbp;
        
        // The new RBP is the saved RBP in the current stack frame.
        uintptr_t new_rbp = *(uintptr_t*)old_rbp;
        
        // Exit if bad RBP
        if (new_rbp == 0) {
            return 0;
        }

        // The return address (the caller's RIP) is located 8 bytes above old_rbp.
        uintptr_t new_rip = *(uintptr_t*)(old_rbp + sizeof(uintptr_t));

        // The stack pointer for the caller's frame is typically old_rbp + 16.
        // This points to the position just after the saved RBP and RIP on the stack.
        uintptr_t new_rsp = old_rbp + (2 * sizeof(uintptr_t));

        // Update the cursor to the caller's frame.
        cursor->rbp = new_rbp;
        cursor->rip = new_rip;
        cursor->rsp = new_rsp;
        return 1;
    #else
        return unw_step(cursor);
    #endif
}

int do_jumpbuf_set_backtrace (__cilkrts_worker *const w, __cilkrts_worker *const victim_w, walker_state* cursor, Closure* cl, Closure** res) {
    backtracking_return_state fstate = NONE;
    //print_deque(head, tail);
    backtracking_state backtracking_state;
    initialize_backtracking_state(&backtracking_state, victim_w);
    victim_w->l->thief_id = w->self;

    while (step_state(cursor) > 0) {
        backtracking_state.frame_count += 1;
        CILK_BACKTRACK_INCR_FRAME(w);
        if (backtracking_state.child_child_frame.valid) backtracking_state.child_child_child_frame = backtracking_state.child_child_frame;
        if (backtracking_state.child_frame.valid) backtracking_state.child_child_frame = backtracking_state.child_frame;
        if (backtracking_state.current_frame.valid) backtracking_state.child_frame = backtracking_state.current_frame;
        store_current_c_frame_values(cursor, &backtracking_state);
        //if (backtracking_state.frame_count > 1 ) test_step_cursor_from_frame(w,  &(backtracking_state.child_frame), &(backtracking_state.current_frame));

        // if (backtracking_state.signal_rip == backtracking_state.current_frame.rip) {
        //     test_func();
        // }

        //Terminate if rsp is off the fiber
        if ((backtracking_state.current_frame.rsp >= ((uintptr_t) cl->fiber->stack_high)) || (backtracking_state.current_frame.rsp < ((uintptr_t)cl->fiber->stack_low))) {
            backtracking_state.current_frame.is_in_fiber = false;
        } else {
            backtracking_state.current_frame.is_in_fiber = true;
        }


        if (backtracking_state.current_frame.rip == 0) {
            fstate = NULL_RIP;
            break;
        }
        if (check_for_pop_state_in_list(backtracking_state.current_frame.rip, w)) {
            fstate = POP_STATE;
            // printf("Found pop state and broke\n");
            break;
        }
        
        //This checks if the last frame was a signal frame - in which case we can't steal from it so we just update the frame pointer and continue
        // if (backtracking_state.current_frame.is_signal) {
        //     backtracking_state.seen_frame = backtracking_state.frame_count;
        //     continue;
        // }

        // if (backtracking_state.child_frame.valid && backtracking_state.child_frame.is_signal) continue;

        // //We need to see at least 1 signal frame before we're exiting the backtrack
        // if (!backtracking_state.seen_frame) {
        //     continue;
        // }

        if (!backtracking_state.current_frame.is_in_fiber) {
            fstate = OUTSIDE_FIBER;
            break;
        }

        if (*(((void**) backtracking_state.current_frame.rsp)-1) != (void*) backtracking_state.current_frame.rip) {
            fstate = MISMATCHED_FRAME;
            break;
        }



        //If we backtrack off the stack, stop
        CILK_ASSERT(w, backtracking_state.current_frame.rip != 0);

        //Check whether we're at a spawn and record the information if so
        // printf("Checking 0x%lx with rbp 0x%lx\n", pc, bp);
        __spawn_state_registration* tmp_spawn = check_for_spawn_state_in_list(backtracking_state.current_frame.rip, w);
        if (tmp_spawn) {
            CILK_BACKTRACK_INCR_CILK(w);
            store_current_spawn_frame_value(w, cursor, &backtracking_state, tmp_spawn);
            // printf("Found tmp spawn at 0x%lx with old_rbp 0x%lx\n", latest_regstate.registers[UNW_X86_64_RBP], latest_regstate.old_rbp);
#if CILK_BACKTRACK_CACHE_SIZE
            if (!backtracking_state.previous_spawn.valid) { //If this is the first spawn we've found
                initialize_victim_cache(victim_w);
            }
            if (victim_w->l->stored_cache_count < CILK_BACKTRACK_CACHE_SIZE) { //if we haven't reached capacity yet, mark that we're adding another cache frame
                increment_victim_cache(victim_w);
            } else {
                shift_victim_cache(victim_w, &backtracking_state);
            }
            insert_new_cache_spawn(victim_w, &backtracking_state);
#endif
        }

        //If we haven't found a spawn yet or we're at a spawn we just found
//TODO update caching to new version
#if CILK_BACKTRACK_CACHE_SIZE
        if (((!backtracking_state.latest_spawn.valid)) || (backtracking_state.uncached_regstate && (backtracking_state.held_retptr != backtracking_state.latest_uncached_regstate.ret_addr))) {
            if (backtracking_state.uncached_regstate) {
                void** tmp_retptr = ((void**) backtracking_state.latest_uncached_regstate.ret_addr);
                void* tmp_retfunc = (void*) backtracking_state.latest_uncached_regstate.resume_addr;
                //if (tmp_retfunc != (void*) latest_uncached_regstate.registers[UNW_X86_64_RIP]) printf("frame: %d\n", frame_count);
                //CILK_ASSERT(w, tmp_retfunc == (void*) latest_uncached_regstate.registers[UNW_X86_64_RIP]);
                CILK_ASSERT(w, tmp_retfunc != holding_function);
                //Set the return address to the holding function
                __atomic_store_n(tmp_retptr, holding_function, __ATOMIC_SEQ_CST);
                //If the polling flag is still set, unset it to free the victim
                if (victim_w->l->context.poll_flag) {
                    __atomic_store_n(&(victim_w->l->context.poll_flag), 0, __ATOMIC_SEQ_CST);
                }
                //If an older frame is still being held, set it to the held return function
                if (backtracking_state.held_retptr) {
                    __atomic_store_n(backtracking_state.held_retptr, backtracking_state.held_retfunc, __ATOMIC_SEQ_CST);
                }
                //Set the held return function to the current frame
                victim_w->l->old_held_func = backtracking_state.held_retfunc;
                backtracking_state.held_retptr = tmp_retptr;
                backtracking_state.held_retfunc = tmp_retfunc;
                victim_w->l->held_func = backtracking_state.held_retfunc;
            } else if (!backtracking_state.latest_spawn.valid) {
                void** tmp_retptr = ((void**) backtracking_state.current_frame.rsp) - 1;
                void* tmp_retfunc = *tmp_retptr;
                if (tmp_retfunc != (void*) backtracking_state.current_frame.rip) {
                    CILK_ASSERT(w, !backtracking_state.latest_spawn.valid);
                } else {
                    CILK_ASSERT(w, tmp_retfunc != holding_function);
                    //Set the return address to the holding function
                    __atomic_store_n(tmp_retptr, holding_function, __ATOMIC_SEQ_CST);
                    //If the polling flag is still set, unset it to free the victim
                    if (victim_w->l->context.poll_flag) {
                        __atomic_store_n(&(victim_w->l->context.poll_flag), 0, __ATOMIC_SEQ_CST);
                    }
                    //If an older frame is still being held, set it to the held return function
                    if (backtracking_state.held_retptr) {
                        __atomic_store_n(backtracking_state.held_retptr, backtracking_state.held_retfunc, __ATOMIC_SEQ_CST);
                    }
                    //Set the held return function to the current frame
                    victim_w->l->old_held_func = backtracking_state.held_retfunc;
                    backtracking_state.held_retptr = tmp_retptr;
                    backtracking_state.held_retfunc = tmp_retfunc;
                    victim_w->l->held_func = backtracking_state.held_retfunc;
                }
            }
        }
#else
        // if ((!backtracking_state.latest_spawn.valid)) {
        //     void** tmp_retptr = ((void**) backtracking_state.current_frame.rsp) - 1;
        //     void* tmp_retfunc = *tmp_retptr;
        //     if (tmp_retfunc != (void*) backtracking_state.current_frame.rip || backtracking_state.child_frame.is_signal) {
        //         CILK_ASSERT(w, !backtracking_state.latest_spawn.valid);
        //     } else {
        //         backtracking_state.current_frame.handpassed = true;
        //         CILK_ASSERT(w, tmp_retfunc != holding_function);
        //         //Set the return address to the holding function
        //         __atomic_store_n(tmp_retptr, holding_function, __ATOMIC_SEQ_CST);
        //         //If the polling flag is still set, unset it to free the victim
        //         if (victim_w->l->context.poll_flag) {
        //             __atomic_store_n(&(victim_w->l->context.poll_flag), 0, __ATOMIC_SEQ_CST);
        //         }
        //         //If an older frame is still being held, set it to the held return function
        //         if (backtracking_state.held_retptr) {
        //             __atomic_store_n(backtracking_state.held_retptr, backtracking_state.held_retfunc, __ATOMIC_SEQ_CST);
        //         }
        //         //Set the held return function to the current frame
        //         victim_w->l->old_held_func = backtracking_state.held_retfunc;
        //         backtracking_state.held_retptr = tmp_retptr;
        //         backtracking_state.held_retfunc = tmp_retfunc;
        //         victim_w->l->held_func = backtracking_state.held_retfunc;
        //     }
        // }
#endif

        //If we're at the end of the stack, break out
        
        // if (bp == (uintptr_t) cl->functional_rbp) {
        //     break;
        // }

        // if (bp > (uintptr_t) cl->functional_rbp) {
        //     //printf("Went past functional_rbp(0x%lx) ???: 0x%lx\n", (uintptr_t) cl->functional_rbp, bp);
        //     break;
        // }
    };
    // if (!seen_frame) {
    //     printf("Never saw an interupt frame\n");
    // }
    if (backtracking_state.latest_spawn.valid) {

        //Get the frame pointer from the latest spawn and the address of the return pointer
        uintptr_t sbp = backtracking_state.latest_spawn.regstate.registers[UNW_X86_64_RBP];
        uintptr_t child_sbp = backtracking_state.latest_spawn.regstate.old_rbp;
        void** victim_retaddr = backtracking_state.latest_spawn.regstate.ret_addr;

#if CILK_BACKTRACK_LOW_OVERHEAD_HEURISTIC
        *((volatile int*) & (victim_w->steal_counter)) += 1;
#endif
        //The last spawn should match the held return pointer
        //CILK_ASSERT(w, victim_retaddr == held_retptr);
        backtracking_state.latest_spawn.regstate.registers[UNW_X86_64_RIP] = backtracking_state.latest_spawn.spawn_state.slow_thief_address;
        //Overwrite the victim with the victim return address (pop pointer)
        __atomic_store_n(victim_retaddr, (void*) backtracking_state.latest_spawn.spawn_state.slow_return_address, __ATOMIC_SEQ_CST);
    
        if (victim_w->l->context.poll_flag) {
            __atomic_store_n(&(victim_w->l->context.poll_flag), 0, __ATOMIC_SEQ_CST);
        }

        *res = extract_top_spawning_closure(w, victim_w, cl, sbp, child_sbp);
        cl = *res;


        if (cl->orig_rsp == NULL) {
            cl->orig_rsp = (void*)((uintptr_t) backtracking_state.latest_spawn.regstate.registers[UNW_X86_64_RSP]);
        }
        copy_regstate_to_regstate(&(backtracking_state.latest_spawn.regstate), &(cl->regstate));
        __cilkrts_set_unsynced(cl);
        Closure_make_ready(cl);


        cl->jump_cursor_ready = 1;
#if CILK_BACKTRACK_CACHE_SIZE
        int scc = __atomic_fetch_sub(&(victim_w->l->stored_cache_count), 1, __ATOMIC_SEQ_CST);
        CILK_ASSERT(w, victim_w->l->regstate_cache[victim_w->l->stolen_cache_count].valid);
#if BACKTRACK_CACHE_DEBUG
        if (!compare_preserved_regstates(&latest_regstate, &(victim_w->l->regstate_cache[victim_w->l->stolen_cache_count].regstate))) {
            printf("Cached: \n");
            print_regstate(&(victim_w->l->regstate_cache[victim_w->l->stolen_cache_count].regstate));
            printf("Backtracked: \n");
            print_regstate(&latest_regstate);
            print_regstate_cache(victim_w);
            CILK_ASSERT(w, compare_preserved_regstates(&latest_regstate, &(victim_w->l->regstate_cache[victim_w->l->stolen_cache_count].regstate)));
        } else {
            print_regstate_cache(victim_w);
            printf("Stole successfully\n");
        }
#endif
        victim_w->l->regstate_cache[victim_w->l->stolen_cache_count++].valid = false;
#if BACKTRACK_CACHE_DEBUG
        print_regstate_cache(victim_w);
#endif
        CILK_ASSERT(w, scc >= 0);
#endif
    }

    // if (!backtracking_state.latest_spawn.valid) {
    //     printf("Couldn't find work with victim{sp[%d] st[%d]}\n", victim_w->spawn_counter, victim_w->steal_counter);
    // }

    //If we still have a holding function, free it
    CILK_ASSERT(w, backtracking_state.held_retfunc != holding_function);
    if (backtracking_state.held_retptr && (*(backtracking_state.held_retptr) == holding_function)) {
        __atomic_store_n(backtracking_state.held_retptr, (void*) backtracking_state.held_retfunc, __ATOMIC_SEQ_CST);
        victim_w->l->old_held_func = NULL;
        victim_w->l->held_func = NULL;
    }

    CILK_ASSERT(w, !backtracking_state.held_retptr || (*(backtracking_state.held_retptr) != holding_function));
    __atomic_store_n(&(victim_w->l->context.poll_flag), 0, __ATOMIC_SEQ_CST);

    //Return failure
    if (!backtracking_state.latest_spawn.valid) {
        return fstate;
    }

    //Return success
    return SUCCESS;
}

// #define READ_CAST_PTR(type, pointer) *((type *) pointer)
// #define INCR_CAST_PTR(type, pointer) pointer += sizeof(type)

// static
// uintptr_t
// readULEB128(const uint8_t** data)
// {
//     uintptr_t result = 0;
//     uintptr_t shift = 0;
//     unsigned char byte;
//     const uint8_t *p = *data;
//     do
//     {
//         byte = *p++;
//         result |= (uintptr_t)((byte & 0x7F) << shift);
//         shift += 7;
//     } while (byte & 0x80);
//     *data = p;
//     return result;
// }

// static
// intptr_t
// readSLEB128(const uint8_t** data)
// {
//     uintptr_t result = 0;
//     uintptr_t shift = 0;
//     unsigned char byte;
//     const uint8_t *p = *data;
//     do
//     {
//         byte = *p++;
//         result |= (uintptr_t)((byte & 0x7F) << shift);
//         shift += 7;
//     } while (byte & 0x80);
//     *data = p;
//     if ((byte & 0x40) && (shift < (sizeof(result) << 3)))
//         result |= (uintptr_t)((~0) << shift);
//     return (uintptr_t)(result);
// }



// static
// uintptr_t
// readEncodedPointer(const uint8_t** data, uint8_t encoding)
// {
//     uintptr_t result = 0;
//     if (encoding == DW_EH_PE_omit)
//         return result;
//     const uint8_t* p = *data;
//     // first get value
//     switch (encoding & 0x0F)
//     {
//     case DW_EH_PE_absptr:
//         result = READ_CAST_PTR(uintptr_t, p);
//         INCR_CAST_PTR(uintptr_t, p);
//         break;
//     case DW_EH_PE_uleb128:
//         result = readULEB128(&p);
//         break;
//     case DW_EH_PE_sleb128:
//         result = (uintptr_t)(readSLEB128(&p));
//         break;
//     case DW_EH_PE_udata2:
//         result = READ_CAST_PTR(uint16_t, p);
//         INCR_CAST_PTR(uint16_t, p);
//         break;
//     case DW_EH_PE_udata4:
//         result = READ_CAST_PTR(uint32_t, p);
//         INCR_CAST_PTR(uint32_t, p);
//         break;
//     case DW_EH_PE_udata8:
//         result = READ_CAST_PTR(uint64_t, p);
//         INCR_CAST_PTR(uint64_t, p);
//         break;
//     case DW_EH_PE_sdata2:
//         result = READ_CAST_PTR(int16_t, p);
//         INCR_CAST_PTR(int16_t, p);
//         break;
//     case DW_EH_PE_sdata4:
//         result = READ_CAST_PTR(int32_t, p);
//         INCR_CAST_PTR(int32_t, p);
//         break;
//     case DW_EH_PE_sdata8:
//         result = READ_CAST_PTR(int64_t, p);
//         INCR_CAST_PTR(int64_t, p);
//         break;
//     default:
//         // not supported
//         abort();
//         break;
//     }
//     // then add relative offset
//     switch (encoding & 0x70)
//     {
//     case DW_EH_PE_absptr:
//         // do nothing
//         break;
//     case DW_EH_PE_pcrel:
//         if (result)
//             result += (uintptr_t)(*data);
//         break;
//     case DW_EH_PE_textrel:
//     case DW_EH_PE_datarel:
//     case DW_EH_PE_funcrel:
//     case DW_EH_PE_aligned:
//     default:
//         // not supported
//         abort();
//         break;
//     }
//     // then apply indirection
//     if (result && (encoding & DW_EH_PE_indirect))
//         result = *((uintptr_t*)result);
//     *data = p;
//     return result;
// }

// uintptr_t _Unwind_GetRegionStart(unw_cursor_t *context) {
//     unw_proc_info_t frameInfo;
//     uintptr_t result = 0;
//     if (unw_get_proc_info(context, &frameInfo) == UNW_ESUCCESS)
//         result = (uintptr_t)frameInfo.start_ip;
//     return result;
// }

// unsigned long _Unwind_GetIP (unw_cursor_t *context)
// {
//     unw_word_t val;

//     unw_get_reg(context, UNW_REG_IP, &val);
//     return val;
// }

// unsigned long _Unwind_GetLanguageSpecificData(unw_cursor_t *context) {
//     unw_proc_info_t pi;
//     pi.lsda = 0;
//     unw_get_proc_info (context, &pi);
//     return pi.lsda;
// }

// void print_personality_cache(__cilkrts_worker* const w, struct spawn_item* spawn_cache) {
//     printf("[%d] CACHE \n", w->self);
//     for (int i = 0; i < PERSONALITY_CACHE_SIZE; i++) {
//         printf("\t\tL:%08lx V:%x U:%04x", spawn_cache[i].location, spawn_cache[i].personality_val, spawn_cache[i].use_count);
//     }

// }

// int personality_lookup(walker_state * context, uintptr_t ip, __cilkrts_worker* const w) {
//     struct spawn_item* spawn_cache = w->l->spawn_cache;
//     for (int i = 0; i < PERSONALITY_CACHE_SIZE; i++) {
//         if (ip == spawn_cache[i].location) {
//             spawn_cache[i].use_count = ++(w->l->cache_count);
//             return spawn_cache[i].personality_val;
//         }
//     }
//     int personality_val = scan_for_handler(context);
//     int min_index = 0;
//     int min_val = INT_MAX;
//     for (int i = 0; i < PERSONALITY_CACHE_SIZE; i++) {
//         if (spawn_cache[i].use_count < min_val) {
//             min_val = spawn_cache[i].use_count;
//             min_index = i;
//         }
//     }
//     spawn_cache[min_index].personality_val = personality_val;
//     spawn_cache[min_index].location = ip;
//     spawn_cache[min_index].use_count = ++(w->l->cache_count);
//     return personality_val;
// }

// int scan_for_handler(unw_cursor_t * context) {
//     const uint8_t *lsda = (const uint8_t *)_Unwind_GetLanguageSpecificData(context);
//     if (lsda == 0)
//     {
//         return 0;
//     }
//     uintptr_t ip = _Unwind_GetIP(context) - 1;
//     uintptr_t funcStart = _Unwind_GetRegionStart(context);
//     uintptr_t ipOffset = ip - funcStart;
//     // Note: See JITDwarfEmitter::EmitExceptionTable(...) for corresponding
//     //       dwarf emission
//     // Parse LSDA header.
//     uint8_t lpStartEncoding = *lsda++;
//     const uint8_t* lpStart = (const uint8_t*)readEncodedPointer(&lsda, lpStartEncoding);
//     if (lpStart == 0)
//         lpStart = (const uint8_t*)funcStart;

//     // Walk call-site table looking for range that
//     // includes current PC.
//     uint8_t callSiteEncoding = *lsda++;
//     uint32_t callSiteTableLength = (uint32_t) (readULEB128(&lsda));
//     const uint8_t* callSiteTableStart = lsda;
//     const uint8_t* callSiteTableEnd = callSiteTableStart + callSiteTableLength;
//     const uint8_t* actionTableStart = callSiteTableEnd;
//     const uint8_t* callSitePtr = callSiteTableStart;

//     while (callSitePtr < callSiteTableEnd)
//     {
//         // There is one entry per call site.
//         // The call sites are non-overlapping in [start, start+length)
//         // The call sites are ordered in increasing value of start
//         uintptr_t start = readEncodedPointer(&callSitePtr, callSiteEncoding);
//         uintptr_t length = readEncodedPointer(&callSitePtr, callSiteEncoding);
//         uintptr_t landingPad = readEncodedPointer(&callSitePtr, callSiteEncoding);
//         uintptr_t actionEntry = readULEB128(&callSitePtr);
//         if ((start <= ipOffset) && (ipOffset < (start + length)))
//         {
//             // Found the call site containing ip.
//             if (landingPad == 0)
//             {
//                 return 0;
//             }
//             landingPad = (uintptr_t)lpStart + landingPad;
//             if (actionEntry == 0)
//             {
//                 return 0;
//             }

//             // Scan action entries until you find a matching handler, cleanup, or the end of action list
//             const uint8_t* action = actionTableStart + (actionEntry - 1);
//             while (true)
//             {
//                 int64_t ttypeIndex = readSLEB128(&action);
//                 if (ttypeIndex > 0)
//                 {
//                     return 1;
//                 }
//                 else if (ttypeIndex < 0)
//                 {
//                     return 0;
//                 }
//                 const uint8_t* temp = action;
//                 int64_t actionOffset = readSLEB128(&temp);
//                 if (actionOffset == 0)
//                 {
//                     return 0;
//                 }
//                 action += actionOffset;

//             }
//         }
//     }
//     return 0;
// }


#endif