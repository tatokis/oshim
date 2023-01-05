#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <link.h>
#include <inttypes.h>
#include <gnu/lib-names.h>
#include <string.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <execinfo.h>

// Green colour
#define IN_SHIM_C

#include "inlines.h"
#include "structs.h"

#include <malloc.h>

#define GLIBC_VER "GLIBC_2.2.5"

// Used to prevent infinite recursion when calling Xlib functions from within xcb (Xlib calls xcb)
// Maybe instead of this we should just use steam's dlsym which will resolve them to its own functions
#define XLIBGUARD if(!is_function_from(__builtin_extract_return_addr(__builtin_return_address(0)), "libX11.so"))

void* (*dlopen_real)(const char* filename, int flags) = NULL;
void* (*dlsym_real)(void* handle, const char* symbol) = NULL;
static void* (*dlsym_steam)(void* handle, const char* symbol) = NULL;
void* xcb_handle = NULL;
static void* libc_handle = NULL;
extern void* xlib_handle;

// From XCB
// Maaaybe rewrite
/*static const int xcb_con_error = XCB_CONN_ERROR;
static const int xcb_con_closed_mem_er = XCB_CONN_CLOSED_MEM_INSUFFICIENT;
static const int xcb_con_closed_parse_er = XCB_CONN_CLOSED_PARSE_ERR;
static const int xcb_con_closed_screen_er = XCB_CONN_CLOSED_INVALID_SCREEN;

static int is_static_error_conn(xcb_connection_t *c)
{
    return c == (xcb_connection_t *) &xcb_con_error ||
           c == (xcb_connection_t *) &xcb_con_closed_mem_er ||
           c == (xcb_connection_t *) &xcb_con_closed_parse_er ||
           c == (xcb_connection_t *) &xcb_con_closed_screen_er;
}*/

// FIXME: Possibly requires locking just for these
//_Atomic
/*static size_t xcb_size;
static inline FakeDisplay* get_fakedp(xcb_connection_t* conn)
{
    if(!xcb_size)
        abort();
    FakeDisplay* f;
    memcpy(&f, (void*)((uintptr_t)conn + xcb_size), sizeof(f));
    return f;
}*/

static FakeDisplay* FakeDisplays[64];
static inline FakeDisplay* add_to_fakedps(FakeDisplay* f)
{
    for(size_t i = 0; i < sizeof(FakeDisplays)/sizeof(*FakeDisplays); i++)
    {
        if(FakeDisplays[i])
            continue;
        FakeDisplays[i] = f;
        return f;
    }
    abort();
}

static inline FakeDisplay* get_fakedp(xcb_connection_t* conn)
{
    for(size_t i = 0; i < sizeof(FakeDisplays)/sizeof(*FakeDisplays); i++)
    {
        if(FakeDisplays[i] && FakeDisplays[i]->xcbconn == conn)
            return FakeDisplays[i];
    }
    abort();
}

static inline FakeDisplay* rem_from_fakedps(FakeDisplay* f)
{
    for(size_t i = 0; i < sizeof(FakeDisplays)/sizeof(*FakeDisplays); i++)
    {
        if(FakeDisplays[i] == f)
        {
            FakeDisplays[i] = NULL;
            return f;
        }
    }
    abort();
}

// Setup function
void setup_reentrant()
{
    // We have to bypass steam's dlsym and dlopen implementations
    if(!dlopen_real || !dlsym_real)
    {
        // We can't use dlopen because steam overrides it and we end up calling our own dlsym
        void* handle = libc_handle = dlmopen(LM_ID_BASE, LIBDL_SO, RTLD_NOW);
        // We have to use dlvsym here to avoid calling our own dlsym
        dlopen_real = dlvsym(handle, "dlopen", GLIBC_VER);
        dlsym_real = dlvsym(handle, "dlsym", GLIBC_VER);
        if(!dlopen_real || !dlsym_real)
            puts("Could not find dlopen or dlsym, most likely invalid glibc version. Incoming segfault!");
    }

    if(!xcb_handle)
        xcb_handle = dlopen_real("libxcb.so.1", RTLD_NOW);

    if(!dlsym_steam){
        dlsym_steam = dlsym_real(RTLD_NEXT, "dlsym");
        print_which_library(dlsym_steam);
    }

}

// Very hacky, this should not exist
// For the very off chance that it can avoid crashes when calling unimplemented Xlib functions
void catchall_fn()
{
    PM();
    // Spit out backtrace to know who called us and why
    void* bt____[6];
    const int len = backtrace(bt____, SZ(bt____));
    char** sym = backtrace_symbols(bt____, len);
    for(int i = 0; i < len; i++)
        printf("%d: %s\n", i, sym[i]);
}

// Custom dlsym to inject the fake Xlib functions
// FIXME: Make this return our own xcb functions as well just to be sure
void* dlsym(void* handle, const char* symbol)
{
    // Is this needed?
    setup_reentrant();

    // Call the appropriate dlsym
    void* retptr = NULL;
    if(handle == RTLD_NEXT)
    {
        // All X functions
        if(*symbol == 'X')
        {
            // Some Xlib functions we do not have to implement, so just pass them through
            // Eventually, this should be the default behaviour
            /*if(!strcmp(symbol, "XEventsQueued") || !strcmp(symbol, "XPutBackEvent") || !strcmp(symbol, "XSelectInput"))
            {
                //xlib_setup_reentrant();
                retptr = dlsym_real(xlib_handle, symbol);
                printf("OVERRIDE   (XLib)  dlsym for %-35s, found at %p\n", symbol, retptr);
            }
            else*/
            {
                char customsymbol[256];
                snprintf(customsymbol, sizeof(customsymbol), "%s_custom", symbol);
                retptr = dlsym_real(RTLD_DEFAULT, customsymbol);
                printf("Asked real (fakeX) dlsym for %-35s, found at %p\n", customsymbol, retptr);
                // If we couldn't find it, fall back to the real one with a big warning
                if(!retptr)
                {
                    retptr = catchall_fn;
                    printf("WARNING! Could not find fakeX %s, falling back to catchall_fn at %p\n", customsymbol, retptr);
                }
            }
        }
        else
        {
            // Problem is that RTLD_NEXT points back to steam...
            // Segfault because steam looks up open(), then proceeds to call its own open() inside its own open()...
            // HACK
            retptr = dlsym_real(libc_handle, symbol);
            printf("Asked real (libc)  dlsym for %-35s, found at %p\n", symbol, retptr);
        }
        if(!retptr){
            puts("Couldn't find requested symbol. Aborting.");
            abort();
        }
    }
    else
    {
        retptr = dlsym_steam(handle, symbol);
        printf("Asked steam's      dlsym for %-35s, found at %p\n", symbol, retptr);
    }
    return retptr;
}

//
// WARNING!
// All xcb_-* functions must have XLIBGUARD in them somewhere to handle Xlib calling xcb functions and expecting actual answers
//

// Used for init
xcb_connection_t* xcb_connect(const char* displayname, int* screenp)
{
    PM();
    // Use this as an init of sorts
    setup_reentrant();

    xcb_connection_t* (*xcb_connect_real)(const char* displayname, int* screenp) = dlsym_real(xcb_handle, "xcb_connect");

    xcb_connection_t* conn = xcb_connect_real(displayname, screenp);

    // It just keeps getting worse and worse
    // We can't figure out the size of xcb_connection_t, so we have to improvise
    /*if(!xcb_size)
        xcb_size = malloc_usable_size(conn);

    if(!xcb_size)
        abort();

    // We have to realloc before we actually try to use conn...
    if(!(conn = realloc(conn, xcb_size + sizeof(FakeDisplay*))))
        abort();*/

    // We do this unconditionally because most applications call XOpenDisplay themselves (which internally calls xcb_connect) and then get the XCB pointer from Xlib
    FakeDisplay* f = add_to_fakedps(calloc(1, sizeof(FakeDisplay)));
    f->magic1 = MAGIC1;
    f->magic2 = MAGIC2;
    f->xcbconn = conn;
    f->xcbsyms = xcb_key_symbols_alloc(conn);
    // FIXME: Fill in default_screen in _XDisplay. Steam seems to use it as we get garbage in an Xlib call otherwise

    /*memcpy((void*)((uintptr_t)conn + xcb_size), &f, sizeof(f));
    printf("FAKE IS %p CONN %p zulu %zu %zu\n", f, conn, xcb_size, malloc_usable_size(conn));*/


    // Do not call XOpenDisplay if xcb_connect was called by Xlib
    XLIBGUARD
    {
        // Add its pointer to the end of the fakestr passed to our custom XOpenDisplay
        char fakestr[FAKE_DISPLAY_STR_LEN];
        const size_t maxstrlen = sizeof(fakestr) - sizeof(f);
        int bytes = snprintf(fakestr, maxstrlen, "%s", displayname ? displayname : ":0");
        if(bytes <= 0 || (size_t)bytes > maxstrlen)
            abort();

        memcpy(fakestr + maxstrlen, &f, sizeof(f));
        // Call XOpenDisplay() to simulate the application opening an X connection
        XOpenDisplay(fakestr);
    }

    return conn;
}

// Used for deinit
void xcb_disconnect(xcb_connection_t* c)
{
    PM();
    xcb_void_cookie_t (*xcb_disconnect_real)(xcb_connection_t* conn) = dlsym_real(xcb_handle, "xcb_disconnect");
    FakeDisplay* f = rem_from_fakedps(get_fakedp(c));

    XLIBGUARD
        XCloseDisplay((Display*)f);

    xcb_key_symbols_free(f->xcbsyms);

    free(f);
    xcb_disconnect_real(c);
}

// XCB overrides so that the application doesn't call into the overlay's xcb functions
xcb_void_cookie_t xcb_create_window(xcb_connection_t* conn,
              uint8_t depth, xcb_window_t wid, xcb_window_t parent, int16_t x,
              int16_t y, uint16_t width, uint16_t height,
              uint16_t border_width, uint16_t _class, xcb_visualid_t visual,
              uint32_t value_mask, const void *value_list)
{
    PM();

    xcb_void_cookie_t (*xcb_create_window_real)(
        xcb_connection_t* conn,
        uint8_t depth, xcb_window_t wid, xcb_window_t parent, int16_t x,
        int16_t y, uint16_t width, uint16_t height,
        uint16_t border_width, uint16_t _class, xcb_visualid_t visual,
        uint32_t value_mask, const void *value_list
    ) = dlsym_real(xcb_handle, "xcb_create_window");
    xcb_void_cookie_t ret = xcb_create_window_real(conn, depth, wid, parent, x, y, width, height, border_width, _class, visual, value_mask, value_list);
    // FIXME: visual not translated
    // FIXME: hopefully casting a non const to const won't make much of a difference here
    XLIBGUARD
    {
        // Hack because we somehow need to pass the window id as well
        FakeDisplay* f = get_fakedp(conn);
        f->extrawid = wid;
        XCreateWindow((Display*)f, parent, x, y, width, height, border_width, depth, _class, NULL, value_mask, (void*)value_list);
    }
    return ret;
}

xcb_void_cookie_t xcb_create_window_checked(xcb_connection_t* conn,
              uint8_t depth, xcb_window_t wid, xcb_window_t parent, int16_t x,
              int16_t y, uint16_t width, uint16_t height,
              uint16_t border_width, uint16_t _class, xcb_visualid_t visual,
              uint32_t value_mask, const void *value_list)
{
    PM();

    xcb_void_cookie_t (*xcb_create_window_checked_real)(
        xcb_connection_t* conn,
        uint8_t depth, xcb_window_t wid, xcb_window_t parent, int16_t x,
        int16_t y, uint16_t width, uint16_t height,
        uint16_t border_width, uint16_t _class, xcb_visualid_t visual,
        uint32_t value_mask, const void *value_list
    ) = dlsym_real(xcb_handle, "xcb_create_window_checked");
    // FIXME: visual not translated
    // FIXME: hopefully casting a non const to const won't make much of a difference here
    XLIBGUARD
    {
        // Hack because we somehow need to pass the window id as well
        FakeDisplay* f = get_fakedp(conn);
        f->extrawid = wid;
        XCreateWindow((Display*)f, parent, x, y, width, height, border_width, depth, _class, NULL, value_mask, (void*)value_list);
    }
    return xcb_create_window_checked_real(conn, depth, wid, parent, x, y, width, height, border_width, _class, visual, value_mask, value_list);
}

xcb_void_cookie_t xcb_map_window(xcb_connection_t* conn, xcb_window_t window)
{
    PM();
    xcb_void_cookie_t (*xcb_map_window_real)(xcb_connection_t *conn, xcb_window_t window) = dlsym_real(xcb_handle, "xcb_map_window");
    XLIBGUARD
        XMapWindow((Display*)conn, window);
    return xcb_map_window_real(conn, window);
}

xcb_void_cookie_t xcb_destroy_window(xcb_connection_t* conn, xcb_window_t window)
{
    xcb_void_cookie_t (*xcb_destroy_window_real)(xcb_connection_t* conn, xcb_window_t window) = dlsym_real(xcb_handle, "xcb_destroy_window");
    XLIBGUARD
    {
        FakeDisplay* f = get_fakedp(conn);
        XDestroyWindow((Display*)f, window);
    }
    return xcb_destroy_window_real(conn, window);
}

xcb_generic_event_t* xcb_wait_for_event(xcb_connection_t* c)
{
    PM();
    // We have the other end actually dequeue the event and return it to us. This is done because once the steam overlay kicks in
    // it starts calling XNextEvent expecting it to block, so we need xcb_wait_for_event() to block for us there
    xcb_generic_event_t* evt;
    XLIBGUARD
    {
        FakeDisplay* f = get_fakedp(c);
        // I don't think there's a need to malloc this
        CustomXEvent e = {0};
        XAnyEvent* anye = (XAnyEvent*)&e;
        anye->display = (Display*)f;
        XNextEvent((Display*)f, (XEvent*)&e);
        evt = e.xcbevt;
    }
    else
    {
        xcb_generic_event_t* (*xcb_wait_for_event_real)(xcb_connection_t* c) = dlsym_real(xcb_handle, "xcb_wait_for_event");
        evt = xcb_wait_for_event_real(c);
    }
    return evt;
}

// Unfortunately this needs to be stubbed.
// There is no easy/obvious way to query xcb for queued events without actually dequeueing them
// Waiting on the fd might do the trick, however that doesn't deal with already-queued data.
xcb_generic_event_t* xcb_poll_for_queued_event(xcb_connection_t* c)
{
    PM();
    XLIBGUARD
    {
        return NULL;
    }
    else
    {
        xcb_generic_event_t* (*xcb_poll_for_queued_event_real)(xcb_connection_t* c) = dlsym_real(xcb_handle, "xcb_poll_for_queued_event");
        return xcb_poll_for_queued_event_real(c);
    }
}

// We can use the fd here
xcb_generic_event_t* xcb_poll_for_event(xcb_connection_t* c)
{
    PM();
    XLIBGUARD
    {
        puts("STUB");
        return NULL;
    }
    else
    {
        xcb_generic_event_t* (*xcb_poll_for_event_real)(xcb_connection_t* c) = dlsym_real(xcb_handle, "xcb_poll_for_event");
        return xcb_poll_for_event_real(c);
    }
}
