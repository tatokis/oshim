#include <X11/Xlib.h>
#include <X11/Xlibint.h> // Dangerous
// FIXME add proper includes for xcb
#include <xcb/xcb_keysyms.h>
#include <stdint.h>
#include <assert.h>

#define FAKE_DISPLAY_STR_LEN 256

#define MAGIC1 0x1A313F584E37D4BC
#define MAGIC2 0xCDEFABC654865BAD

#define SIZEOF_XLIB_DISPLAY (sizeof(struct _XDisplay))
// fake Display struct that is used for both compatibility with Xlib's Display and also carrying extra data
typedef struct {
    // We use _XDisplay because we can't use Display since it is opaque
    union {
        char origdisplay[SIZEOF_XLIB_DISPLAY];
        struct {
            uint64_t magic1;
            uint64_t magic2;
        };
        static_assert(SIZEOF_XLIB_DISPLAY >= 8 * 2, "_XDisplay is not large enough"); // Probably unnecessary, but won't harm
        static_assert(sizeof(void*) >= 8, "This has not been tested under 32 bit _XDisplay structs. It might overwrite something.");
    };
    xcb_connection_t* xcbconn;
    xcb_window_t extrawid;
    xcb_key_symbols_t* xcbsyms;
} FakeDisplay;

typedef struct {
    char origxevent[sizeof(XEvent)];
    xcb_generic_event_t* xcbevt;
} CustomXEvent;
