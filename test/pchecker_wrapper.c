#include "pchecker_wrapper.h"
#include <signal.h>

#if __GNUC__ >= 4
#define DSO_PUBLIC __attribute__((visibility("default")))
#else
#define DSO_PUBLIC
#endif

#ifdef __cplusplus
extern "C" {
#endif

static __thread int s_enableAssert;
static __thread void *s_AssertArg;

static pf_assert_callback_t s_pFAssertCallback;

void cobalt_assert_nrt()
{
    static __thread int s_Recurse;
    int rec = s_Recurse++;
    pf_assert_callback_t pf;

    if (s_enableAssert) {
        pf = s_pFAssertCallback;
        if (pf) {
            /* avoid recursion if the callback triggers an assert */
            if (rec == 0)
                (*pf)(s_AssertArg);
        }
        else
            raise(SIGXCPU);
    }
    --s_Recurse;
}

pf_assert_callback_t set_cobalt_assert_nrt(pf_assert_callback_t pf)
{
    pf_assert_callback_t pf_old = s_pFAssertCallback;
    s_pFAssertCallback = pf;
    return pf_old;
}

int enable_cobalt_assert_nrt_arg(int enable, int setArg, void *pArg)
{
    int old = s_enableAssert;
    if (setArg)
        s_AssertArg = pArg;
    s_enableAssert = ~~enable;
    return old;
}

void *get_cobalt_assert_nrt_arg()
{
    return s_AssertArg;
}

#ifdef __cplusplus
}
#endif
