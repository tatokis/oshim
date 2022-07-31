#define _GNU_SOURCE
#include <stdio.h>
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <execinfo.h>
// removeme
#include <dlfcn.h>

// Red colour
#define IN_SHIM_XLIB_C

#include "inlines.h"
#include "structs.h"

// shim.c
extern void setup_reentrant();
extern void* (*dlopen_real)(const char* filename, int flags);
extern void* (*dlsym_real)(void* handle, const char* symbol);

static void* xlib_handle = NULL;
extern void* xcb_handle;

static inline void xlib_setup_reentrant()
{
    PM();
    setup_reentrant();
    if(xlib_handle)
        return;
    xlib_handle = dlopen_real("libX11.so.6", RTLD_NOW);
}

// Walk up the stack and see if we are the ones who called the function
// Used only in select few calls for xcb applications for now
#define IS_FN_CALL_FROM_OSHIM \
    void* bt____[3]; \
    if(backtrace(bt____, SZ(bt____)) == SZ(bt____) && is_function_from(bt____[SZ(bt____)-1], "liboshim.so"))

// custom Xlib implementation
// WARNING: All functions here _MUST_ end in `_custom`
// If they don't, the shim will end up calling itself
// FIXME just make a macro to do it automatically


Display* XOpenDisplay_custom(_Xconst char* displayname)
{
    PM();
    xlib_setup_reentrant();

    IS_FN_CALL_FROM_OSHIM
    {
        // Extract FakeDisplay* from the string
        FakeDisplay* f;
        const size_t start = FAKE_DISPLAY_STR_LEN - sizeof(f);
        memcpy(&f, displayname + start, sizeof(f));
        printf("ptr %p \n", f);
        return (Display*)f;
    }
    else
    {
        Display* (*XOpenDisplay_real)(_Xconst char* displayname) = dlsym_real(xlib_handle, "XOpenDisplay");
        return XOpenDisplay_real(displayname);
    }
}

int XCloseDisplay_custom(Display* d)
{
    PM();
    //IS_FN_CALL_FROM_OSHIM
    // For some reason the steam overlay doesn't show up in the backtrace here
    if(is_function_from(__builtin_extract_return_addr(__builtin_return_address(0)), "liboshim.so"))
    {
        return 0;
    }
    else
    {
        int (*XCloseDisplay_real)(Display* d) = dlsym_real(xlib_handle, "XCloseDisplay");
        return XCloseDisplay_real(d);
    }

}

Window XCreateWindow_custom(Display* display, Window parent, int x, int y, unsigned int width, unsigned int height, unsigned int border_width, int depth, unsigned int class, Visual* visual, unsigned long valuemask, XSetWindowAttributes* attributes)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;
    return f->extrawid;
}

XErrorHandler XSetErrorHandler_custom(XErrorHandler handler)
{
    PM();
    // Do nothing
    return handler;
}

int XChangeProperty_custom(Display* display, Window w, Atom property, Atom type, int format, int mode, _Xconst unsigned char* data, int nelements)
{
    PM();
    // FIXME
    printf("XChangePropety_custom prop %lu type %lu\n", property, type);
    return 0;
}

Window XRootWindow_custom(Display* display, int screen_number)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;

    xcb_screen_iterator_t i = xcb_setup_roots_iterator(xcb_get_setup(f->xcbconn));
    for(int s = 0; s < screen_number; s++)
        xcb_screen_next(&i);

    return i.data->root;
}

Atom XInternAtom_custom(Display* display, _Xconst char* atom_name, Bool only_if_exists)
{
    PM();
    printf("Req Atom %s\n", atom_name);
    return None;
}

int XMapWindow_custom(Display* display, Window w)
{
    PM();
    // FIXME: Is the return value correct?
    return 1;
}

Status XGetGeometry_custom(Display *display, Drawable d, Window *root_return, int *x_return, int *y_return, unsigned  int  *width_return,  unsigned  int  *height_return,  unsigned int *border_width_return, unsigned int *depth_return)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;
    xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(f->xcbconn, xcb_get_geometry(f->xcbconn, d), NULL);
    if(!reply)
        return 0; // FIXME

    *root_return = reply->root;
    *x_return = reply->x;
    *y_return = reply->y;
    *width_return = reply->width;
    *height_return = reply->height;
    *border_width_return = reply->border_width;
    *depth_return = reply->depth;

    free(reply);
    return 1;
}

Status XQueryTree_custom(Display *display, Window w, Window *root_return, Window  *parent_return, Window  **children_return, unsigned int *nchildren_return)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;
    xcb_query_tree_reply_t* reply = xcb_query_tree_reply(f->xcbconn, xcb_query_tree(f->xcbconn, w), NULL);
    if(!reply)
        return 0;

    *root_return = reply->root;
    *parent_return = reply->parent;

    const int len = xcb_query_tree_children_length(reply);
    *nchildren_return = len;

    // Copy data over

    xcb_window_t* children = xcb_query_tree_children(reply);
    // Gets freed by XFree_custom()
    Window* array = *children_return = malloc(sizeof(Window)*len);

    for(int i = 0; i < len; i++)
        array[i] = children[i];

    free(reply);
    return 1;
}

int XFree_custom(void* data)
{
    PM();
    free(data);
    return 1;
}

int XDestroyWindow_custom(Display *display, Window w)
{
    PM();
    // Nothing else to do here
    return 1;
}

int XNextEvent_custom(Display* display, XEvent* event_return)
{
    PM();
    XAnyEvent* e = (XAnyEvent*)event_return;
    CustomXEvent* ce = (CustomXEvent*)event_return;

    xcb_generic_event_t* (*xcb_wait_for_event_real)(xcb_connection_t* c) = dlsym_real(xcb_handle, "xcb_wait_for_event");
    xcb_generic_event_t* evt = xcb_wait_for_event_real(((FakeDisplay*)display)->xcbconn);
    ce->xcbevt = evt;
    if(!evt)
        return 1;

    // Event type ids are the same between Xlib and XCB
    e->type = evt->response_type & ~0x80;
    e->serial = evt->sequence; // FIXME: Is this correct?
    // FIXME FIXME FIXME
    // How on earth do we get this?
    //e->window =
    return 0;
}

KeySym XLookupKeysym_custom(XKeyEvent* key_event, int col)
{
    PM();
    CustomXEvent* e = (CustomXEvent*)key_event;
    xcb_keysym_t sym = xcb_key_press_lookup_keysym(((FakeDisplay*)key_event->display)->xcbsyms, (xcb_key_press_event_t*)e->xcbevt, col);
    printf("SYM %d\n", sym);
    return sym;
}
