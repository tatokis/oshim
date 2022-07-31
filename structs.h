#include <X11/Xlib.h>
#include <X11/Xlibint.h> // Dangerous
// FIXME add proper includes for xcb
#include <xcb/xcb_keysyms.h>

#define FAKE_DISPLAY_STR_LEN 256

// fake Display struct that is used for both compatibility with Xlib's Display and also carrying extra data
typedef struct {
    // Would it be viable to use a union or would it break things
    // We use _XDisplay because we can't Display is opaque
    char origdisplay[sizeof(struct _XDisplay)];
    xcb_connection_t* xcbconn;
    xcb_window_t extrawid;
    xcb_key_symbols_t* xcbsyms;
} FakeDisplay;

typedef struct {
    char origxevent[sizeof(XEvent)];
    xcb_generic_event_t* xcbevt;
} CustomXEvent;
