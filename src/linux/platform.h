/* Interfaccia di astrazione piattaforma per Jotilla. */
#ifndef JOTILLA_PLATFORM_H
#define JOTILLA_PLATFORM_H
#include <gtk/gtk.h>

/* Nome del backend display corrente. */
const char *platform_name(void);
/* Imposta always-on-top (avviso su Wayland). */
void platform_set_keep_above(GtkWidget *window, gboolean above);
/* Imposta finestra appiccicosa (solo X11). */
void platform_set_sticky(GtkWidget *window, gboolean sticky);
/* True se il display e' Wayland. */
gboolean platform_is_wayland(void);

/* Callback invocata quando viene premuto l'hotkey globale. */
typedef void (*PlatformHotkeyFunc)(gpointer data);

/* Installa Ctrl+Alt++ come hotkey globale (solo X11; avvisa su Wayland). */
gboolean platform_hotkey_install(PlatformHotkeyFunc cb, gpointer data);
/* Rimuove l'hotkey globale installato. */
void platform_hotkey_uninstall(void);

/* True se esiste la voce autostart dell'utente. */
gboolean platform_autostart_enabled(void);
/* Crea o rimuove la voce autostart (Exec = /proc/self/exe). */
gboolean platform_autostart_set(gboolean enabled);
#endif
