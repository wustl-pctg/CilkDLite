#pragma once
#define _GNU_SOURCE
#include <stdint.h>
#include "rts-config.h"
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#if CILK_BACKTRACK_STACK_WALKER
typedef struct {
    uintptr_t rip;
    uintptr_t rsp;
    uintptr_t rbp;
} walker_state;
#else
typedef unw_cursor_t walker_state;
#endif
#include "jmpbuf.h"

#define USING_EXCEPTION_IDENTIFY 1
#define CILK_SPAWN_SYMBOL_TOKEN "__CILK_SPAWN_PREFIX__"
#ifdef USING_UNWIND_STEAL_STRUCTS

#define VICTIM_RESUME 1

typedef struct __cilkrts_worker __cilkrts_worker;
typedef struct __cilkrts_stack_frame __cilkrts_stack_frame;
typedef struct Closure Closure;
typedef struct __cilkrts_regstate __cilkrts_regstate;
typedef struct __cilkrts_regstate_cache_item __cilkrts_regstate_cache_item; 

struct __cilkrts_regstate {
	uintptr_t registers[UNW_TDEP_LAST_REG+1];
    uintptr_t old_rbp;
    uintptr_t pop_addr;
    uintptr_t resume_addr;
    uintptr_t thief_retaddr;
    void** ret_addr;
};

struct __cilkrts_regstate_cache_item {
    __cilkrts_regstate regstate;
    int spawned_cache;
    int valid;
};

#include "spawn-table.h"


struct __cilkrts_regstate_debug {
    uint64_t UNW_X86_64_RAX;
    uint64_t UNW_X86_64_RDX;
    uint64_t UNW_X86_64_RCX;
    uint64_t UNW_X86_64_RBX;
    uint64_t UNW_X86_64_RSI;
    uint64_t UNW_X86_64_RDI;
    uint64_t UNW_X86_64_RBP;
    uint64_t UNW_X86_64_RSP;
    uint64_t UNW_X86_64_R8;
    uint64_t UNW_X86_64_R9;
    uint64_t UNW_X86_64_R10;
    uint64_t UNW_X86_64_R11;
    uint64_t UNW_X86_64_R12;
    uint64_t UNW_X86_64_R13;
    uint64_t UNW_X86_64_R14;
    uint64_t UNW_X86_64_R15;
    uint64_t UNW_X86_64_RIP;
};
#endif

#ifdef SEQUENTIAL_EXECUTION
#define __CILK_FUNC_PREFIX
#define __CILK_FUNC_SUFFIX
#define __CILK_FUNC_SYNC
#define __CILK_FUNC_SPAWN(jmp_lbl, ...) __VA_ARGS__
#elif USING_EXCEPTION_IDENTIFY
int scan_for_handler(unw_cursor_t *context);
enum
{
    DW_EH_PE_absptr   = 0x00,
    DW_EH_PE_uleb128  = 0x01,
    DW_EH_PE_udata2   = 0x02,
    DW_EH_PE_udata4   = 0x03,
    DW_EH_PE_udata8   = 0x04,
    DW_EH_PE_sleb128  = 0x09,
    DW_EH_PE_sdata2   = 0x0A,
    DW_EH_PE_sdata4   = 0x0B,
    DW_EH_PE_sdata8   = 0x0C,
    DW_EH_PE_pcrel    = 0x10,
    DW_EH_PE_textrel  = 0x20,
    DW_EH_PE_datarel  = 0x30,
    DW_EH_PE_funcrel  = 0x40,
    DW_EH_PE_aligned  = 0x50,
    DW_EH_PE_indirect = 0x80,
    DW_EH_PE_omit     = 0xFF
};
struct __internal_unwind_context {
  unw_cursor_t cursor;
  int end_of_stack;
};
typedef struct __internal_unwind_context __internal_unwind_context;
struct __internal_unwind_exception {
        uint64_t           exception_class;
        void* exception_cleanup;
        uint64_t           private_1;
        uint64_t           private_2;
};
typedef struct __internal_unwind_exception __internal_unwind_exception;
#define __CILK_FUNC_PREFIX dummy(alloca(ZERO)); __cilkrts_enter_frame();
#define __CILK_FUNC_SUFFIX __cilk_parent_epilogue();
#define __CILK_FUNC_SYNC __cilk_sync_nothrow();
#define __CILK_FUNC_SPAWN(jmp_lbl, ...)   \
                                do { \
                                asm volatile goto ("" : : : : jmp_lbl);\
                                __VA_ARGS__\
                                __cilk_self_epilogue();\
                                asm(CILK_SPAWN_SYMBOL_TOKEN #jmp_lbl "_cont:");\
                                jmp_lbl:\
/*correctness*/                 __asm__ volatile("" : : :   "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",\
                                                            "xmm0","xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",\
                                                            "xmm8","xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",\
                                                            "mm0","mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6",\
                                                            "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");\
                                } while (0);

#else
#define __CILK_FUNC_PREFIX dummy(alloca(ZERO));__cilkrts_stack_frame sf;__cilkrts_enter_frame(&sf);
#define __CILK_FUNC_SUFFIX __cilk_parent_epilogue(&sf);
#define __CILK_FUNC_SYNC __cilk_sync_nothrow(&sf);
#define __CILK_FUNC_SPAWN(jmp_lbl, ...)   \
                                do { \
                                sf.pc = &&jmp_lbl;\
/*correctness*/                 int cilk_func_spawn_tmp_zero;\
/*correctness*/                 __asm__ volatile("movl $0, %0" : "=r"(cilk_func_spawn_tmp_zero)); \
                                __cilk_prepare_spawn(&sf);\
/*correctness*/                 if (__builtin_expect(cilk_func_spawn_tmp_zero, 0)) goto jmp_lbl;\
                                __cilkrts_detach_self(&sf);\
                                __VA_ARGS__\
                                __cilk_self_epilogue(&sf);\
                                jmp_lbl:\
/*correctness*/                 __asm__ volatile("" : : : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11");\
                                sf.flags &= ~CILK_FRAME_SPAWNER; \
                                } while (0);
#endif
#define BACKTRACK_GET_RSP(osp) __asm__ volatile("movq %%rsp, %0" : "=r"(osp))
#define BACKTRACK_GET_RBP(osp) __asm__ volatile("movq %%rbp, %0" : "=r"(osp))

typedef enum {
    SUCCESS = 1,
    NONE = 0,
    NULL_RIP  = -1,
    POP_STATE = -2,
    OUTSIDE_FIBER = -3,
    MISMATCHED_FRAME = -4
} backtracking_return_state;


#ifdef USING_UNWIND_STEALS
void __record_regstate(ucontext_t *uc);

static inline __attribute__((always_inline)) void
save_saved_regstate(__cilkrts_regstate* regstate) {
    asm volatile ("movq %%rbx, 0x18(%0)" : : "r"(regstate)); //3
    asm volatile ("movq %%rbp, 0x30(%0)" : : "r"(regstate)); //6
    asm volatile ("movq %%rsp, 0x38(%0)" : : "r"(regstate)); //7
    asm volatile ("movq %%r12, 0x60(%0)" : : "r"(regstate)); //12
    asm volatile ("movq %%r13, 0x68(%0)" : : "r"(regstate)); //13
    asm volatile ("movq %%r14, 0x70(%0)" : : "r"(regstate)); //14
    asm volatile ("movq %%r15, 0x78(%0)" : : "r"(regstate)); //15
}




int compare_preserved_regstates(__cilkrts_regstate* start, __cilkrts_regstate* finish);
__attribute__((noreturn)) void jump_to_regstate(__cilkrts_regstate*);
__attribute__((noinline)) int backtrack_setjmp(__cilkrts_regstate* regstate);
__attribute__((noreturn)) void backtrack_longjmp(__cilkrts_regstate* regstate, int retval);
void copy_cursor(walker_state*, walker_state*);
void copy_cursor_to_regstate(walker_state* src, __cilkrts_regstate* dst);
void copy_regstate_to_cursor(__cilkrts_regstate* src, walker_state* dst);
void copy_regstate_to_regstate(__cilkrts_regstate* src, __cilkrts_regstate* dst);
//void compare_regstate_to_jmpbuf(__cilkrts_regstate* src, jmpbuf jump_buffer);
int do_jumpbuf_set_backtrace (__cilkrts_worker *const w, __cilkrts_worker *const victim_w, walker_state* cursor, Closure* cl, Closure** res);
void print_regstate(__cilkrts_regstate* regstate);
int personality_lookup(walker_state * context, uintptr_t ip, __cilkrts_worker* const w);
#endif
