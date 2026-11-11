#ifndef JOTILLA_PALETTE_PICKER_H
#define JOTILLA_PALETTE_PICKER_H

#include <gtk/gtk.h>

typedef void (*PalettePickedFunc)(int palette, int color, gpointer data);
typedef void (*PaletteFavoriteFunc)(int palette, gpointer data);

/* Crea un popover di selezione colore.
   text_mode=TRUE indica che il colore verra' applicato al testo. */
GtkWidget *palette_picker_new(GtkWindow *parent, int palette, int color,
                              gboolean text_mode, PalettePickedFunc picked,
                              PaletteFavoriteFunc favorite, gpointer data);

#endif
