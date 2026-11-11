/* Utility X11 per i test E2E: trova finestre e simula input.
 * Fornisce find, click, type, key e ctrlkey. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>

static Display *display;

static int ignore_x_error(Display *dpy, XErrorEvent *event)
{
    (void)dpy;
    (void)event;
    return 0;
}

static char *window_title(Display *dpy, Window win)
{
    Atom net_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char *prop = NULL;
    char *title = NULL;

    if (XGetWindowProperty(dpy, win, net_name, 0, 4096, False, utf8,
                           &actual_type, &actual_format, &nitems,
                           &bytes_after, &prop) == Success &&
        prop != NULL) {
        if (actual_format == 8 && nitems > 0) {
            title = malloc(nitems + 1);
            if (title != NULL) {
                memcpy(title, prop, nitems);
                title[nitems] = '\0';
            }
        }
        XFree(prop);
    }

    if (title != NULL && title[0] != '\0')
        return title;
    free(title);

    title = NULL;
    if (XFetchName(dpy, win, &title) != 0 && title != NULL) {
        char *copy = strdup(title);
        XFree(title);
        return copy;
    }

    return NULL;
}

static void sanitize(char *text)
{
    for (; *text != '\0'; ++text) {
        if (*text == '\n' || *text == '\r' || *text == '\t')
            *text = ' ';
    }
}

static int matched;

static void walk_tree(Display *dpy, Window win, Window root, const char *needle)
{
    XWindowAttributes attrs;
    Window child = None;
    int abs_x = 0;
    int abs_y = 0;

    if (!XGetWindowAttributes(dpy, win, &attrs))
        return;

    if (attrs.map_state == IsViewable) {
        char *title = window_title(dpy, win);
        if (title != NULL && title[0] != '\0' &&
            strstr(title, needle) != NULL) {
            if (!XTranslateCoordinates(dpy, win, root, 0, 0,
                                       &abs_x, &abs_y, &child)) {
                abs_x = attrs.x;
                abs_y = attrs.y;
            }
            sanitize(title);
            printf("WIN %lu %d %d %d %d %s\n",
                   (unsigned long)win, abs_x, abs_y,
                   attrs.width, attrs.height, title);
            matched = 1;
        }
        free(title);
    }

    {
        Window parent = None;
        Window *children = NULL;
        unsigned int nchildren = 0;

        if (XQueryTree(dpy, win, &root, &parent, &children, &nchildren)) {
            for (unsigned int i = 0; i < nchildren; ++i)
                walk_tree(dpy, children[i], root, needle);
        }
        if (children != NULL)
            XFree(children);
    }
}

static int parse_window(const char *text, Window *win)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL)
        return -1;
    value = strtoul(text, &end, 0);
    if (end == text || value == 0)
        return -1;
    *win = (Window)value;
    return 0;
}

static int cmd_find(int argc, char **argv)
{
    Window root;

    if (argc != 3) {
        fprintf(stderr, "usage: xui find <substring>\n");
        return 2;
    }

    root = DefaultRootWindow(display);
    walk_tree(display, root, root, argv[2]);
    return matched ? 0 : 1;
}

/* Simula un click del mouse con XTest. */
static int click_button(Window win, int x, int y, unsigned int button)
{
    XRaiseWindow(display, win);
    XSetInputFocus(display, win, RevertToParent, CurrentTime);
    XSync(display, False);
    usleep(20000);

    XTestFakeMotionEvent(display, DefaultScreen(display), x, y, 0);
    XSync(display, False);
    usleep(20000);

    XTestFakeButtonEvent(display, button, True, 0);
    XSync(display, False);
    usleep(30000);
    XTestFakeButtonEvent(display, button, False, 0);
    XSync(display, False);
    usleep(30000);

    return 0;
}

static int cmd_drag(int argc, char **argv)
{
    Window win;
    int x1, y1, x2, y2;

    if (argc != 7) {
        fprintf(stderr, "usage: xui drag <winid> <x1> <y1> <x2> <y2>\n");
        return 2;
    }
    if (parse_window(argv[2], &win) != 0) {
        fprintf(stderr, "invalid window id: %s\n", argv[2]);
        return 2;
    }
    x1 = atoi(argv[3]);
    y1 = atoi(argv[4]);
    x2 = atoi(argv[5]);
    y2 = atoi(argv[6]);
    XRaiseWindow(display, win);
    XSetInputFocus(display, win, RevertToParent, CurrentTime);
    XSync(display, False);
    usleep(30000);
    XTestFakeMotionEvent(display, DefaultScreen(display), x1, y1, 0);
    XTestFakeButtonEvent(display, 1, True, 0);
    XSync(display, False);
    usleep(30000);
    XTestFakeMotionEvent(display, DefaultScreen(display), (x1 + x2) / 2,
                         (y1 + y2) / 2, 0);
    XSync(display, False);
    usleep(30000);
    XTestFakeMotionEvent(display, DefaultScreen(display), x2, y2, 0);
    XSync(display, False);
    usleep(30000);
    XTestFakeButtonEvent(display, 1, False, 0);
    XSync(display, False);
    usleep(30000);
    return 0;
}

static int cmd_dclick(int argc, char **argv)
{
    Window win;
    int x;
    int y;

    if (argc != 5) {
        fprintf(stderr, "usage: xui dclick <winid> <x> <y>\n");
        return 2;
    }
    if (parse_window(argv[2], &win) != 0) {
        fprintf(stderr, "invalid window id: %s\n", argv[2]);
        return 2;
    }
    x = atoi(argv[3]);
    y = atoi(argv[4]);
    (void)click_button(win, x, y, 1);
    usleep(40000);
    return click_button(win, x, y, 1);
}

static int cmd_click(int argc, char **argv, unsigned int button)
{
    Window win;
    int x;
    int y;

    if (argc != 5) {
        fprintf(stderr, "usage: xui %s <winid> <x> <y>\n",
                button == 3 ? "rclick" : "click");
        return 2;
    }
    if (parse_window(argv[2], &win) != 0) {
        fprintf(stderr, "invalid window id: %s\n", argv[2]);
        return 2;
    }
    x = atoi(argv[3]);
    y = atoi(argv[4]);
    return click_button(win, x, y, button);
}

static int send_keycode(KeyCode keycode, int shift, int control)
{
    KeyCode shift_code = XKeysymToKeycode(display, XK_Shift_L);
    KeyCode control_code = XKeysymToKeycode(display, XK_Control_L);

    if (control && control_code != 0)
        XTestFakeKeyEvent(display, control_code, True, 0);
    if (shift && shift_code != 0)
        XTestFakeKeyEvent(display, shift_code, True, 0);

    XTestFakeKeyEvent(display, keycode, True, 0);
    XTestFakeKeyEvent(display, keycode, False, 0);

    if (shift && shift_code != 0)
        XTestFakeKeyEvent(display, shift_code, False, 0);
    if (control && control_code != 0)
        XTestFakeKeyEvent(display, control_code, False, 0);

    XSync(display, False);
    usleep(12000);
    return 0;
}

static int keysym_keycode(KeySym symbol, KeyCode *keycode, int *shift)
{
    KeySym base;
    KeySym shifted;

    *keycode = XKeysymToKeycode(display, symbol);
    if (*keycode == 0)
        return -1;

    base = XkbKeycodeToKeysym(display, *keycode, 0, 0);
    shifted = XkbKeycodeToKeysym(display, *keycode, 0, 1);

    if (base == symbol)
        *shift = 0;
    else if (shifted == symbol)
        *shift = 1;
    else
        *shift = 0;

    return 0;
}

static int send_symbol(KeySym symbol, int shift, int control)
{
    KeyCode keycode;
    int needed_shift = 0;

    if (keysym_keycode(symbol, &keycode, &needed_shift) != 0) {
        fprintf(stderr, "no keycode for keysym 0x%lx\n", (unsigned long)symbol);
        return -1;
    }

    return send_keycode(keycode, shift ? 1 : needed_shift, control);
}

static int char_keysym(unsigned char c, KeySym *symbol)
{
    if (c >= 'a' && c <= 'z') {
        *symbol = XK_a + (c - 'a');
        return 0;
    }
    if (c >= 'A' && c <= 'Z') {
        *symbol = XK_A + (c - 'A');
        return 0;
    }
    if (c >= '0' && c <= '9') {
        *symbol = XK_0 + (c - '0');
        return 0;
    }

    switch (c) {
    case ' ': *symbol = XK_space; return 0;
    case '\n':
    case '\r': *symbol = XK_Return; return 0;
    case '!': *symbol = XK_exclam; return 0;
    case '"': *symbol = XK_quotedbl; return 0;
    case '#': *symbol = XK_numbersign; return 0;
    case '$': *symbol = XK_dollar; return 0;
    case '%': *symbol = XK_percent; return 0;
    case '&': *symbol = XK_ampersand; return 0;
    case '\'': *symbol = XK_apostrophe; return 0;
    case '(': *symbol = XK_parenleft; return 0;
    case ')': *symbol = XK_parenright; return 0;
    case '*': *symbol = XK_asterisk; return 0;
    case '+': *symbol = XK_plus; return 0;
    case ',': *symbol = XK_comma; return 0;
    case '-': *symbol = XK_minus; return 0;
    case '.': *symbol = XK_period; return 0;
    case '/': *symbol = XK_slash; return 0;
    case ':': *symbol = XK_colon; return 0;
    case ';': *symbol = XK_semicolon; return 0;
    case '<': *symbol = XK_less; return 0;
    case '=': *symbol = XK_equal; return 0;
    case '>': *symbol = XK_greater; return 0;
    case '?': *symbol = XK_question; return 0;
    case '@': *symbol = XK_at; return 0;
    case '[': *symbol = XK_bracketleft; return 0;
    case '\\': *symbol = XK_backslash; return 0;
    case ']': *symbol = XK_bracketright; return 0;
    case '^': *symbol = XK_asciicircum; return 0;
    case '_': *symbol = XK_underscore; return 0;
    case '`': *symbol = XK_grave; return 0;
    case '{': *symbol = XK_braceleft; return 0;
    case '|': *symbol = XK_bar; return 0;
    case '}': *symbol = XK_braceright; return 0;
    case '~': *symbol = XK_asciitilde; return 0;
    default:
        return -1;
    }
}

static int focus_window(Window win)
{
    XRaiseWindow(display, win);
    XSetInputFocus(display, win, RevertToParent, CurrentTime);
    XSync(display, False);
    usleep(40000);
    return 0;
}

static int cmd_type(int argc, char **argv)
{
    Window win;
    const unsigned char *text;

    if (argc != 4) {
        fprintf(stderr, "usage: xui type <winid> <text>\n");
        return 2;
    }
    if (parse_window(argv[2], &win) != 0) {
        fprintf(stderr, "invalid window id: %s\n", argv[2]);
        return 2;
    }

    focus_window(win);
    text = (const unsigned char *)argv[3];
    for (; *text != '\0'; ++text) {
        KeySym symbol;
        if (char_keysym(*text, &symbol) != 0) {
            fprintf(stderr, "unsupported character: 0x%02x\n", *text);
            return 1;
        }
        if (send_symbol(symbol, 0, 0) != 0)
            return 1;
    }
    return 0;
}

static int cmd_key(int argc, char **argv)
{
    Window win;
    KeySym symbol;
    int force_shift = 0;

    if (argc < 4 || argc > 5) {
        fprintf(stderr, "usage: xui key <winid> <keysymname> [shift]\n");
        return 2;
    }
    if (parse_window(argv[2], &win) != 0) {
        fprintf(stderr, "invalid window id: %s\n", argv[2]);
        return 2;
    }
    symbol = XStringToKeysym(argv[3]);
    if (symbol == NoSymbol) {
        fprintf(stderr, "unknown keysym: %s\n", argv[3]);
        return 2;
    }
    if (argc == 5)
        force_shift = atoi(argv[4]) != 0;

    focus_window(win);
    return send_symbol(symbol, force_shift, 0) == 0 ? 0 : 1;
}

static int cmd_wheel(int argc, char **argv)
{
    Window win;
    int x, y, button;

    if (argc != 7) {
        fprintf(stderr, "usage: xui wheel <winid> <x> <y> <up|down> <count>\n");
        return 2;
    }
    if (parse_window(argv[2], &win) != 0) {
        fprintf(stderr, "invalid window id: %s\n", argv[2]);
        return 2;
    }
    x = atoi(argv[3]);
    y = atoi(argv[4]);
    button = strcmp(argv[5], "up") == 0 ? 4 : 5;
    {
        int count = atoi(argv[6]);
        int i;

        XRaiseWindow(display, win);
        XSetInputFocus(display, win, RevertToParent, CurrentTime);
        XSync(display, False);
        XTestFakeMotionEvent(display, DefaultScreen(display), x, y, 0);
        XSync(display, False);
        usleep(30000);
        for (i = 0; i < count; ++i) {
            XTestFakeButtonEvent(display, button, True, 0);
            XTestFakeButtonEvent(display, button, False, 0);
            XSync(display, False);
            usleep(20000);
        }
    }
    return 0;
}

static int cmd_keyglobal(int argc, char **argv)
{
    KeySym symbol;

    if (argc != 3) {
        fprintf(stderr, "usage: xui keyglobal <keysymname>\n");
        return 2;
    }
    symbol = XStringToKeysym(argv[2]);
    if (symbol == NoSymbol) {
        fprintf(stderr, "unknown keysym: %s\n", argv[2]);
        return 2;
    }
    return send_symbol(symbol, 0, 0) == 0 ? 0 : 1;
}

static int cmd_ctrlkey(int argc, char **argv)
{
    Window win;
    KeySym symbol;

    if (argc != 4) {
        fprintf(stderr, "usage: xui ctrlkey <winid> <keysymname>\n");
        return 2;
    }
    if (parse_window(argv[2], &win) != 0) {
        fprintf(stderr, "invalid window id: %s\n", argv[2]);
        return 2;
    }
    symbol = XStringToKeysym(argv[3]);
    if (symbol == NoSymbol) {
        fprintf(stderr, "unknown keysym: %s\n", argv[3]);
        return 2;
    }

    focus_window(win);
    return send_symbol(symbol, 0, 1) == 0 ? 0 : 1;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: xui find <substring>\n"
            "       xui click <winid> <x> <y>\n"
            "       xui rclick <winid> <x> <y>\n"
            "       xui type <winid> <text>\n"
            "       xui key <winid> <keysymname> [shift]\n"
            "       xui ctrlkey <winid> <keysymname>\n");
}

int main(int argc, char **argv)
{
    int rc;

    if (argc < 2) {
        usage();
        return 2;
    }

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "xui: cannot open display %s\n",
                getenv("DISPLAY") != NULL ? getenv("DISPLAY") : "(unset)");
        return 2;
    }

    XSetErrorHandler(ignore_x_error);

    if (strcmp(argv[1], "find") == 0)
        rc = cmd_find(argc, argv);
    else if (strcmp(argv[1], "click") == 0)
        rc = cmd_click(argc, argv, 1);
    else if (strcmp(argv[1], "rclick") == 0)
        rc = cmd_click(argc, argv, 3);
    else if (strcmp(argv[1], "dclick") == 0)
        rc = cmd_dclick(argc, argv);
    else if (strcmp(argv[1], "drag") == 0)
        rc = cmd_drag(argc, argv);
    else if (strcmp(argv[1], "type") == 0)
        rc = cmd_type(argc, argv);
    else if (strcmp(argv[1], "key") == 0)
        rc = cmd_key(argc, argv);
    else if (strcmp(argv[1], "ctrlkey") == 0)
        rc = cmd_ctrlkey(argc, argv);
    else if (strcmp(argv[1], "keyglobal") == 0)
        rc = cmd_keyglobal(argc, argv);
    else if (strcmp(argv[1], "wheel") == 0)
        rc = cmd_wheel(argc, argv);
    else {
        fprintf(stderr, "xui: unknown command: %s\n", argv[1]);
        usage();
        rc = 2;
    }

    XCloseDisplay(display);
    return rc;
}
