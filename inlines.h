// Inline functions
#include <dlfcn.h>
#include <string.h>

// Debug
#if defined(IN_SHIM_XLIB_C)
#define PM() printf("\x1B[31mOSHIM %s\x1B[0m\n", __FUNCTION__)
#elif defined(IN_SHIM_C)
#define PM() printf("\x1B[32mOSHIM %s\x1B[0m\n", __FUNCTION__)
#else
#error "No file defined for debug colouring"
#endif

#define SZ(x) (sizeof(x)/sizeof(*x))

static inline void print_which_library(void* fn)
{
    Dl_info i;
    if(dladdr(fn, &i))
        printf("0x%p found at library 0x%p %-70s\n", fn, i.dli_fbase, i.dli_fname);
    else
        printf("Couldn't find library for 0x%p\n", fn);
}

static inline int is_function_from(void* fn, const char* const needle)
{
    Dl_info i;
    if(dladdr(fn, &i) && strstr(i.dli_fname, needle))
        return 1;
    return 0;
}

