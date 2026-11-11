/* Utility X11: elenca le finestre visibili con titolo.
 * Usata dagli smoke test E2E. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

static int titled_windows = 0;

static char *get_net_wm_name(Display *dpy, Window win, Atom utf8)
{
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char *prop = NULL;
    char *name = NULL;

    if (XGetWindowProperty(dpy, win, XInternAtom(dpy, "_NET_WM_NAME", False),
                           0, 1024, False, utf8,
                           &actual_type, &actual_format, &nitems,
                           &bytes_after, &prop) == Success &&
        prop != NULL) {
        if (actual_format == 8 && nitems > 0) {
            name = malloc(nitems + 1);
            if (name != NULL) {
                memcpy(name, prop, nitems);
                name[nitems] = '\0';
            }
        }
        XFree(prop);
    }

    return name;
}

/* Stampa il titolo se la finestra è visibile. */
static void probe_window(Display *dpy, Window win, Atom utf8)
{
    XWindowAttributes attrs;
    char *name;

    if (!XGetWindowAttributes(dpy, win, &attrs))
        return;

    if (attrs.map_state != IsViewable)
        return;

    name = get_net_wm_name(dpy, win, utf8);
    if (name != NULL && name[0] != '\0') {
        printf("%s\n", name);
        titled_windows++;
        free(name);
        return;
    }
    free(name);

    name = NULL;
    if (XFetchName(dpy, win, &name) != 0 && name != NULL) {
        if (name[0] != '\0') {
            printf("%s\n", name);
            titled_windows++;
        }
        XFree(name);
    }
}

static void walk_tree(Display *dpy, Window win, Atom utf8)
{
    Window root = None;
    Window parent = None;
    Window *children = NULL;
    unsigned int nchildren = 0;

    probe_window(dpy, win, utf8);

    if (!XQueryTree(dpy, win, &root, &parent, &children, &nchildren))
        return;

    for (unsigned int i = 0; i < nchildren; ++i)
        walk_tree(dpy, children[i], utf8);

    if (children != NULL)
        XFree(children);
}

int main(void)
{
    Display *dpy;
    Atom utf8;

    dpy = XOpenDisplay(NULL);
    if (dpy == NULL) {
        fprintf(stderr, "window_probe: cannot open display %s\n",
                getenv("DISPLAY") != NULL ? getenv("DISPLAY") : "(unset)");
        return 1;
    }

    utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    walk_tree(dpy, DefaultRootWindow(dpy), utf8);
    XCloseDisplay(dpy);

    return titled_windows > 0 ? 0 : 1;
}
