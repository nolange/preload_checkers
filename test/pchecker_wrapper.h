#if __GNUC__ >= 4
#define DSO_PUBLIC __attribute__((visibility("default")))
#else
#define DSO_PUBLIC
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pf_assert_callback_t)(void *);

DSO_PUBLIC void cobalt_assert_nrt();
DSO_PUBLIC pf_assert_callback_t set_cobalt_assert_nrt(pf_assert_callback_t pf);
DSO_PUBLIC int enable_cobalt_assert_nrt_arg(int enable, int setArg, void *pArg);
DSO_PUBLIC void *get_cobalt_assert_nrt_arg();

#define enable_cobalt_assert_nrt(e) enable_cobalt_assert_nrt_arg(e, 0, NULL)

#ifdef __cplusplus
}
#endif
