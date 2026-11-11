#include "palette_picker.h"
#include "palette.h"

#include <string.h>

typedef struct {
    GtkWidget *popover;
    GtkWidget *grid;
    GtkWidget *name_label;
    GtkWidget *favorite_button;
    int palette;
    int color;
    gboolean text_mode;
    PalettePickedFunc picked;
    PaletteFavoriteFunc favorite;
    gpointer data;
} Picker;

static void on_color_clicked(GtkButton *button, gpointer data);

static int wrap_palette(int value)
{
    if (value < 0)
        return PALETTE_COUNT - 1;
    if (value >= PALETTE_COUNT)
        return 0;
    return value;
}


static void rebuild_grid(Picker *picker)
{
    GList *children, *iter;
    int i;
    GtkWidget *old;

    children = gtk_container_get_children(GTK_CONTAINER(picker->grid));
    for (iter = children; iter != NULL; iter = iter->next) {
        old = GTK_WIDGET(iter->data);
        gtk_widget_destroy(old);
    }
    g_list_free(children);

    gtk_label_set_text(GTK_LABEL(picker->name_label), palette_name(picker->palette));

    for (i = 0; i < COLOR_COUNT; ++i) {
        GtkWidget *button;
        GtkCssProvider *provider;
        char *css;
        char name[32];

        if (i >= PALETTE_SIZES[picker->palette])
            break;

        g_snprintf(name, sizeof(name), "pick-%d-%d", picker->palette, i);
        button = gtk_button_new();
        gtk_widget_set_name(button, name);
        gtk_widget_set_size_request(button, 30, 30);
        gtk_widget_set_tooltip_text(button,
            palette_color_hex(picker->palette, i));
        g_object_set_data(G_OBJECT(button), "picker", picker);
        g_object_set_data(G_OBJECT(button), "color", GINT_TO_POINTER(i));
        g_signal_connect(button, "clicked", G_CALLBACK(on_color_clicked), picker);

        css = g_strdup_printf(
            "#%s { background-color: %s; border: 1px solid rgba(0,0,0,0.35); "
            "border-radius: 4px; min-width: 30px; min-height: 30px; }",
            name, palette_color_hex(picker->palette, i));
        provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider, css, -1, NULL);
        gtk_style_context_add_provider(gtk_widget_get_style_context(button),
                                       GTK_STYLE_PROVIDER(provider),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        g_free(css);

        gtk_grid_attach(GTK_GRID(picker->grid), button, i % 3, i / 3, 1, 1);
    }
    gtk_widget_show_all(picker->grid);
}

static void on_color_clicked(GtkButton *button, gpointer data)
{
    Picker *picker = data;
    int color = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "color"));

    picker->color = color;
    if (picker->picked != NULL)
        picker->picked(picker->palette, color, picker->data);
    gtk_popover_popdown(GTK_POPOVER(picker->popover));
}

static void on_prev(GtkButton *button, gpointer data)
{
    Picker *picker = data;
    (void)button;
    picker->palette = wrap_palette(picker->palette - 1);
    rebuild_grid(picker);
}

static void on_next(GtkButton *button, gpointer data)
{
    Picker *picker = data;
    (void)button;
    picker->palette = wrap_palette(picker->palette + 1);
    rebuild_grid(picker);
}

static void on_favorite(GtkButton *button, gpointer data)
{
    Picker *picker = data;
    (void)button;
    if (picker->favorite != NULL)
        picker->favorite(picker->palette, picker->data);
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    Picker *picker = data;
    int count = PALETTE_SIZES[picker->palette];
    (void)widget;

    if (count < 1)
        count = 1;
    switch (event->keyval) {
    case GDK_KEY_Left:
    case GDK_KEY_Right:
        picker->palette = wrap_palette(picker->palette +
                                       (event->keyval == GDK_KEY_Left ? -1 : 1));
        if (picker->color >= PALETTE_SIZES[picker->palette])
            picker->color = 0;
        rebuild_grid(picker);
        return TRUE;
    case GDK_KEY_Up:
    case GDK_KEY_Down:
        picker->color = (picker->color +
                         (event->keyval == GDK_KEY_Up ? count - 1 : 1)) % count;
        return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        if (picker->picked != NULL)
            picker->picked(picker->palette, picker->color, picker->data);
        gtk_popover_popdown(GTK_POPOVER(picker->popover));
        return TRUE;
    case GDK_KEY_Escape:
        gtk_popover_popdown(GTK_POPOVER(picker->popover));
        return TRUE;
    case GDK_KEY_f:
    case GDK_KEY_F:
        if (picker->favorite != NULL)
            picker->favorite(picker->palette, picker->data);
        return TRUE;
    default:
        return FALSE;
    }
}

static gboolean on_show(GtkWidget *widget, gpointer data)
{
    (void)data;
    gtk_widget_grab_focus(widget);
    return FALSE;
}

GtkWidget *palette_picker_new(GtkWindow *parent, int palette, int color,
                              gboolean text_mode, PalettePickedFunc picked,
                              PaletteFavoriteFunc favorite, gpointer data)
{
    Picker *picker = g_new0(Picker, 1);
    GtkWidget *header;
    GtkWidget *prev;
    GtkWidget *next;
    GtkWidget *outer;
    GtkWidget *label;

    if (palette < 0 || palette >= PALETTE_COUNT)
        palette = 0;
    if (color < 0 || color >= COLOR_COUNT)
        color = 0;

    picker->palette = palette;
    picker->color = color;
    picker->text_mode = text_mode;
    picker->picked = picked;
    picker->favorite = favorite;
    picker->data = data;

    picker->popover = gtk_popover_new(GTK_WIDGET(parent));
    gtk_popover_set_position(GTK_POPOVER(picker->popover), GTK_POS_BOTTOM);

    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 8);
    gtk_container_add(GTK_CONTAINER(picker->popover), outer);

    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);

    prev = gtk_button_new_with_label("\342\200\271");
    gtk_button_set_relief(GTK_BUTTON(prev), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(prev, "Previous palette");
    g_signal_connect(prev, "clicked", G_CALLBACK(on_prev), picker);
    gtk_box_pack_start(GTK_BOX(header), prev, FALSE, FALSE, 0);

    picker->name_label = gtk_label_new(palette_name(palette));
    gtk_label_set_ellipsize(GTK_LABEL(picker->name_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(picker->name_label, TRUE);
    gtk_box_pack_start(GTK_BOX(header), picker->name_label, TRUE, TRUE, 0);

    next = gtk_button_new_with_label("\342\200\272");
    gtk_button_set_relief(GTK_BUTTON(next), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(next, "Next palette");
    g_signal_connect(next, "clicked", G_CALLBACK(on_next), picker);
    gtk_box_pack_start(GTK_BOX(header), next, FALSE, FALSE, 0);

    picker->favorite_button = gtk_button_new_with_label("\342\230\205");
    gtk_button_set_relief(GTK_BUTTON(picker->favorite_button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(picker->favorite_button, "Favorite palette");
    g_signal_connect(picker->favorite_button, "clicked",
                     G_CALLBACK(on_favorite), picker);
    gtk_box_pack_start(GTK_BOX(header), picker->favorite_button, FALSE, FALSE, 0);

    picker->grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(picker->grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(picker->grid), 6);
    gtk_box_pack_start(GTK_BOX(outer), picker->grid, FALSE, FALSE, 0);

    label = gtk_label_new(text_mode ? "Text colour" : "Note colour");
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "dim-label");
    gtk_box_pack_start(GTK_BOX(outer), label, FALSE, FALSE, 0);

    rebuild_grid(picker);
    gtk_widget_set_can_focus(picker->popover, TRUE);
    g_signal_connect(picker->popover, "key-press-event",
                     G_CALLBACK(on_key_press), picker);
    g_signal_connect(picker->popover, "show", G_CALLBACK(on_show), picker);

    g_object_set_data_full(G_OBJECT(picker->popover), "picker", picker, g_free);
    gtk_widget_show_all(outer);
    return picker->popover;
}
