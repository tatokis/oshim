#define _GNU_SOURCE
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/XKBlib.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xinput.h>
#include <xcb/xkb.h>
#include <execinfo.h>
#include <stdint.h>
// removeme
#include <dlfcn.h>

#include <assert.h>

// Red colour
#define IN_SHIM_XLIB_C

#include "inlines.h"
#include "structs.h"

// FIXME: Try to get this from the server somehow
#define XKB_BASE_EVENT 85

// shim.c
extern void setup_reentrant();
extern void* (*dlopen_real)(const char* filename, int flags);
extern void* (*dlsym_real)(void* handle, const char* symbol);

void* xlib_handle = NULL;
extern void* xcb_handle;

static uint8_t xinput_opcode;

static inline void xlib_setup_reentrant()
{
    PM();
    setup_reentrant();
    if(xlib_handle)
        return;
    xlib_handle = dlopen_real("libX11.so.6", RTLD_NOW);
}

static inline double fp1616_to_double(xcb_input_fp1616_t v)
{
    return v / (double)UINT16_MAX;
}

static inline double fp3232_to_double(xcb_input_fp3232_t v)
{
    return v.integral + v.frac / (double)UINT32_MAX;
}

// Walk up the stack and see if we are the ones who called the function
// Used only in select few calls for xcb applications for now
#define IS_FN_CALL_FROM_OSHIM \
    void* bt____[3]; \
    if(backtrace(bt____, SZ(bt____)) == SZ(bt____) && is_function_from(bt____[SZ(bt____)-1], "liboshim.so"))

// Alternative that should most likely replace the above one in all almost all cases
#define IS_FN_CALL_FROM_OSHIM_MAGIC(d) if(((FakeDisplay*)d)->magic1 == MAGIC1 && ((FakeDisplay*)d)->magic2 == MAGIC2)

// custom Xlib implementation
// WARNING: All functions here _MUST_ end in `_custom`
// If they don't, the shim will end up calling itself
// FIXME just make a macro to do it automatically

Bool XQueryExtension_custom(Display* display, _Xconst char* name, int* major_opcode_return, int* first_event_return, int* first_error_return);
Display* XOpenDisplay_custom(_Xconst char* displayname)
{
    PM();
    xlib_setup_reentrant();

    // FIXME: Maybe add a magic start to the string here to avoid walking the stack
    IS_FN_CALL_FROM_OSHIM
    {
        // Extract FakeDisplay* from the string
        FakeDisplay* f;
        const size_t start = FAKE_DISPLAY_STR_LEN - sizeof(f);
        memcpy(&f, displayname + start, sizeof(f));
        printf("ptr %p \n", f);

        // Ask for XI2
        int dummy;
        XQueryExtension_custom((Display*)f, "XInputExtension", &dummy, &dummy, &dummy);
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
    //if(is_function_from(__builtin_extract_return_addr(__builtin_return_address(0)), "liboshim.so"))
    IS_FN_CALL_FROM_OSHIM_MAGIC(d)
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
    IS_FN_CALL_FROM_OSHIM_MAGIC(display)
    {
        FakeDisplay* f = (FakeDisplay*)display;
        return f->extrawid;
    }
    else
    {
        Window (*XCreateWindow_real)(Display* display, Window parent, int x, int y, unsigned int width, unsigned int height, unsigned int border_width, int depth, unsigned int class, Visual* visual, unsigned long valuemask, XSetWindowAttributes* attributes) = dlsym_real(xlib_handle, "XCreateWindow");
        return XCreateWindow_real(display, parent, x, y, width, height, border_width, depth, class, visual, valuemask, attributes);
    }
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
    IS_FN_CALL_FROM_OSHIM_MAGIC(display)
    {
        FakeDisplay* f = (FakeDisplay*)display;

        xcb_screen_iterator_t i = xcb_setup_roots_iterator(xcb_get_setup(f->xcbconn));
        for(int s = 0; s < screen_number; s++)
            xcb_screen_next(&i);

        return i.data->root;
    }
    else
    {
        int (*XRootWindow_real)(Display* d, int screen_number) = dlsym_real(xlib_handle, "XRootWindow");
        return XRootWindow_real(display, screen_number);
    }
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
    IS_FN_CALL_FROM_OSHIM_MAGIC(display)
    {
        xcb_generic_event_t* (*xcb_wait_for_event_real)(xcb_connection_t* c) = dlsym_real(xcb_handle, "xcb_wait_for_event");
        xcb_generic_event_t* evt = xcb_wait_for_event_real(((FakeDisplay*)display)->xcbconn);
        ((CustomXEvent*)event_return)->xcbevt = evt;
        if(!evt)
            return 1;

        // Event type ids should be the same between Xlib and XCB
        // It is done like this because it's very easy to make the mistake of casting the wrong event and then wonder why the results are wrong
        // (It happened to me.)
        const uint8_t type = ((XAnyEvent*)event_return)->type = evt->response_type & ~0x80;
        ((XAnyEvent*)event_return)->serial = evt->sequence;
        ((XAnyEvent*)event_return)->send_event = !!(evt->response_type & 0x80);
        ((XAnyEvent*)event_return)->display = display;

        if(type == XCB_KEY_RELEASE || type == XCB_KEY_PRESS)
        {
            // Both event structs are the same in xcb as well
            xcb_key_press_event_t* xcbe = (xcb_key_press_event_t*)evt;
            XKeyEvent* ke = (XKeyEvent*)event_return;
            ke->time = xcbe->time;
            ke->root = xcbe->root;
            ke->window = xcbe->event;
            ke->subwindow = xcbe->child;
            ke->x = xcbe->event_x;
            ke->y = xcbe->event_y;
            ke->x_root = xcbe->root_x;
            ke->y_root = xcbe->root_y;
            ke->keycode = xcbe->detail;
            ke->state = xcbe->state;
            ke->same_screen = xcbe->same_screen;
        }
        else if(type == XCB_BUTTON_RELEASE || type == XCB_BUTTON_PRESS)
        {
            xcb_button_press_event_t* xcbe = (xcb_button_press_event_t*)evt;
            XButtonEvent* ke = (XButtonEvent*)event_return;
            ke->time = xcbe->time;
            ke->root = xcbe->root;
            ke->window = xcbe->event;
            ke->subwindow = xcbe->child;
            ke->x = xcbe->event_x;
            ke->y = xcbe->event_y;
            ke->x_root = xcbe->root_x;
            ke->y_root = xcbe->root_y;
            ke->state = xcbe->state;
            ke->button = xcbe->detail;
            ke->same_screen = xcbe->same_screen;
        }
        else if(type == XCB_GE_GENERIC)
        {
            xcb_ge_generic_event_t* ge = (xcb_ge_generic_event_t*)evt;
            if(xinput_opcode && ge->extension == xinput_opcode) // FIXME doesn't work
            {
                // They all use the same struct
                // Key and button are different struct definitions but end up being the same
                if(ge->event_type == XCB_INPUT_KEY_PRESS || ge->event_type == XCB_INPUT_KEY_RELEASE || ge->event_type == XCB_INPUT_BUTTON_PRESS || ge->event_type == XCB_INPUT_BUTTON_RELEASE || ge->event_type == XCB_INPUT_MOTION)
                {
                    xcb_input_button_press_event_t* xievt = (xcb_input_button_press_event_t*)ge;
                    // FIXME: verify we're not overwriting anything
                    XIDeviceEvent* retevt = (XIDeviceEvent*)event_return;

                    retevt->extension = xievt->extension;
                    retevt->evtype = xievt->event_type;
                    retevt->deviceid = xievt->deviceid;
                    retevt->sourceid = xievt->sourceid;
                    retevt->detail = xievt->detail;
                    retevt->root = xievt->root;
                    retevt->event = xievt->event;
                    retevt->child = xievt->child;
                    retevt->root_x = fp1616_to_double(xievt->root_x);
                    retevt->root_y = fp1616_to_double(xievt->root_y);
                    retevt->event_x = fp1616_to_double(xievt->event_x);
                    retevt->event_y = fp1616_to_double(xievt->event_y);
                    retevt->flags = xievt->flags;
                    retevt->time = xievt->time;

                    // mods
                    retevt->mods.base = xievt->mods.base;
                    retevt->mods.latched = xievt->mods.latched;
                    retevt->mods.locked = xievt->mods.locked;
                    retevt->mods.effective = xievt->mods.effective;
                    // group
                    retevt->group.base = xievt->group.base;
                    retevt->group.latched = xievt->group.latched;
                    retevt->group.locked = xievt->group.locked;
                    retevt->group.effective = xievt->group.effective;

                    // buttons, valuators
#warning "These leak memory. Maybe we need a static buffer."
                    retevt->buttons.mask_len = xievt->buttons_len;
                    retevt->buttons.mask = malloc(xievt->buttons_len * sizeof(unsigned char));
                    const uint32_t* bmask = xcb_input_button_press_button_mask(xievt);
                    for(int i = 0; i < retevt->buttons.mask_len; i++)
                        retevt->buttons.mask[i] = bmask[i];

                    retevt->valuators.mask_len = xievt->valuators_len;
                    retevt->valuators.mask = malloc(xievt->valuators_len * sizeof(unsigned char));
                    const uint32_t* vmask = xcb_input_button_press_valuator_mask(xievt);
                    for(int i = 0; i < retevt->valuators.mask_len; i++)
                        retevt->valuators.mask[i] = vmask[i];

                    const int vlen = xcb_input_button_press_axisvalues_length(xievt);
                    const xcb_input_fp3232_t* avals = xcb_input_button_press_axisvalues(xievt);
                    retevt->valuators.values = malloc(vlen * sizeof(double));
                    for(int i = 0; i < vlen; i++)
                        retevt->valuators.values[i] = fp3232_to_double(avals[i]);
                }
                else
                    printf("UNHANDLED XINPUT EVENT %hhu\n", ge->event_type); // FIXME
            }
            else
            {
                if(xinput_opcode)
                    printf("UNHANDLED GENERIC EVENT %hhu ext %d op %hhu\n", type, ge->extension, xinput_opcode);
                else
                    printf("XI2 NOT INITIALISED %hhu ext %d\n", type, ge->extension);
            }
        }
        else if(type == XCB_PROPERTY_NOTIFY)
        {
            xcb_property_notify_event_t* n = (xcb_property_notify_event_t*)evt;
            XPropertyEvent* pe = (XPropertyEvent*)event_return;
            pe->window = n->window;
            pe->atom = n->atom;
            pe->time = n->time;
            pe->state = n->state;
        }
        else if(type == XKB_BASE_EVENT)
        {
            // This isn't the correct type, but there doesn't seem to be an equivalent of XkbAnyEvent in xcb.
            xcb_xkb_new_keyboard_notify_event_t* xcbxkb = (xcb_xkb_new_keyboard_notify_event_t*)evt;
            XkbAnyEvent* xkbev = (XkbAnyEvent*)event_return;
            printf("XKBTYPE IS %d\n", xcbxkb->xkbType);
        }
        else
            printf("UNHANDLED EVENT %d\n", type);
        return 0;
    }
    else
    {
        int (*XNextEvent_real)(Display* display, XEvent* event_return) = dlsym_real(xlib_handle, "XNextEvent");
        return XNextEvent_real(display, event_return);
    }
}

KeySym XLookupKeysym_custom(XKeyEvent* key_event, int col)
{
    PM();
    CustomXEvent* e = (CustomXEvent*)key_event;
    xcb_keysym_t sym = xcb_key_press_lookup_keysym(((FakeDisplay*)key_event->display)->xcbsyms, (xcb_key_press_event_t*)e->xcbevt, col);
    printf("SYM %d\n", sym);
    return sym;
}

Pixmap XCreatePixmap_custom(Display* display, Drawable d, unsigned int width, unsigned int height, unsigned int depth)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;
    xcb_pixmap_t pixmap = xcb_generate_id(f->xcbconn);
    xcb_create_pixmap(f->xcbconn, depth, pixmap, d, width, height);
    //xcb_flush(f->xcbconn);
    return pixmap;
}

int XFreePixmap_custom(Display* display, Pixmap pixmap)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;
    xcb_free_pixmap(f->xcbconn, pixmap);
    return 1;
}

int XFreeGC_custom(Display* display, GC gc)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;
    xcb_free_gc(f->xcbconn, *((xcb_gcontext_t*)gc));
    free(gc);
    return 1;
}

GC XCreateGC_custom(Display* display, Drawable d, unsigned long valuemask, XGCValues* values)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;
    xcb_gcontext_t gc = xcb_generate_id(f->xcbconn);
    xcb_create_gc(f->xcbconn, gc, d, valuemask, values);
    // malloc it because the compiler won't stop with the warnings if we try to cast xcb_gcontext_t to GC (a pointer)
    xcb_gcontext_t* ptr = malloc(sizeof(xcb_gcontext_t));
    *ptr = gc;
    // Make sure xcb_gcontext_t fits in whatever GC is
    //static_assert(sizeof(GC) >= sizeof(xcb_gcontext_t));
    return (GC)ptr;
}

int XFillRectangle_custom(Display* display, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;
    xcb_rectangle_t rect = {x, y, width, height};
    xcb_poly_rectangle(f->xcbconn, d, *((xcb_gcontext_t*)gc), 1, &rect);

    xcb_flush(f->xcbconn);
    return 1;
}

// Untested
Bool XQueryExtension_custom(Display* display, _Xconst char* name, int* major_opcode_return, int* first_event_return, int* first_error_return)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;

    xcb_query_extension_reply_t* reply = xcb_query_extension_reply(f->xcbconn, xcb_query_extension(f->xcbconn, strlen(name), name), NULL);
    if(!reply)
        return 0;

    uint8_t present = reply->present;
    *major_opcode_return = reply->major_opcode;
    *first_event_return = reply->first_event;
    *first_error_return = reply->first_error;

    // Cache the xinput opcode for further use
    if(!xinput_opcode && name && present && !strcmp(name, "XInputExtension"))
        xinput_opcode = reply->major_opcode;

    printf("REQUESTED OPCODE FOR %s, was %d\n", name, reply->major_opcode);
    free(reply);

    return present;
}

int XSync_custom(Display* display, Bool discard)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;
    // Equivalent of xcb_aux_sync();
    free(xcb_get_input_focus_reply(f->xcbconn, xcb_get_input_focus(f->xcbconn), NULL));
    // I think this is the correct order...
    if(discard)
    {
        // Since we are stubbing xcb_poll_for_queued_event, get the handle to the real one first and call that
        xcb_generic_event_t* (*xcb_poll_for_queued_event_real)(xcb_connection_t* conn) = dlsym_real(xcb_handle, "xcb_poll_for_queued_event");
        // Then drain the queue
        xcb_generic_event_t* e;
        while((e = xcb_poll_for_queued_event_real(f->xcbconn)))
            free(e);
    }
    return 1;
}

int XWarpPointer_custom(Display* display, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width, unsigned int src_height, int dest_x, int dest_y)
{
    PM();
    FakeDisplay* f = (FakeDisplay*)display;

    xcb_warp_pointer(f->xcbconn, src_w, dest_w, src_x, src_y, src_width, src_height, dest_x, dest_y);
    // FIXME: flush?
    return 1;
}

Bool XGetEventData_custom(Display* display, XGenericEventCookie* cookie)
{
    PM();
    cookie->data = calloc(1, 1234); // FIXME
    return False;
}

void XFreeEventData_custom(Display* display, XGenericEventCookie* cookie)
{
    PM();
    free(cookie->data);
}

int XEventsQueued_custom(Display* display, int mode)
{
    PM();
    puts("STUB");
    return 0;
}
