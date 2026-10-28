#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unwind.h>

#include "cilk-internal.h"
#include "cilk2c.h"
#include "closure.h"
#include "readydeque.h"

typedef _Unwind_Reason_Code (*__personality_routine)(
    int version, _Unwind_Action actions, uint64_t exception_class,
    struct _Unwind_Exception *exception_object,
    struct _Unwind_Context *context);

static char *get_cfa(struct _Unwind_Context *context) {
    /* _Unwind_GetCFA is originally a gcc extension.  FreeBSD has its
       own library without that extension. */
#if defined(__linux__) || (defined(__APPLE__) && defined(__MACH__))
    return (char *)_Unwind_GetCFA(context);
#else
    /* See *RegisterInfo.td in LLVM source */
#ifdef __i386__
    int sp_regno = 5; /* unclear if 5 or 6 is right here */
#elif defined __x86_64__
    int sp_regno = 7;
#elif defined __aarch64__
    int sp_regno = 31;
#elif defined __arm__
    int sp_regno = 13;
#else
    /* Probably 14 for SPARC, 2 for RISCV, and 1 for PPC. */
#error "no CFA"
#endif
    return (char *)_Unwind_GetGR(context, sp_regno);
#endif
}

_Unwind_Reason_Code __cilk_personality_internal(
    __personality_routine std_lib_personality, int version,
    _Unwind_Action actions, uint64_t exception_class,
    struct _Unwind_Exception *ue_header, struct _Unwind_Context *context) {
        get_cfa(context);
        return _URC_FATAL_PHASE1_ERROR;
}
