/* Interfaccia pubblica della finestra nota. */
/* Dichiarazioni delle funzioni esportate. */
#ifndef JOTILLA_NOTE_WINDOW_H
#define JOTILLA_NOTE_WINDOW_H

#include <gtk/gtk.h>

#include "jotilla.h"

typedef void (*NoteChangedFunc)(Note *note, gpointer user_data);
typedef void (*NoteFocusFunc)(Note *note, gpointer user_data);

/* Crea e inizializza una nuova finestra nota. */
GtkWidget *note_window_new(Store *store, Note *note);
void note_window_expand(GtkWidget *window);
/* Ricarica stile, contenuto e stato della finestra. */
void note_window_refresh(GtkWidget *window);
/* Imposta il callback di modifica nota. */
void note_window_set_changed_callback(GtkWidget *window, NoteChangedFunc cb, gpointer data);
void note_window_set_focus_callback(GtkWidget *window, NoteFocusFunc cb, gpointer data);
/* Restituisce la nota della finestra. */
Note *note_window_get_note(GtkWidget *window);
void note_window_save(GtkWidget *window);
gboolean note_window_flush(GtkWidget *window);
/* Imposta il callback di nuova nota. */
void note_window_set_new_callback(GtkWidget *window, void (*cb)(gpointer), gpointer data);
/* Imposta il callback di archivio. */
void note_window_set_archive_callback(GtkWidget *window, void (*cb)(gpointer), gpointer data);

#endif
