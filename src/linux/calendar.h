/* Interfaccia del selettore calendario per Jotilla (dialog GTK). */
#ifndef JOTILLA_CALENDAR_H
#define JOTILLA_CALENDAR_H
#include <gtk/gtk.h>

/* Crea un dialog modale di scelta data con callback on_pick. */
GtkWidget *calendar_picker_new(GtkWindow *parent, guint32 current,
                               void (*on_pick)(guint32 date, gpointer data),
                               gpointer data);
#endif
