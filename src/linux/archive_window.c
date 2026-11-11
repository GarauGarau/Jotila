/* Finestra archivio GTK per elencare e cercare note. */
/* Gestisce tile, calendario, filtri, albero date/tag e azioni. */
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

#include "archive_window.h"
#include "date.h"
#include "palette.h"
#include "platform.h"
#include "tags.h"

#define ARCHIVE_STATE_KEY "archive-window-state"
#define ARCHIVE_STORE_KEY "archive-store"
#define ARCHIVE_CARD_WIDTH 290
#define ARCHIVE_CARD_HEIGHT 150
#define ARCHIVE_HIGHLIGHT "#e4c46d"
#define ARCHIVE_PAPER "#f4eee2"
#define ARCHIVE_SIDEBAR "#ebe2d1"
#define ARCHIVE_INK "#3a342b"
#define ARCHIVE_SELECTION "#d3c2a2"
#define ARCHIVE_LINE "#b7a98f"
#define CAL_PAD 8
#define CAL_TITLE_H 20
#define CAL_WEEK_H 16
#define CAL_GRID_TOP (CAL_TITLE_H + CAL_WEEK_H + 4)

static const char ARCHIVE_CSS[] =
    ".archive-window, .archive-content { background-color: " ARCHIVE_PAPER
    "; color: " ARCHIVE_INK "; }"
    "headerbar.archive-header, .archive-header { background-image: none;"
    " background-color: " ARCHIVE_PAPER "; color: " ARCHIVE_INK
    "; border: none; box-shadow: none; }"
    ".archive-header .title { font-size: 12px; font-weight: bold; color: "
    ARCHIVE_INK "; }"
    ".archive-header .subtitle { font-size: 10px; color: #6b6153; }"
    ".archive-toolbar { background-color: " ARCHIVE_PAPER "; padding: 8px; }"
    ".archive-search { background-color: #fffdf8; color: " ARCHIVE_INK
    "; border: 1px solid " ARCHIVE_LINE "; border-radius: 2px;"
    " min-height: 28px; font-size: 12px; }"
    ".archive-search:focus { border-color: " ARCHIVE_INK "; }"
    "button.archive-tool-button { background-image: none; background-color: "
    ARCHIVE_SIDEBAR "; color: " ARCHIVE_INK "; border: 1px solid "
    ARCHIVE_LINE "; border-radius: 2px; min-height: 28px; padding: 2px 14px;"
    " font-size: 12px; box-shadow: none; text-shadow: none; }"
    "button.archive-tool-button:hover { background-color: #e3d8c2; }"
    "button.archive-tool-button:checked, button.archive-tool-button:active {"
    " background-color: " ARCHIVE_SELECTION "; color: " ARCHIVE_INK "; }"
    "button.archive-icon-button { background-image: none; background-color: "
    ARCHIVE_SIDEBAR "; color: " ARCHIVE_INK "; border: 1px solid "
    ARCHIVE_LINE "; border-radius: 2px; min-width: 28px; min-height: 28px;"
    " padding: 2px; box-shadow: none; }"
    "button.archive-icon-button:checked { background-color: " ARCHIVE_SELECTION
    "; }"
    ".archive-sidebar, .archive-sidebar-scroll, .archive-sidebar-scroll viewport"
    " { background-color: " ARCHIVE_SIDEBAR "; border: none; }"
    ".archive-tree { background-color: " ARCHIVE_SIDEBAR "; color: " ARCHIVE_INK
    "; font-size: 12px; }"
    ".archive-tree:selected, .archive-tree.view:selected { background-color: "
    ARCHIVE_SELECTION "; color: " ARCHIVE_INK "; }"
    ".archive-scroll, .archive-scroll viewport { background-color: " ARCHIVE_PAPER
    "; border: none; }"
    ".archive-flow, .archive-flow flowboxchild, .archive-flow flowboxchild:selected"
    " { background-color: transparent; padding: 0; margin: 0;"
    " border: none; min-width: 0; min-height: 0; }"
    ".archive-card-title { font-size: 12px; font-weight: bold; }"
    ".archive-card-excerpt { font-size: 11px; }"
    ".archive-card-footer { font-size: 10px; }"
    ".archive-calendar-page, .archive-calendar-page list,"
    " .archive-calendar-page list row { background-color: " ARCHIVE_PAPER
    "; color: " ARCHIVE_INK "; }"
    ".archive-calendar-page list row:selected { background-color: "
    ARCHIVE_SELECTION "; color: " ARCHIVE_INK "; }"
    ".archive-calendar { background-color: " ARCHIVE_PAPER "; color: "
    ARCHIVE_INK "; }"
    ".archive-calendar-label { color: #6b6153; font-size: 11px; }"
    "scrollbar { background-color: " ARCHIVE_SIDEBAR "; border: none; }"
    "scrollbar slider { background-color: " ARCHIVE_LINE
    "; border-radius: 4px; min-width: 8px; min-height: 24px; }";

static const char *MONTH_NAMES[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

enum {
    DATE_COL_LABEL,
    DATE_COL_VALUE,
    DATE_COL_KIND,
    DATE_COL_COUNT
};

enum {
    TAG_COL_LABEL,
    TAG_COL_VALUE,
    TAG_COL_COUNT,
    TAG_COL_N_COLUMNS
};

typedef struct {
    GtkWidget *window;
    Store *store;
    GtkWidget *header;
    GtkWidget *search;
    GtkWidget *tags_button;
    GtkWidget *date_button;
    GtkWidget *sidebar;
    GtkCssProvider *css;
    GtkWidget *stack;
    GtkWidget *flow;
    GtkWidget *flow_scroll;
    GtkWidget *calendar;
    GtkWidget *calendar_button;
    GtkWidget *calendar_year;
    GtkWidget *calendar_prev;
    GtkWidget *calendar_next;
    GtkWidget *calendar_today_button;
    GtkWidget *calendar_list;
    GtkWidget *calendar_label;
    GtkWidget *sidebar_stack;
    GtkWidget *date_tree;
    GtkWidget *tag_tree;
    GtkTreeStore *date_model;
    GtkTreeStore *tag_model;
    ArchiveOpenFunc open_cb;
    gpointer open_data;
    ArchiveChangeFunc change_cb;
    gpointer change_data;
    ArchiveDeleteFunc delete_cb;
    gpointer delete_data;
    Note *first_note;
    gint selection_anchor;
    GPtrArray *visible_notes;
    char *tag_filter;
    guint32 date_filter;
    gint date_kind;
    gboolean sidebar_tags;
    gboolean updating_sidebar;
    gdouble flow_scroll_x;
    gdouble flow_scroll_y;
    guint flow_scroll_restore_id;
    GHashTable *day_counts;
    guint32 cal_year;
    guint32 cal_selected;
    guint32 cal_focus;
    guint32 cal_today;
    guint32 cal_hover;
    gboolean cal_updating;
    guint search_timeout_id;
} ArchiveWindow;

/* Ricarica la lista con filtri e ricerca. */
static void repopulate_list(ArchiveWindow *aw);
/* Aggiorna il calendario e le note del giorno. */
static void update_calendar(ArchiveWindow *aw);
/* Conferma ed elimina una nota dall'archivio. */
static void confirm_delete(ArchiveWindow *aw, Note *note);
/* Elimina le note selezionate nel flow box dopo conferma. */
static void delete_selected_notes(ArchiveWindow *aw);
/* Mostra il menu contestuale di una card. */
static void show_card_menu(ArchiveWindow *aw, Note *note,
                           GdkEventButton *event);
/* Ripristina la posizione di scorrimento del flow box. */
static gboolean restore_flow_scroll(gpointer data);
/* Ricostruisce gli alberi di date e tag. */
static void populate_sidebar(ArchiveWindow *aw);
/* Allinea i toggle Tags/Date allo stato della sidebar. */
static void update_sidebar_buttons(ArchiveWindow *aw);
/* Cambia l'albero mostrato nella sidebar. */
static void on_sidebar_toggled(GtkToggleButton *button, gpointer data);
/* Pulisce ricerca e filtri. */
static void clear_search_text(ArchiveWindow *aw);
/* Aggiorna titolo finestra e header in base a vista e filtri. */
static void update_window_title(ArchiveWindow *aw);

/* Restituisce lo stato della finestra archivio. */
static ArchiveWindow *get_state(GtkWidget *window) {
    return g_object_get_data(G_OBJECT(window), ARCHIVE_STATE_KEY);
}

/* Imposta tooltip e nome accessibile per un controllo. */
static void set_accessible_label(GtkWidget *widget, const char *label) {
    AtkObject *accessible;

    if (!widget || !label) return;
    gtk_widget_set_tooltip_text(widget, label);
    accessible = gtk_widget_get_accessible(widget);
    if (accessible) atk_object_set_name(accessible, label);
}

/* Svuota un list box rimuovendo i figli. */
static void clear_listbox(GtkWidget *list) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(list));
    GList *item;

    for (item = children; item != NULL; item = item->next)
        gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(children);
}

/* Svuota il flow box rimuovendo le card. */
static void clear_flowbox(GtkWidget *flow) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(flow));
    GList *item;

    for (item = children; item != NULL; item = item->next)
        gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(children);
}

/* Estrae il titolo dalla prima riga del testo. */
static char *note_title(const char *text) {
    const char *end;
    char *line;

    if (!text || !*text) return g_strdup("(empty)");
    end = text;
    while (*end && *end != '\n' && *end != '\r') ++end;
    line = g_strndup(text, (gsize)(end - text));
    g_strstrip(line);
    if (!*line) {
        g_free(line);
        return g_strdup("(empty)");
    }
    return line;
}

/* Crea un estratto del testo su una riga. */
static char *note_excerpt(const char *text) {
    char *flat;
    char *cursor;
    glong length;

    if (!text || !*text) return g_strdup("");
    flat = g_strdup(text);
    for (cursor = flat; *cursor; ++cursor) {
        if (*cursor == '\n' || *cursor == '\r' || *cursor == '\t')
            *cursor = ' ';
    }
    g_strstrip(flat);
    length = g_utf8_strlen(flat, -1);
    if (length > 160) {
        char *head = g_utf8_substring(flat, 0, 160);
        char *result = g_strconcat(head, "\342\200\246", NULL);
        g_free(head);
        g_free(flat);
        return result;
    }
    return flat;
}

/* Formatta i tag per il footer della card (vuoto se non ci sono tag). */
static char *note_tag_list(const char *tags) {
    GPtrArray *items = tags_split(tags);
    GString *text;
    guint i;

    if (!items->len) {
        g_ptr_array_free(items, TRUE);
        return g_strdup("");
    }
    text = g_string_new(NULL);
    for (i = 0; i < items->len; ++i) {
        if (i) g_string_append(text, "  ");
        g_string_append(text, (const char *)g_ptr_array_index(items, i));
    }
    g_ptr_array_free(items, TRUE);
    return g_string_free(text, FALSE);
}

/* Titolo della card senza il cancelletto iniziale. */
static char *note_card_title(const Note *note) {
    char *value;
    const char *cursor;

    if (note->has_title && note->title) return g_strdup(note->title);
    value = note_title(note->text);
    cursor = value;
    while (*cursor == '#') ++cursor;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor && cursor != value) {
        char *trimmed = g_strdup(cursor);

        g_free(value);
        return trimmed;
    }
    return value;
}

/* Formatta i tag per la riga del calendario. */
static char *note_tags(const char *tags) {
    GPtrArray *items = tags_split(tags);
    GString *text;
    guint i;

    if (!items->len) {
        g_ptr_array_free(items, TRUE);
        return g_strdup("No tags");
    }
    text = g_string_new("Tags: ");
    for (i = 0; i < items->len; ++i) {
        if (i) g_string_append(text, ", ");
        g_string_append(text, (const char *)g_ptr_array_index(items, i));
    }
    g_ptr_array_free(items, TRUE);
    return g_string_free(text, FALSE);
}

/* Formatta la data della nota. */
static char *note_date(guint32 date) {
    char *formatted;

    if (!date || !date_valid(date)) return g_strdup("No date");
    formatted = date_format(date);
    if (!formatted || !*formatted) {
        g_free(formatted);
        return g_strdup("No date");
    }
    return formatted;
}

/* Evidenzia le occorrenze della query nell'estratto con markup Pango. */
static char *highlight_excerpt(const char *text, const char *query) {
    GString *folded;
    GString *markup;
    GArray *map;
    const char *cursor;
    char *folded_query;
    gsize text_len;
    gsize needle_len;
    gsize last = 0;
    gsize search_from = 0;

    if (!text) return g_strdup("");
    if (!query || !*query) return g_markup_escape_text(text, -1);

    folded_query = g_utf8_casefold(query, -1);
    if (!folded_query || !*folded_query) {
        g_free(folded_query);
        return g_markup_escape_text(text, -1);
    }

    folded = g_string_new(NULL);
    map = g_array_new(FALSE, FALSE, sizeof(gsize));
    cursor = text;
    while (*cursor) {
        const char *next = g_utf8_next_char(cursor);
        gsize original = (gsize)(cursor - text);
        char *piece = g_utf8_casefold(cursor, (gssize)(next - cursor));

        if (piece) {
            gsize bytes = strlen(piece);
            gsize b;
            g_string_append(folded, piece);
            for (b = 0; b < bytes; ++b)
                g_array_append_val(map, original);
            g_free(piece);
        } else {
            const char *byte;
            for (byte = cursor; byte < next; ++byte) {
                g_string_append_c(folded, *byte);
                g_array_append_val(map, original);
            }
        }
        cursor = next;
    }
    text_len = (gsize)strlen(text);
    g_array_append_val(map, text_len);

    needle_len = strlen(folded_query);
    markup = g_string_new(NULL);
    while (needle_len > 0 && search_from + needle_len <= folded->len) {
        char *found = g_strstr_len(folded->str + search_from,
                                   folded->len - search_from, folded_query);
        gsize at;
        gsize match_start;
        gsize match_end;
        char *escaped;

        if (!found) break;
        at = (gsize)(found - folded->str);
        match_start = g_array_index(map, gsize, at);
        match_end = g_array_index(map, gsize, at + needle_len);
        if (match_end <= match_start || match_start < last) {
            search_from = at + needle_len;
            continue;
        }
        escaped = g_markup_escape_text(text + last,
                                       (gssize)(match_start - last));
        g_string_append(markup, escaped);
        g_free(escaped);
        g_string_append(markup, "<span background=\""
                                ARCHIVE_HIGHLIGHT "\">");
        escaped = g_markup_escape_text(text + match_start,
                                       (gssize)(match_end - match_start));
        g_string_append(markup, escaped);
        g_free(escaped);
        g_string_append(markup, "</span>");
        last = match_end;
        search_from = at + needle_len;
    }
    {
        char *tail = g_markup_escape_text(text + last,
                                          (gssize)(text_len - last));
        g_string_append(markup, tail);
        g_free(tail);
    }

    g_array_free(map, TRUE);
    g_string_free(folded, TRUE);
    g_free(folded_query);
    return g_string_free(markup, FALSE);
}

/* Verifica se la nota rientra nel filtro data della sidebar. */
static gboolean note_matches_date(const ArchiveWindow *aw, guint32 date) {
    if (!aw->date_filter || aw->date_kind <= 0) return TRUE;
    switch (aw->date_kind) {
    case 1:
        return date / 10000u == aw->date_filter / 10000u;
    case 2:
        return date / 100u == aw->date_filter / 100u;
    case 3:
        return date == aw->date_filter;
    default:
        return TRUE;
    }
}

/* Evidenzia le card selezionate. */
static void on_flow_selection_changed(GtkFlowBox *box, gpointer data) {
    GList *children;
    GList *item;

    (void)data;
    children = gtk_container_get_children(GTK_CONTAINER(box));
    for (item = children; item != NULL; item = item->next) {
        GtkWidget *child = GTK_WIDGET(item->data);
        GtkWidget *card = g_object_get_data(G_OBJECT(child), "card");

        if (!card) continue;
        if (gtk_flow_box_child_is_selected(GTK_FLOW_BOX_CHILD(child)))
            gtk_widget_set_state_flags(card, GTK_STATE_FLAG_SELECTED, FALSE);
        else
            gtk_widget_unset_state_flags(card, GTK_STATE_FLAG_SELECTED);
    }
    g_list_free(children);
}

/* Gestisce il clic destro sulla lista del calendario. */
static gboolean on_list_button_press(GtkWidget *widget, GdkEventButton *event,
                                     gpointer data) {
    ArchiveWindow *aw = data;
    GtkListBoxRow *row;
    Note *note;

    if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
    row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(widget), (gint)event->y);
    if (!row) return FALSE;
    note = g_object_get_data(G_OBJECT(row), "note");
    confirm_delete(aw, note);
    return TRUE;
}

/* Apre la nota scelta dal menu contestuale. */
static void on_menu_open(GtkMenuItem *item, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(item), "note");

    if (note && aw->open_cb) aw->open_cb(note, aw->open_data);
}

/* Elimina la nota scelta dal menu contestuale. */
static void on_menu_delete(GtkMenuItem *item, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(item), "note");

    confirm_delete(aw, note);
}

/* Rilascia il menu contestuale quando viene chiuso. */
static void on_menu_selection_done(GtkMenuShell *menu, gpointer data) {
    (void)data;
    gtk_widget_destroy(GTK_WIDGET(menu));
    g_object_unref(menu);
}

/* Mostra il menu contestuale per la card indicata. */
static void show_card_menu(ArchiveWindow *aw, Note *note,
                           GdkEventButton *event) {
    GtkWidget *menu;
    GtkWidget *item;

    if (!aw || !note || !event) return;

    menu = gtk_menu_new();
    g_object_ref_sink(menu);
    gtk_menu_attach_to_widget(GTK_MENU(menu), aw->flow, NULL);
    g_signal_connect(menu, "selection-done",
                     G_CALLBACK(on_menu_selection_done), NULL);

    item = gtk_menu_item_new_with_label("Open");
    g_object_set_data(G_OBJECT(item), "note", note);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_open), aw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Delete from archive");
    g_object_set_data(G_OBJECT(item), "note", note);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_delete), aw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
}

/* Gestisce il clic destro sulla singola card. */
static gboolean on_card_button_press(GtkWidget *widget, GdkEventButton *event,
                                     gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(widget), "note");

    if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
    if (!note) return FALSE;
    show_card_menu(aw, note, event);
    return TRUE;
}

/* Gestisce clic destro e clic singolo sulle card. */
static gboolean on_flow_button_press(GtkWidget *widget, GdkEventButton *event,
                                     gpointer data) {
    ArchiveWindow *aw = data;
    GtkFlowBoxChild *child;
    Note *note;

    if (event->type != GDK_BUTTON_PRESS) return FALSE;
    child = gtk_flow_box_get_child_at_pos(GTK_FLOW_BOX(widget),
                                          (gint)event->x, (gint)event->y);
    if (!child) return FALSE;
    note = g_object_get_data(G_OBJECT(child), "note");

    if (event->button == 3) {
        if (note) show_card_menu(aw, note, event);
        return TRUE;
    }
    if (event->button != 1) return FALSE;
    {
        gint index = gtk_flow_box_child_get_index(child);

        if (event->state & GDK_SHIFT_MASK) {
            if (aw->selection_anchor < 0 || index < 0) {
                aw->selection_anchor = index;
                return FALSE;
            }
            gint first = MIN(aw->selection_anchor, index);
            gint last = MAX(aw->selection_anchor, index);
            gint i;

            gtk_flow_box_unselect_all(GTK_FLOW_BOX(widget));
            for (i = first; i <= last; ++i) {
                GtkFlowBoxChild *range =
                    gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(widget), i);

                if (range != NULL)
                    gtk_flow_box_select_child(GTK_FLOW_BOX(widget), range);
            }
            gtk_widget_grab_focus(GTK_WIDGET(child));
            aw->selection_anchor = index;
            return TRUE;
        }
        if (event->state & GDK_CONTROL_MASK) {
            aw->selection_anchor = index;
            return FALSE;
        }
        aw->selection_anchor = index;
    }
    if (note && aw->open_cb) aw->open_cb(note, aw->open_data);
    return TRUE;
}

/* Apre la card attivata da tastiera. */
static void on_flow_child_activated(GtkFlowBox *box, GtkFlowBoxChild *child,
                                    gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(child), "note");

    (void)box;
    if (note && aw->open_cb) aw->open_cb(note, aw->open_data);
}

/* Avvia l'eliminazione dalla riga calendario. */
static void on_delete_clicked(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(button), "note");

    confirm_delete(aw, note);
}

/* Apre la nota selezionata nel calendario. */
static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(row), "note");

    (void)box;
    if (note && aw->open_cb) aw->open_cb(note, aw->open_data);
}

/* Applica un provider CSS a un widget e a tutti i suoi discendenti. */
static void apply_card_provider(GtkWidget *widget, GtkCssProvider *provider) {
    GtkStyleContext *context = gtk_widget_get_style_context(widget);

    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    if (GTK_IS_CONTAINER(widget)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        GList *item;

        for (item = children; item != NULL; item = item->next)
            apply_card_provider(GTK_WIDGET(item->data), provider);
        g_list_free(children);
    }
}

/* Costruisce la card tile per una nota. */
static GtkWidget *build_note_card(ArchiveWindow *aw, Note *note,
                                  const char *query) {
    static guint card_serial = 0;
    GtkWidget *child;
    GtkWidget *card;
    GtkWidget *header_row;
    GtkWidget *content;
    GtkWidget *title;
    GtkWidget *excerpt_label;
    GtkWidget *footer;
    GtkWidget *tags;
    GtkCssProvider *provider;
    GtkStyleContext *context;
    PangoAttrList *attrs;
    char *value;
    char *tags_text;
    char *excerpt;
    char *css;
    char class_name[64];
    const char *paper;
    const char *bar;
    const char *ink;

    child = gtk_flow_box_child_new();
    card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(card, ARCHIVE_CARD_WIDTH, ARCHIVE_CARD_HEIGHT);
    gtk_widget_set_halign(card, GTK_ALIGN_START);
    gtk_widget_set_valign(card, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(card),
                                "archive-card");

    paper = palette_color_hex(note->palette, note->color);
    bar = palette_bar_hex(note->palette, note->color);
    ink = palette_ink_hex(note->palette, note->color);

    ++card_serial;
    g_snprintf(class_name, sizeof(class_name), "note-card-%u", card_serial);
    context = gtk_widget_get_style_context(card);
    gtk_style_context_add_class(context, class_name);
    css = g_strdup_printf(
        ".%s { background-color: %s; border: 1px solid %s;"
        " border-radius: 0; }"
        ".%s .card-header { background-color: %s; padding: 4px 10px;"
        " min-height: 24px; }"
        ".%s label { color: %s; }"
        ".%s:selected { border: 2px solid %s;"
        " box-shadow: inset 0 0 0 1px %s; }",
        class_name, paper, bar, class_name, bar, class_name, ink,
        class_name, ink, ARCHIVE_SELECTION);
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    g_free(css);

    header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(header_row),
                                "card-header");
    gtk_style_context_add_class(gtk_widget_get_style_context(header_row),
                                "archive-card-header");

    value = note_card_title(note);
    title = gtk_label_new(value);
    g_free(value);
    attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_lines(GTK_LABEL(title), 1);
    gtk_label_set_width_chars(GTK_LABEL(title), 26);
    gtk_label_set_max_width_chars(GTK_LABEL(title), 26);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(title),
                                "archive-card-title");
    gtk_box_pack_start(GTK_BOX(header_row), title, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(card), header_row, FALSE, FALSE, 0);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(content, 10);
    gtk_widget_set_margin_end(content, 10);
    gtk_widget_set_margin_top(content, 6);
    gtk_widget_set_margin_bottom(content, 6);
    gtk_widget_set_vexpand(content, TRUE);

    excerpt = note_excerpt(note->text);
    excerpt_label = gtk_label_new(NULL);
    if (query && *query) {
        char *markup = highlight_excerpt(excerpt, query);
        gtk_label_set_markup(GTK_LABEL(excerpt_label), markup);
        g_free(markup);
    } else {
        gtk_label_set_text(GTK_LABEL(excerpt_label), excerpt);
    }
    g_free(excerpt);
    gtk_label_set_xalign(GTK_LABEL(excerpt_label), 0.0f);
    gtk_label_set_yalign(GTK_LABEL(excerpt_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(excerpt_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap(GTK_LABEL(excerpt_label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(excerpt_label),
                                 PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(excerpt_label), 4);
    gtk_label_set_width_chars(GTK_LABEL(excerpt_label), 34);
    gtk_label_set_max_width_chars(GTK_LABEL(excerpt_label), 34);
    gtk_widget_set_vexpand(excerpt_label, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(excerpt_label),
                                "archive-card-excerpt");
    gtk_box_pack_start(GTK_BOX(content), excerpt_label, TRUE, TRUE, 0);

    tags_text = note_tag_list(note->tags);
    if (*tags_text) {
        footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_style_context_add_class(gtk_widget_get_style_context(footer),
                                    "archive-card-footer");
        tags = gtk_label_new(tags_text);
        gtk_label_set_xalign(GTK_LABEL(tags), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(tags), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(tags), 34);
        gtk_widget_set_hexpand(tags, TRUE);
        gtk_box_pack_start(GTK_BOX(footer), tags, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(content), footer, FALSE, FALSE, 0);
    }
    g_free(tags_text);

    gtk_box_pack_start(GTK_BOX(card), content, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(child), card);
    apply_card_provider(child, aw->css);
    apply_card_provider(card, provider);
    g_object_unref(provider);
    g_object_set_data(G_OBJECT(child), "note", note);
    g_object_set_data(G_OBJECT(child), "card", card);
    g_signal_connect(child, "button-press-event",
                     G_CALLBACK(on_card_button_press), aw);
    gtk_widget_show_all(child);
    return child;
}

/* Costruisce la riga UI per una nota (vista calendario). */
static GtkWidget *build_note_row(ArchiveWindow *aw, Note *note) {
    GtkWidget *row;
    GtkWidget *box;
    GtkWidget *title_row;
    GtkWidget *title;
    GtkWidget *remove;
    GtkWidget *excerpt;
    GtkWidget *meta;
    GtkWidget *tags;
    GtkWidget *date;
    PangoAttrList *attrs;
    char *value;

    row = gtk_list_box_row_new();
    g_object_set_data(G_OBJECT(row), "note", note);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    value = (note->has_title && note->title)
                ? g_strdup(note->title)
                : note_title(note->text);
    title = gtk_label_new(value);
    g_free(value);
    attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_pack_start(GTK_BOX(title_row), title, TRUE, TRUE, 0);

    remove = gtk_button_new_from_icon_name("user-trash-symbolic",
                                           GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
    set_accessible_label(remove, "Delete note");
    gtk_style_context_add_class(gtk_widget_get_style_context(remove), "flat");
    g_object_set_data(G_OBJECT(remove), "note", note);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_delete_clicked), aw);
    gtk_box_pack_end(GTK_BOX(title_row), remove, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), title_row, FALSE, FALSE, 0);

    value = note_excerpt(note->text);
    excerpt = gtk_label_new(value);
    g_free(value);
    gtk_label_set_xalign(GTK_LABEL(excerpt), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(excerpt), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap(GTK_LABEL(excerpt), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(excerpt), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(excerpt), 2);
    gtk_style_context_add_class(gtk_widget_get_style_context(excerpt),
                                "dim-label");
    gtk_box_pack_start(GTK_BOX(box), excerpt, FALSE, FALSE, 0);

    meta = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    value = note_tags(note->tags);
    tags = gtk_label_new(value);
    g_free(value);
    gtk_label_set_xalign(GTK_LABEL(tags), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(tags), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tags, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(tags),
                                "dim-label");
    gtk_box_pack_start(GTK_BOX(meta), tags, TRUE, TRUE, 0);

    value = note_date(note->date);
    date = gtk_label_new(value);
    g_free(value);
    gtk_label_set_xalign(GTK_LABEL(date), 1.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(date),
                                "dim-label");
    gtk_box_pack_end(GTK_BOX(meta), date, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), meta, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(row), box);
    apply_card_provider(row, aw->css);
    gtk_widget_show_all(row);
    return row;
}

/* Conferma ed elimina una nota dall'archivio. */
static void confirm_delete(ArchiveWindow *aw, Note *note) {
    GtkWidget *dialog;
    GError *error = NULL;
    gint response;
    char *id;

    if (!aw || !note || !note->id) return;

    dialog = gtk_message_dialog_new(GTK_WINDOW(aw->window),
                                    GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_YES_NO,
                                    "Delete this note?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "This action cannot be undone.");
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_YES) return;

    id = g_strdup(note->id);
    if (!store_delete(aw->store, id, &error)) {
        GtkWidget *failure = gtk_message_dialog_new(
            GTK_WINDOW(aw->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Could not delete the note: %s",
            error ? error->message : "unknown error");
        gtk_dialog_run(GTK_DIALOG(failure));
        gtk_widget_destroy(failure);
        if (error) g_error_free(error);
        g_free(id);
        return;
    }
    g_free(id);
    archive_window_refresh(aw->window);
}

/* Elimina tutte le note selezionate nel flow box. */
static void delete_selected_notes(ArchiveWindow *aw) {
    GList *selected;
    GList *item;
    guint count;
    GtkWidget *dialog;
    gint response;
    GPtrArray *ids;
    GHashTable *seen;
    guint failures = 0;
    guint i;
    char *message;

    selected = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(aw->flow));
    if (!selected) return;

    ids = g_ptr_array_new_with_free_func(g_free);
    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (item = selected; item != NULL; item = item->next) {
        Note *note = g_object_get_data(G_OBJECT(item->data), "note");

        if (!note || !note->id) continue;
        if (g_hash_table_contains(seen, note->id)) continue;
        g_hash_table_add(seen, g_strdup(note->id));
        g_ptr_array_add(ids, g_strdup(note->id));
    }
    g_hash_table_destroy(seen);
    g_list_free(selected);

    count = ids->len;
    if (!count) {
        g_ptr_array_free(ids, TRUE);
        return;
    }

    message = g_strdup_printf("Delete %u selected note%s from the archive?",
                              count, count == 1 ? "" : "s");
    dialog = gtk_message_dialog_new(GTK_WINDOW(aw->window),
                                    GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_YES_NO,
                                    "%s", message);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "This action cannot be undone.");
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(message);
    if (response != GTK_RESPONSE_YES) {
        g_ptr_array_free(ids, TRUE);
        return;
    }

    for (i = 0; i < ids->len; ++i) {
        GError *error = NULL;
        const char *id = g_ptr_array_index(ids, i);
        if (aw->delete_cb != NULL)
            aw->delete_cb(id, aw->delete_data);
        if (!store_delete(aw->store, id, &error)) {
            ++failures;
            if (error) g_error_free(error);
        }
    }
    g_ptr_array_free(ids, TRUE);
    archive_window_refresh(aw->window);

    if (failures) {
        GtkWidget *failure = gtk_message_dialog_new(
            GTK_WINDOW(aw->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Some notes could not be deleted.");
        gtk_dialog_run(GTK_DIALOG(failure));
        gtk_widget_destroy(failure);
    }
}

/* Esc chiude la lista del giorno e riporta il fuoco al calendario. */
static gboolean on_day_list_key_press(GtkWidget *widget, GdkEventKey *event,
                                      gpointer data) {
    ArchiveWindow *aw = data;

    (void)widget;
    if (event->keyval == GDK_KEY_Escape) {
        aw->cal_selected = 0;
        update_calendar(aw);
        gtk_widget_grab_focus(aw->calendar);
        return TRUE;
    }
    return FALSE;
}

/* Allinea i toggle Tags/Date allo stato della sidebar. */
static void update_sidebar_buttons(ArchiveWindow *aw) {
    if (!aw || !aw->tags_button || !aw->date_button) return;
    g_signal_handlers_block_by_func(aw->tags_button,
                                    G_CALLBACK(on_sidebar_toggled), aw);
    g_signal_handlers_block_by_func(aw->date_button,
                                    G_CALLBACK(on_sidebar_toggled), aw);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(aw->tags_button),
                                 aw->sidebar_tags);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(aw->date_button),
                                 !aw->sidebar_tags);
    g_signal_handlers_unblock_by_func(aw->tags_button,
                                      G_CALLBACK(on_sidebar_toggled), aw);
    g_signal_handlers_unblock_by_func(aw->date_button,
                                      G_CALLBACK(on_sidebar_toggled), aw);
}

/* Alterna albero date/tag nella sidebar. */
static void on_sidebar_toggled(GtkToggleButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    gboolean tags = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(button), "archive-tags")) != 0;

    if (gtk_toggle_button_get_active(button) && aw->sidebar_tags != tags) {
        aw->sidebar_tags = tags;
        gtk_stack_set_visible_child_name(
            GTK_STACK(aw->sidebar_stack),
            tags ? "tags" : "dates");
    }
    update_sidebar_buttons(aw);
}

/* Alterna vista calendario/lista. */
static void on_calendar_clicked(GtkToggleButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    gboolean enabled = gtk_toggle_button_get_active(button);

    gtk_stack_set_visible_child_name(GTK_STACK(aw->stack),
                                     enabled ? "calendar" : "list");
    if (enabled) {
        gtk_widget_hide(aw->sidebar);
        gtk_widget_hide(aw->tags_button);
        gtk_widget_hide(aw->date_button);
        update_calendar(aw);
    } else {
        gtk_widget_show(aw->sidebar);
        gtk_widget_show(aw->tags_button);
        gtk_widget_show(aw->date_button);
        update_sidebar_buttons(aw);
        repopulate_list(aw);
    }
    set_accessible_label(GTK_WIDGET(button),
                         enabled ? "Show notes" : "Show calendar");
    update_window_title(aw);
}

/* Annulla il timer di debounce della ricerca. */
static void cancel_search_timeout(ArchiveWindow *aw) {
    if (aw->search_timeout_id) {
        g_source_remove(aw->search_timeout_id);
        aw->search_timeout_id = 0;
    }
}

/* Esegue la ricerca rimandata dal debounce. */
static gboolean search_debounce_cb(gpointer data) {
    ArchiveWindow *aw = data;

    aw->search_timeout_id = 0;
    repopulate_list(aw);
    return G_SOURCE_REMOVE;
}

/* Ripoppola la lista dopo la ricerca, con debounce di 150 ms. */
static void on_search_changed(GtkSearchEntry *entry, gpointer data) {
    ArchiveWindow *aw = data;

    (void)entry;
    cancel_search_timeout(aw);
    aw->search_timeout_id = g_timeout_add(150, search_debounce_cb, aw);
}

/* Comparatore per chiavi numeriche. */
static gint compare_uint_keys(gconstpointer a, gconstpointer b) {
    guint left = *(const guint *)a;
    guint right = *(const guint *)b;
    return left > right ? -1 : left < right ? 1 : 0;
}

/* Comparatore per nomi di tag. */
static gint compare_tag_keys(gconstpointer a, gconstpointer b) {
    return tags_compare((const char *)a, (const char *)b);
}

/* Incrementa il contatore associato a una chiave numerica. */
static void count_key(GHashTable *table, guint key) {
    guint count = GPOINTER_TO_UINT(
        g_hash_table_lookup(table, GUINT_TO_POINTER(key)));
    g_hash_table_insert(table, GUINT_TO_POINTER(key),
                        GUINT_TO_POINTER(count + 1));
}

/* Estrae le chiavi numeriche ordinate da una hash table. */
static GArray *sorted_table_keys(GHashTable *table) {
    GArray *keys = g_array_new(FALSE, FALSE, sizeof(guint));
    GList *items = g_hash_table_get_keys(table);
    GList *item;

    for (item = items; item != NULL; item = item->next) {
        guint key = GPOINTER_TO_UINT(item->data);
        g_array_append_val(keys, key);
    }
    g_list_free(items);
    g_array_sort(keys, compare_uint_keys);
    return keys;
}

/* Cerca ricorsivamente un nodo data per tipo e valore. */
static gboolean find_date_node(GtkTreeModel *model, GtkTreeIter *parent,
                               gint kind, guint32 value, GtkTreeIter *out) {
    GtkTreeIter iter;
    gboolean has;

    has = parent ? gtk_tree_model_iter_children(model, &iter, parent)
                 : gtk_tree_model_get_iter_first(model, &iter);
    while (has) {
        gint node_kind = 0;
        guint node_value = 0;

        gtk_tree_model_get(model, &iter,
                           DATE_COL_KIND, &node_kind,
                           DATE_COL_VALUE, &node_value, -1);
        if (node_kind == kind && node_value == value) {
            *out = iter;
            return TRUE;
        }
        if (find_date_node(model, &iter, kind, value, out)) return TRUE;
        has = gtk_tree_model_iter_next(model, &iter);
    }
    return FALSE;
}

/* Seleziona il nodo data corrente o la radice. */
static void select_date_node(ArchiveWindow *aw) {
    GtkTreeSelection *selection;
    GtkTreeIter iter;

    if (!aw->date_tree || !aw->date_model) return;
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(aw->date_tree));
    if (!find_date_node(GTK_TREE_MODEL(aw->date_model), NULL,
                        aw->date_kind, aw->date_filter, &iter)) {
        aw->date_filter = 0;
        aw->date_kind = 0;
        if (!find_date_node(GTK_TREE_MODEL(aw->date_model), NULL, 0, 0, &iter))
            return;
    }
    gtk_tree_selection_select_iter(selection, &iter);
    {
        GtkTreePath *path = gtk_tree_model_get_path(
            GTK_TREE_MODEL(aw->date_model), &iter);

        if (path != NULL) {
            gtk_tree_view_expand_to_path(GTK_TREE_VIEW(aw->date_tree), path);
            gtk_tree_path_free(path);
        }
    }
}

/* Espande una riga dell'albero indicata dall'iter. */
static void expand_tree_iter(GtkWidget *tree, GtkTreeModel *model,
                             GtkTreeIter *iter) {
    GtkTreePath *path = gtk_tree_model_get_path(model, iter);

    if (path == NULL) return;
    gtk_tree_view_expand_row(GTK_TREE_VIEW(tree), path, FALSE);
    gtk_tree_path_free(path);
}

/* Seleziona il nodo tag corrente o la radice. */
static void select_tag_node(ArchiveWindow *aw) {
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter root;

    if (!aw->tag_tree || !aw->tag_model) return;
    model = GTK_TREE_MODEL(aw->tag_model);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(aw->tag_tree));
    if (!gtk_tree_model_get_iter_first(model, &root)) return;
    if (aw->tag_filter && *aw->tag_filter) {
        GtkTreeIter child;
        gboolean has = gtk_tree_model_iter_children(model, &child, &root);

        while (has) {
            char *tag = NULL;
            gtk_tree_model_get(model, &child, TAG_COL_VALUE, &tag, -1);
            if (g_strcmp0(tag, aw->tag_filter) == 0) {
                g_free(tag);
                gtk_tree_selection_select_iter(selection, &child);
                return;
            }
            g_free(tag);
            has = gtk_tree_model_iter_next(model, &child);
        }
    }
    gtk_tree_selection_select_iter(selection, &root);
}

/* Costruisce l'albero date con conteggi. */
static void populate_date_tree(ArchiveWindow *aw) {
    GtkTreeStore *model;
    GtkTreeIter root;
    GHashTable *years;
    GHashTable *months;
    GHashTable *days;
    GArray *year_keys;
    GArray *month_keys;
    GArray *day_keys;
    guint total = 0;
    guint i;
    guint j;
    guint k;

    if (!aw->date_model) return;
    model = aw->date_model;

    aw->updating_sidebar = TRUE;
    gtk_tree_store_clear(model);

    years = g_hash_table_new(g_direct_hash, g_direct_equal);
    months = g_hash_table_new(g_direct_hash, g_direct_equal);
    days = g_hash_table_new(g_direct_hash, g_direct_equal);

    if (aw->store && aw->store->notes) {
        total = aw->store->notes->len;
        for (i = 0; i < aw->store->notes->len; ++i) {
            Note *note = g_ptr_array_index(aw->store->notes, i);

            if (!note || !note->created || !date_valid(note->created)) continue;
            count_key(years, note->created / 10000u);
            count_key(months, note->created / 100u);
            count_key(days, note->created);
        }
    }

    gtk_tree_store_append(model, &root, NULL);
    {
        char *label = g_strdup_printf("All notes (%u)", total);
        gtk_tree_store_set(model, &root,
                           DATE_COL_LABEL, label,
                           DATE_COL_VALUE, 0u,
                           DATE_COL_KIND, 0, -1);
        g_free(label);
    }

    year_keys = sorted_table_keys(years);
    month_keys = sorted_table_keys(months);
    day_keys = sorted_table_keys(days);

    for (i = 0; i < year_keys->len; ++i) {
        guint year = g_array_index(year_keys, guint, i);
        GtkTreeIter year_iter;
        char *label = g_strdup_printf("%04u", year);

        gtk_tree_store_append(model, &year_iter, &root);
        gtk_tree_store_set(model, &year_iter,
                           DATE_COL_LABEL, label,
                           DATE_COL_VALUE, year * 10000u,
                           DATE_COL_KIND, 1, -1);
        g_free(label);

        for (j = 0; j < month_keys->len; ++j) {
            guint month = g_array_index(month_keys, guint, j);
            GtkTreeIter month_iter;
            guint month_number;

            if (month / 100u != year) continue;
            month_number = month % 100u;
            label = g_strdup_printf(
                "%s", month_number >= 1 && month_number <= 12
                          ? MONTH_NAMES[month_number - 1]
                          : "");
            gtk_tree_store_append(model, &month_iter, &year_iter);
            gtk_tree_store_set(model, &month_iter,
                               DATE_COL_LABEL, label,
                               DATE_COL_VALUE, month * 100u,
                               DATE_COL_KIND, 2, -1);
            g_free(label);

            for (k = 0; k < day_keys->len; ++k) {
                guint day = g_array_index(day_keys, guint, k);
                GtkTreeIter day_iter;

                if (day / 100u != month) continue;
                label = g_strdup_printf("%02u", day % 100u);
                gtk_tree_store_append(model, &day_iter, &month_iter);
                gtk_tree_store_set(model, &day_iter,
                                   DATE_COL_LABEL, label,
                                   DATE_COL_VALUE, day,
                                   DATE_COL_KIND, 3, -1);
                g_free(label);
            }
        }
    }

    g_array_free(year_keys, TRUE);
    g_array_free(month_keys, TRUE);
    g_array_free(day_keys, TRUE);
    g_hash_table_destroy(years);
    g_hash_table_destroy(months);
    g_hash_table_destroy(days);

    {
        GtkTreeIter year, month;

        expand_tree_iter(aw->date_tree, GTK_TREE_MODEL(model), &root);
        if (gtk_tree_model_iter_children(GTK_TREE_MODEL(model), &year, &root)) {
            expand_tree_iter(aw->date_tree, GTK_TREE_MODEL(model), &year);
            if (gtk_tree_model_iter_children(GTK_TREE_MODEL(model), &month,
                                             &year))
                expand_tree_iter(aw->date_tree, GTK_TREE_MODEL(model), &month);
        }
    }
    select_date_node(aw);
    aw->updating_sidebar = FALSE;
}

/* Costruisce l'albero dei tag con conteggi. */
static void populate_tag_tree(ArchiveWindow *aw) {
    GtkTreeStore *model;
    GHashTable *counts;
    GtkTreeIter root;
    GList *keys;
    GList *item;
    guint i;

    if (!aw->tag_model) return;
    model = aw->tag_model;

    aw->updating_sidebar = TRUE;
    gtk_tree_store_clear(model);

    counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (aw->store && aw->store->notes) {
        for (i = 0; i < aw->store->notes->len; ++i) {
            Note *note = g_ptr_array_index(aw->store->notes, i);
            GPtrArray *tags;
            guint j;

            if (!note) continue;
            tags = tags_split(note->tags);
            for (j = 0; j < tags->len; ++j) {
                const char *name = g_ptr_array_index(tags, j);
                guint count = GPOINTER_TO_UINT(
                    g_hash_table_lookup(counts, name));
                g_hash_table_insert(counts, g_strdup(name),
                                    GUINT_TO_POINTER(count + 1));
            }
            g_ptr_array_free(tags, TRUE);
        }
    }

    gtk_tree_store_append(model, &root, NULL);
    {
        char *label = g_strdup_printf(
            "All notes (%u)",
            aw->store && aw->store->notes ? aw->store->notes->len : 0);
        gtk_tree_store_set(model, &root,
                           TAG_COL_LABEL, label,
                           TAG_COL_VALUE, "",
                           TAG_COL_COUNT, "", -1);
        g_free(label);
    }

    keys = g_hash_table_get_keys(counts);
    keys = g_list_sort(keys, compare_tag_keys);
    for (item = keys; item != NULL; item = item->next) {
        const char *name = item->data;
        guint count = GPOINTER_TO_UINT(g_hash_table_lookup(counts, name));
        GtkTreeIter child;
        char number[16];

        g_snprintf(number, sizeof(number), "%u", count);
        gtk_tree_store_append(model, &child, &root);
        gtk_tree_store_set(model, &child,
                           TAG_COL_LABEL, name,
                           TAG_COL_VALUE, name,
                           TAG_COL_COUNT, number, -1);
    }
    g_list_free(keys);
    g_hash_table_destroy(counts);

    expand_tree_iter(aw->tag_tree, GTK_TREE_MODEL(model), &root);
    select_tag_node(aw);
    aw->updating_sidebar = FALSE;
}

/* Ricostruisce entrambi gli alberi della sidebar. */
static void populate_sidebar(ArchiveWindow *aw) {
    populate_date_tree(aw);
    populate_tag_tree(aw);
}

/* Salva la posizione di scorrimento corrente del flow box. */
static void capture_flow_scroll(ArchiveWindow *aw) {
    GtkAdjustment *hadj;
    GtkAdjustment *vadj;

    if (!aw || !aw->flow_scroll) return;
    hadj = gtk_scrolled_window_get_hadjustment(
        GTK_SCROLLED_WINDOW(aw->flow_scroll));
    vadj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(aw->flow_scroll));
    if (hadj) aw->flow_scroll_x = gtk_adjustment_get_value(hadj);
    if (vadj) aw->flow_scroll_y = gtk_adjustment_get_value(vadj);
}

/* Ripristina lo scroll del flow box quando il layout è aggiornato. */
static gboolean restore_flow_scroll(gpointer data) {
    ArchiveWindow *aw = data;
    GtkAdjustment *hadj;
    GtkAdjustment *vadj;

    aw->flow_scroll_restore_id = 0;
    if (!aw->flow_scroll) return G_SOURCE_REMOVE;
    hadj = gtk_scrolled_window_get_hadjustment(
        GTK_SCROLLED_WINDOW(aw->flow_scroll));
    vadj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(aw->flow_scroll));
    if (hadj) gtk_adjustment_set_value(hadj, aw->flow_scroll_x);
    if (vadj) gtk_adjustment_set_value(vadj, aw->flow_scroll_y);
    return G_SOURCE_REMOVE;
}

/* Programma il ripristino dello scroll al prossimo idle. */
static void restore_flow_scroll_later(ArchiveWindow *aw) {
    if (!aw) return;
    if (aw->flow_scroll_restore_id)
        g_source_remove(aw->flow_scroll_restore_id);
    aw->flow_scroll_restore_id =
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, restore_flow_scroll, aw, NULL);
}

/* Titoli coerenti con Windows; la lista contiene sempre "Archive". */
static void update_window_title(ArchiveWindow *aw) {
    const char *visible;
    const char *text;
    const char *title;

    if (!aw || !aw->header) return;
    visible = aw->stack != NULL
                  ? gtk_stack_get_visible_child_name(GTK_STACK(aw->stack))
                  : NULL;
    text = aw->search != NULL ? gtk_entry_get_text(GTK_ENTRY(aw->search)) : NULL;
    if (visible != NULL && g_strcmp0(visible, "calendar") == 0) {
        gboolean filtered = (text != NULL && *text != '\0') ||
                            (aw->tag_filter != NULL && *aw->tag_filter != '\0') ||
                            (aw->date_filter != 0 && aw->date_kind > 0);

        title = filtered ? "Calendar - Filtered notes" : "Calendar";
    } else {
        title = "Notes Archive";
    }
    gtk_header_bar_set_title(GTK_HEADER_BAR(aw->header), title);
    gtk_window_set_title(GTK_WINDOW(aw->window), title);
}

/* Ricarica la lista con filtri e ricerca. */
static void repopulate_list(ArchiveWindow *aw) {
    const char *text;
    const char *query;
    GError *error = NULL;
    GPtrArray *results;
    GString *subtitle;
    guint total = 0;
    guint shown = 0;
    guint i;

    if (!aw || !aw->flow) return;

    clear_flowbox(aw->flow);
    aw->first_note = NULL;
    aw->selection_anchor = -1;
    if (!aw->visible_notes)
        aw->visible_notes = g_ptr_array_new();
    else
        g_ptr_array_set_size(aw->visible_notes, 0);

    text = gtk_entry_get_text(GTK_ENTRY(aw->search));
    query = (text && *text) ? text : NULL;

    results = store_search(aw->store, query, aw->tag_filter, &error);
    if (results) {
        for (i = 0; i < results->len; ++i) {
            Note *note = g_ptr_array_index(results, i);

            if (!note) continue;
            if (!note_matches_date(aw, note->created)) continue;
            g_ptr_array_add(aw->visible_notes, note);
            ++total;
            gtk_flow_box_insert(GTK_FLOW_BOX(aw->flow),
                                build_note_card(aw, note, query), -1);
            if (!aw->first_note) aw->first_note = note;
            ++shown;
        }
        g_ptr_array_free(results, FALSE);
    }

    subtitle = g_string_new(NULL);
    if (error) {
        g_string_append_printf(subtitle, "Search failed: %s", error->message);
        g_error_free(error);
    } else if (total == 0) {
        g_string_append(subtitle, (query && *query) || (aw->tag_filter && *aw->tag_filter) ||
                                      (aw->date_filter && aw->date_kind > 0)
                                  ? "No matching notes"
                                  : "No notes");
    } else {
        g_string_append_printf(subtitle, "Showing %u of %u note%s",
                               shown, total, total == 1 ? "" : "s");
    }
    if (aw->tag_filter && *aw->tag_filter)
        g_string_append_printf(subtitle, " \302\267 tag: %s", aw->tag_filter);
    if (query)
        g_string_append_printf(subtitle, " \302\267 \"%s\"", query);
    if (aw->date_filter && aw->date_kind > 0) {
        if (aw->date_kind == 1)
            g_string_append_printf(subtitle, " \302\267 year: %04u",
                                   aw->date_filter / 10000u);
        else if (aw->date_kind == 2)
            g_string_append_printf(subtitle, " \302\267 month: %04u-%02u",
                                   aw->date_filter / 10000u,
                                   aw->date_filter / 100u % 100u);
        else {
            char *formatted = date_format(aw->date_filter);
            g_string_append_printf(subtitle, " \302\267 date: %s",
                                   formatted ? formatted : "");
            g_free(formatted);
        }
    }
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(aw->header), subtitle->str);
    g_string_free(subtitle, TRUE);
    update_window_title(aw);
    update_calendar(aw);
}

/* Data odierna in formato AAAAMMGG (0 se non disponibile). */
static guint32 calendar_today_date(void) {
    GDateTime *now = g_date_time_new_now_local();
    guint32 date;

    if (!now) return 0;
    date = (guint32)(g_date_time_get_year(now) * 10000 +
                     g_date_time_get_month(now) * 100 +
                     g_date_time_get_day_of_month(now));
    g_date_time_unref(now);
    return date;
}

/* Colonne della griglia annuale in base alla larghezza disponibile. */
static gint calendar_columns(gint width) {
    return width >= 900 ? 4 : 3;
}

/* Data sotto il puntatore nella vista annuale (0 se nessuna). */
static guint32 calendar_date_at(ArchiveWindow *aw, gint width, gint height,
                                gint px, gint py) {
    gint cols, rows, mw, mh, idx, cw, ch, month, gx, gy, slot, day;
    guint32 first;

    if (!aw || width <= 0 || height <= 0 || px < 0 || py < 0) return 0;
    cols = calendar_columns(width);
    rows = 12 / cols;
    mw = width / cols;
    mh = height / rows;
    idx = (py / mh) * cols + (px / mw);
    if (idx < 0 || idx >= 12) return 0;
    month = idx + 1;
    cw = (mw - 2 * CAL_PAD) / 7;
    ch = (mh - CAL_GRID_TOP - CAL_PAD) / 6;
    if (cw < 1 || ch < 1) return 0;
    gx = px - (idx % cols) * mw - CAL_PAD;
    gy = py - (idx / cols) * mh - CAL_GRID_TOP;
    if (gx < 0 || gx >= cw * 7 || gy < 0 || gy >= ch * 6) return 0;
    first = aw->cal_year * 10000u + (guint32)month * 100u + 1u;
    slot = (gy / ch) * 7 + (gx / cw);
    day = slot - date_weekday(first) + 1;
    if (day < 1 || day > date_days((gint)aw->cal_year, month)) return 0;
    return aw->cal_year * 10000u + (guint32)month * 100u + (guint32)day;
}

/* Disegna testo centrato in un rettangolo con Pango. */
static void calendar_draw_text(cairo_t *cr, PangoLayout *layout,
                               const char *text, PangoFontDescription *font,
                               const GdkRGBA *color, gint x, gint y,
                               gint w, gint h, PangoAlignment align) {
    gint th = 0;

    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout, w > 0 ? w * PANGO_SCALE : -1);
    pango_layout_set_alignment(layout, align);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_layout_get_pixel_size(layout, NULL, &th);
    gdk_cairo_set_source_rgba(cr, color);
    cairo_move_to(cr, x, y + (h - th) / 2.0);
    pango_cairo_show_layout(cr, layout);
}

/* Disegna la griglia annuale con marcatori e selezione. */
static gboolean on_calendar_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    static const char *const month_names[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    static const char *const week_names[7] = {
        "M", "T", "W", "T", "F", "S", "S"
    };
    ArchiveWindow *aw = data;
    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    PangoFontDescription *base, *heading, *weekday, *day_font, *badge_font;
    PangoLayout *layout;
    GdkRGBA ink, dim, edge, accent, selected_ink, hover;
    gint width = gtk_widget_get_allocated_width(widget);
    gint height = gtk_widget_get_allocated_height(widget);
    gint cols, rows, mw, mh, month;

    gtk_render_background(context, cr, 0, 0, width, height);
    if (width <= 0 || height <= 0) return FALSE;

    gtk_style_context_get_color(context,
                                gtk_style_context_get_state(context), &ink);
    dim = ink;
    dim.alpha *= 0.55;
    edge = ink;
    edge.alpha *= 0.35;
    hover = ink;
    hover.alpha *= 0.08;
    accent = (GdkRGBA){211 / 255.0, 194 / 255.0, 162 / 255.0, 0.80};
    selected_ink = (GdkRGBA){0.23, 0.20, 0.17, 1.0};

    base = pango_font_description_copy(
        pango_context_get_font_description(gtk_widget_get_pango_context(widget)));
    heading = pango_font_description_copy(base);
    pango_font_description_set_weight(heading, PANGO_WEIGHT_BOLD);
    pango_font_description_set_size(heading, 12 * PANGO_SCALE);
    weekday = pango_font_description_copy(base);
    pango_font_description_set_size(weekday, 8 * PANGO_SCALE);
    day_font = pango_font_description_copy(base);
    pango_font_description_set_size(day_font, 9 * PANGO_SCALE);
    badge_font = pango_font_description_copy(base);
    pango_font_description_set_weight(badge_font, PANGO_WEIGHT_BOLD);
    pango_font_description_set_size(badge_font, 7 * PANGO_SCALE);
    layout = pango_cairo_create_layout(cr);

    cols = calendar_columns(width);
    rows = 12 / cols;
    mw = width / cols;
    mh = height / rows;

    for (month = 1; month <= 12; ++month) {
        gint idx = month - 1;
        gint ox = (idx % cols) * mw;
        gint oy = (idx / cols) * mh;
        gint cw = (mw - 2 * CAL_PAD) / 7;
        gint ch = (mh - CAL_GRID_TOP - CAL_PAD) / 6;
        guint32 first = aw->cal_year * 10000u + (guint32)month * 100u + 1u;
        gint d;

        if (cw < 1 || ch < 1) continue;

        calendar_draw_text(cr, layout, month_names[idx], heading, &ink,
                           ox, oy, mw, CAL_TITLE_H, PANGO_ALIGN_CENTER);
        for (d = 0; d < 7; ++d)
            calendar_draw_text(cr, layout, week_names[d], weekday, &dim,
                               ox + CAL_PAD + d * cw, oy + CAL_TITLE_H,
                               cw, CAL_WEEK_H, PANGO_ALIGN_CENTER);

        for (d = 1; d <= date_days((gint)aw->cal_year, month); ++d) {
            guint32 date = aw->cal_year * 10000u + (guint32)month * 100u +
                           (guint32)d;
            guint count = GPOINTER_TO_UINT(
                g_hash_table_lookup(aw->day_counts, GUINT_TO_POINTER(date)));
            gint x, y, w, h, slot;
            char number[8];

            slot = date_weekday(first) + d - 1;
            x = ox + CAL_PAD + (slot % 7) * cw;
            y = oy + CAL_GRID_TOP + (slot / 7) * ch;
            w = cw;
            h = ch;

            if (date == aw->cal_hover && date != aw->cal_selected) {
                cairo_set_source_rgba(cr, hover.red, hover.green,
                                      hover.blue, hover.alpha);
                cairo_rectangle(cr, x, y, w, h);
                cairo_fill(cr);
            }
            if (date == aw->cal_selected) {
                gdouble r = MIN(w, h) / 2.0 - 1.0;

                cairo_set_source_rgba(cr, accent.red, accent.green,
                                      accent.blue, accent.alpha);
                cairo_arc(cr, x + w / 2.0, y + h / 2.0, r, 0, 2 * G_PI);
                cairo_fill(cr);
            } else if (count > 0) {
                gdouble r = MIN(w, h) / 2.0 - 1.0;

                cairo_set_source_rgba(cr, edge.red, edge.green,
                                      edge.blue, edge.alpha);
                cairo_set_line_width(cr, 1.0);
                cairo_arc(cr, x + w / 2.0, y + h / 2.0, r, 0, 2 * G_PI);
                cairo_stroke(cr);
            }

            g_snprintf(number, sizeof(number), "%d", d);
            calendar_draw_text(cr, layout, number, day_font,
                               date == aw->cal_selected ? &selected_ink : &ink,
                               x, y, w, h, PANGO_ALIGN_CENTER);

            if (count > 1) {
                gint br = MIN(w, h) / 4;
                gint bx, by;
                char label[8];

                if (br < 4) br = 4;
                bx = x + w - br - 1;
                by = y + 1;
                g_snprintf(label, sizeof(label), "%u", count > 9 ? 9u : count);
                cairo_set_source_rgba(cr, accent.red, accent.green,
                                      accent.blue, 0.95);
                cairo_arc(cr, bx + br / 2.0, by + br / 2.0, br / 2.0,
                          0, 2 * G_PI);
                cairo_fill(cr);
                calendar_draw_text(cr, layout, label, badge_font,
                                   &selected_ink, bx, by, br, br,
                                   PANGO_ALIGN_CENTER);
            }

            if (date == aw->cal_today) {
                cairo_set_source_rgba(cr, ink.red, ink.green, ink.blue, 0.9);
                cairo_set_line_width(cr, 1.5);
                cairo_move_to(cr, x + w * 0.2, y + h - 2.0);
                cairo_line_to(cr, x + w * 0.8, y + h - 2.0);
                cairo_stroke(cr);
            }

            if (date == aw->cal_focus && gtk_widget_has_focus(widget)) {
                const double dashes[2] = {2.0, 2.0};

                cairo_set_source_rgba(cr, ink.red, ink.green, ink.blue, 0.6);
                cairo_set_line_width(cr, 1.0);
                cairo_set_dash(cr, dashes, 2, 0);
                cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1, h - 1);
                cairo_stroke(cr);
                cairo_set_dash(cr, NULL, 0, 0);
            }
        }
    }

    g_object_unref(layout);
    pango_font_description_free(base);
    pango_font_description_free(heading);
    pango_font_description_free(weekday);
    pango_font_description_free(day_font);
    pango_font_description_free(badge_font);
    return FALSE;
}

/* Scrive l'anno nel campo senza innescare la validazione. */
static void calendar_sync_year_entry(ArchiveWindow *aw) {
    char text[16];

    if (!aw->calendar_year) return;
    g_snprintf(text, sizeof(text), "%u", aw->cal_year);
    aw->cal_updating = TRUE;
    gtk_entry_set_text(GTK_ENTRY(aw->calendar_year), text);
    aw->cal_updating = FALSE;
}

/* Seleziona una data e aggiorna lista e disegno. */
static void calendar_select_date(ArchiveWindow *aw, guint32 date,
                                 gboolean refresh) {
    if (!date_valid(date)) return;
    aw->cal_selected = date;
    aw->cal_focus = date;
    if (date / 10000u != aw->cal_year) {
        aw->cal_year = date / 10000u;
        calendar_sync_year_entry(aw);
    }
    gtk_widget_queue_draw(aw->calendar);
    if (refresh) update_calendar(aw);
}

/* Sposta il fuoco senza aprire il giorno (come le frecce di Windows). */
static void calendar_focus_date(ArchiveWindow *aw, guint32 date) {
    if (!date_valid(date)) return;
    aw->cal_focus = date;
    if (date / 10000u != aw->cal_year) {
        aw->cal_year = date / 10000u;
        calendar_sync_year_entry(aw);
    }
    gtk_widget_queue_draw(aw->calendar);
}

/* Cambia anno mantenendo il giorno del mese quando possibile. */
static void calendar_set_year(ArchiveWindow *aw, guint year) {
    guint32 focus;
    gint month, day, max_day;

    if (year < DATE_MIN_YEAR) year = DATE_MIN_YEAR;
    if (year > DATE_MAX_YEAR) year = DATE_MAX_YEAR;
    focus = date_valid(aw->cal_focus) ? aw->cal_focus : calendar_today_date();
    if (!date_valid(focus)) focus = (guint32)year * 10000u + 101u;
    month = (gint)(focus / 100u % 100u);
    day = (gint)(focus % 100u);
    max_day = date_days((gint)year, month);
    if (day > max_day) day = max_day;

    aw->cal_year = year;
    aw->cal_focus = (guint32)year * 10000u + (guint32)month * 100u +
                    (guint32)day;
    if (aw->cal_selected && aw->cal_selected / 10000u != year)
        aw->cal_selected = 0;
    calendar_sync_year_entry(aw);
    gtk_widget_queue_draw(aw->calendar);
    update_calendar(aw);
}

/* Valida e applica l'anno digitato. */
static void calendar_commit_year(ArchiveWindow *aw) {
    const char *text;
    gchar *end = NULL;
    gint64 value;

    if (!aw->calendar_year || aw->cal_updating) return;
    text = gtk_entry_get_text(GTK_ENTRY(aw->calendar_year));
    value = g_ascii_strtoll(text, &end, 10);
    while (end && g_ascii_isspace(*end)) ++end;
    if (!text[0] || (end && *end) ||
        value < DATE_MIN_YEAR || value > DATE_MAX_YEAR) {
        calendar_sync_year_entry(aw);
        return;
    }
    calendar_set_year(aw, (guint)value);
}


/* Sposta una data di N mesi, saturando il giorno. */
static guint32 calendar_shift_month(guint32 date, gint delta) {
    gint year = (gint)(date / 10000u);
    gint month = (gint)(date / 100u % 100u);
    gint day = (gint)(date % 100u);
    gint max_day;

    if (!date_valid(date)) return date;
    month += delta;
    while (month < 1) {
        month += 12;
        --year;
    }
    while (month > 12) {
        month -= 12;
        ++year;
    }
    if (year < DATE_MIN_YEAR || year > DATE_MAX_YEAR) return date;
    max_day = date_days(year, month);
    if (day > max_day) day = max_day;
    return (guint32)(year * 10000 + month * 100 + day);
}

/* Gestione tastiera della vista annuale. */
static gboolean on_calendar_key_press(GtkWidget *widget, GdkEventKey *event,
                                      gpointer data) {
    ArchiveWindow *aw = data;
    guint32 base = aw->cal_selected ? aw->cal_selected : aw->cal_focus;
    guint32 date = base;
    gboolean move = TRUE;

    (void)widget;
    if (!date_valid(base))
        base = aw->cal_year * 10000u + 101u;
    date = base;

    switch (event->keyval) {
    case GDK_KEY_Left:
        date = date_shift(base, -1);
        break;
    case GDK_KEY_Right:
        date = date_shift(base, 1);
        break;
    case GDK_KEY_Up:
        date = date_shift(base, -7);
        break;
    case GDK_KEY_Down:
        date = date_shift(base, 7);
        break;
    case GDK_KEY_Page_Up:
        date = calendar_shift_month(base, -1);
        break;
    case GDK_KEY_Page_Down:
        date = calendar_shift_month(base, 1);
        break;
    case GDK_KEY_Home:
        date = base / 100u * 100u + 1u;
        break;
    case GDK_KEY_End: {
        guint year = base / 10000u;
        guint month = base / 100u % 100u;

        date = year * 10000u + month * 100u +
               (guint32)date_days((gint)year, (gint)month);
        break;
    }
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_space:
        if (date_valid(base) &&
            g_hash_table_lookup(aw->day_counts, GUINT_TO_POINTER(base)))
            calendar_select_date(aw, base, TRUE);
        else
            calendar_focus_date(aw, base);
        return TRUE;
    case GDK_KEY_Escape:
        aw->cal_selected = 0;
        gtk_widget_queue_draw(aw->calendar);
        update_calendar(aw);
        return TRUE;
    default:
        move = FALSE;
        break;
    }
    if (!move) return FALSE;
    if (date_valid(date)) calendar_focus_date(aw, date);
    return TRUE;
}

/* Clic su un giorno della vista annuale. */
static gboolean on_calendar_button_press(GtkWidget *widget, GdkEventButton *event,
                                         gpointer data) {
    ArchiveWindow *aw = data;
    guint32 date;
    gint width, height;

    if (event->type != GDK_BUTTON_PRESS || event->button != 1) return FALSE;
    gtk_widget_grab_focus(widget);
    width = gtk_widget_get_allocated_width(widget);
    height = gtk_widget_get_allocated_height(widget);
    date = calendar_date_at(aw, width, height, (gint)event->x, (gint)event->y);
    if (date) {
        calendar_focus_date(aw, date);
        if (g_hash_table_lookup(aw->day_counts, GUINT_TO_POINTER(date)))
            calendar_select_date(aw, date, TRUE);
    }
    return TRUE;
}

/* Evidenzia il giorno sotto il puntatore. */
static gboolean on_calendar_motion(GtkWidget *widget, GdkEventMotion *event,
                                   gpointer data) {
    ArchiveWindow *aw = data;
    guint32 date;
    gint width, height;

    width = gtk_widget_get_allocated_width(widget);
    height = gtk_widget_get_allocated_height(widget);
    date = calendar_date_at(aw, width, height, (gint)event->x, (gint)event->y);
    if (date != aw->cal_hover) {
        aw->cal_hover = date;
        gtk_widget_queue_draw(widget);
    }
    return FALSE;
}

/* Rimuove l'evidenziazione quando il puntatore esce. */
static gboolean on_calendar_leave(GtkWidget *widget, GdkEventCrossing *event,
                                  gpointer data) {
    ArchiveWindow *aw = data;

    (void)widget;
    (void)event;
    if (aw->cal_hover) {
        aw->cal_hover = 0;
        gtk_widget_queue_draw(aw->calendar);
    }
    return FALSE;
}

/* Applica l'anno digitato. */
static void on_calendar_year_activate(GtkEntry *entry, gpointer data) {
    (void)entry;
    calendar_commit_year(data);
}

/* Applica l'anno quando il campo perde il focus. */
static gboolean on_calendar_year_focus_out(GtkWidget *widget, GdkEvent *event,
                                           gpointer data) {
    (void)widget;
    (void)event;
    calendar_commit_year(data);
    return FALSE;
}

/* Passo anno dai pulsanti precedente/successivo. */
static void on_calendar_year_step(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    gint step = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "step"));
    gint64 year = (gint64)aw->cal_year + step;

    if (year < DATE_MIN_YEAR) year = DATE_MIN_YEAR;
    if (year > DATE_MAX_YEAR) year = DATE_MAX_YEAR;
    calendar_set_year(aw, (guint)year);
}

/* Torna al giorno corrente. */
static void on_calendar_today_clicked(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    guint32 today = calendar_today_date();

    (void)button;
    if (!date_valid(today)) return;
    aw->cal_today = today;
    aw->cal_selected = 0;
    calendar_set_year(aw, today / 10000u);
    aw->cal_focus = today;
    gtk_widget_queue_draw(aw->calendar);
    gtk_widget_grab_focus(aw->calendar);
}

/* Marca sulla vista annuale ogni giorno che contiene almeno una nota. */
static void refresh_calendar_marks(ArchiveWindow *aw) {
    guint i;

    if (!aw || !aw->calendar) return;
    if (!aw->day_counts)
        aw->day_counts = g_hash_table_new(g_direct_hash, g_direct_equal);
    else
        g_hash_table_remove_all(aw->day_counts);
    if (aw->visible_notes) {
        for (i = 0; i < aw->visible_notes->len; ++i) {
            Note *note = g_ptr_array_index(aw->visible_notes, i);
            guint count;

            if (!note || !note->date || !date_valid(note->date)) continue;
            count = GPOINTER_TO_UINT(
                g_hash_table_lookup(aw->day_counts,
                                    GUINT_TO_POINTER(note->date)));
            g_hash_table_insert(aw->day_counts, GUINT_TO_POINTER(note->date),
                                GUINT_TO_POINTER(count + 1));
        }
    }
    gtk_widget_queue_draw(aw->calendar);
}

/* Aggiorna il calendario e le note del giorno. */
static void update_calendar(ArchiveWindow *aw) {
    guint year = 0;
    guint month = 0;
    guint day = 0;
    guint32 selected;
    guint total = 0;
    guint i;
    char date_text[16];
    char *label;
    const char *visible;

    if (!aw || !aw->calendar || !aw->calendar_list) return;

    refresh_calendar_marks(aw);
    selected = aw->cal_selected;
    if (!date_valid(selected)) {
        clear_listbox(aw->calendar_list);
        gtk_label_set_text(GTK_LABEL(aw->calendar_label), "No day selected");
        visible = gtk_stack_get_visible_child_name(GTK_STACK(aw->stack));
        if (visible && g_strcmp0(visible, "calendar") == 0)
            gtk_header_bar_set_subtitle(GTK_HEADER_BAR(aw->header),
                                        "No day selected");
        update_window_title(aw);
        return;
    }
    year = selected / 10000u;
    month = selected / 100u % 100u;
    day = selected % 100u;

    clear_listbox(aw->calendar_list);
    if (aw->visible_notes) {
        for (i = 0; i < aw->visible_notes->len; ++i) {
            Note *note = g_ptr_array_index(aw->visible_notes, i);

            if (!note || note->date != selected) continue;
            ++total;
            gtk_list_box_insert(GTK_LIST_BOX(aw->calendar_list),
                                build_note_row(aw, note), -1);
        }
    }

    g_snprintf(date_text, sizeof(date_text), "%04u-%02u-%02u",
               year, month, day);
    label = g_strdup_printf("%u note%s on %s", total,
                            total == 1 ? "" : "s", date_text);
    gtk_label_set_text(GTK_LABEL(aw->calendar_label), label);

    visible = gtk_stack_get_visible_child_name(GTK_STACK(aw->stack));
    if (visible && g_strcmp0(visible, "calendar") == 0)
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(aw->header), label);
    update_window_title(aw);
    g_free(label);
}

/* Apre la prima nota dei risultati. */
static void open_first_note(ArchiveWindow *aw) {
    if (aw->first_note && aw->open_cb)
        aw->open_cb(aw->first_note, aw->open_data);
}

/* Porta il focus sul flow box e seleziona la prima card. */
static void focus_flowbox(ArchiveWindow *aw) {
    GList *children;

    if (!aw->flow) return;
    gtk_widget_grab_focus(aw->flow);
    children = gtk_container_get_children(GTK_CONTAINER(aw->flow));
    if (children) {
        gtk_flow_box_select_child(GTK_FLOW_BOX(aw->flow),
                                  GTK_FLOW_BOX_CHILD(children->data));
        g_list_free(children);
    }
}

/* Pulisce ricerca, filtro data e selezione calendario. */
/* Svuota solo il testo di ricerca, come Esc su Windows. */
static void clear_search_text(ArchiveWindow *aw) {
    if (!aw) return;
    cancel_search_timeout(aw);
    g_signal_handlers_block_by_func(aw->search,
                                    G_CALLBACK(on_search_changed), aw);
    gtk_entry_set_text(GTK_ENTRY(aw->search), "");
    g_signal_handlers_unblock_by_func(aw->search,
                                      G_CALLBACK(on_search_changed), aw);
    repopulate_list(aw);
}

/* Porta il fuoco alla vista principale (lista o calendario). */
static void focus_main_view(ArchiveWindow *aw) {
    const char *visible = gtk_stack_get_visible_child_name(GTK_STACK(aw->stack));

    if (visible != NULL && g_strcmp0(visible, "calendar") == 0)
        gtk_widget_grab_focus(aw->calendar);
    else
        focus_flowbox(aw);
}

/* Gestisce i tasti nella casella di ricerca. */
static gboolean on_search_key_press(GtkWidget *widget, GdkEventKey *event,
                                    gpointer data) {
    ArchiveWindow *aw = data;

    (void)widget;
    if (event->keyval == GDK_KEY_Down) {
        focus_main_view(aw);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        const char *visible = gtk_stack_get_visible_child_name(
            GTK_STACK(aw->stack));

        if (visible != NULL && g_strcmp0(visible, "calendar") == 0)
            gtk_widget_grab_focus(aw->calendar);
        else
            open_first_note(aw);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Escape) {
        clear_search_text(aw);
        focus_main_view(aw);
        return TRUE;
    }
    return FALSE;
}

/* Gestisce le scorciatoie della finestra archivio. */
static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event,
                                    gpointer data) {
    ArchiveWindow *aw = data;
    gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;

    (void)widget;
    if (ctrl && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
        archive_window_focus_search(aw->window);
        return TRUE;
    }
    if (ctrl && (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_R)) {
        archive_window_refresh(aw->window);
        return TRUE;
    }
    if (ctrl && (event->keyval == GDK_KEY_a || event->keyval == GDK_KEY_A) &&
        !gtk_widget_has_focus(aw->search)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(aw->flow));
        GList *item;

        for (item = children; item != NULL; item = item->next)
            gtk_flow_box_select_child(GTK_FLOW_BOX(aw->flow),
                                      GTK_FLOW_BOX_CHILD(item->data));
        g_list_free(children);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Escape) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(aw->search));

        if (text != NULL && *text != '\0')
            clear_search_text(aw);
        focus_main_view(aw);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_F5) {
        archive_window_refresh(aw->window);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Delete &&
        !gtk_widget_has_focus(aw->search)) {
        GList *selected =
            gtk_flow_box_get_selected_children(GTK_FLOW_BOX(aw->flow));

        if (selected) {
            g_list_free(selected);
            delete_selected_notes(aw);
            return TRUE;
        }
    }
    if ((event->keyval == GDK_KEY_Return ||
         event->keyval == GDK_KEY_KP_Enter) &&
        gtk_widget_has_focus(aw->search)) {
        open_first_note(aw);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Down && gtk_widget_has_focus(aw->search)) {
        focus_flowbox(aw);
        return TRUE;
    }
    return FALSE;
}

/* Cambia la cartella archivio tramite selettore. */
static void on_open_folder(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    GtkWidget *dialog;
    char *path = NULL;

    (void)button;
    dialog = gtk_file_chooser_dialog_new(
        "Open archive folder", GTK_WINDOW(aw->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, NULL);
    if (aw->store && aw->store->archive_dir)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                            aw->store->archive_dir);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);

    if (!path) return;

    dialog = gtk_message_dialog_new(
        GTK_WINDOW(aw->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "Switch archive folder?");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "Move the existing notes into the new folder, or just open it?");
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Open here", GTK_RESPONSE_NO);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Move notes here",
                          GTK_RESPONSE_YES);
    {
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));

        gtk_widget_destroy(dialog);
        dialog = NULL;
        if (response == GTK_RESPONSE_YES || response == GTK_RESPONSE_NO) {
            gboolean move = response == GTK_RESPONSE_YES;

            if (aw->change_cb) aw->change_cb(path, move, aw->change_data);
        }
    }
    g_free(path);
}

/* Applica il filtro selezionato nell'albero delle date. */
static void on_date_tree_selection_changed(GtkTreeSelection *selection,
                                           gpointer data) {
    ArchiveWindow *aw = data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    guint value = 0;
    gint kind = 0;

    if (aw->updating_sidebar) return;
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter,
                           DATE_COL_VALUE, &value,
                           DATE_COL_KIND, &kind, -1);
    aw->date_filter = value;
    aw->date_kind = kind;
    repopulate_list(aw);
}

/* Applica il filtro selezionato nell'albero dei tag. */
static void on_tag_tree_selection_changed(GtkTreeSelection *selection,
                                          gpointer data) {
    ArchiveWindow *aw = data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    char *tag = NULL;

    if (aw->updating_sidebar) return;
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter, TAG_COL_VALUE, &tag, -1);
    g_free(aw->tag_filter);
    aw->tag_filter = (tag && *tag) ? g_strdup(tag) : NULL;
    g_free(tag);
    repopulate_list(aw);
}

/* Libera lo stato della finestra. */
static void archive_state_free(gpointer data) {
    ArchiveWindow *aw = data;

    if (!aw) return;
    cancel_search_timeout(aw);
    if (aw->flow_scroll_restore_id)
        g_source_remove(aw->flow_scroll_restore_id);
    if (aw->day_counts) g_hash_table_destroy(aw->day_counts);
    if (aw->visible_notes) g_ptr_array_free(aw->visible_notes, TRUE);
    if (aw->css) g_object_unref(aw->css);
    g_free(aw->tag_filter);
    g_free(aw);
}

/* Imposta il callback invocato prima di eliminare una nota. */
void archive_window_set_delete_callback(GtkWidget *window, ArchiveDeleteFunc cb,
                                        gpointer data) {
    ArchiveWindow *aw = get_state(window);

    if (aw == NULL) return;
    aw->delete_cb = cb;
    aw->delete_data = data;
}

/* Crea e inizializza la finestra archivio. */
GtkWidget *archive_window_new(Store *store) {
    ArchiveWindow *aw;
    GtkWidget *window;
    GtkWidget *header;
    GtkWidget *content;
    GtkWidget *toolbar;
    GtkWidget *tags_toggle;
    GtkWidget *date_toggle;
    GtkWidget *archive_button;
    GtkWidget *archive_box;
    GtkWidget *archive_icon;
    GtkWidget *archive_label;
    GtkWidget *calendar_button;
    GtkWidget *body;
    GtkWidget *sidebar;
    GtkWidget *right;
    GtkWidget *scroll;
    GtkWidget *calendar_page;
    GtkWidget *calendar_scroll;
    GtkWidget *calendar_nav;
    GtkWidget *date_scroll;
    GtkWidget *tag_scroll;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Notes Archive");
    gtk_window_set_default_size(GTK_WINDOW(window), 990, 660);
    gtk_style_context_add_class(gtk_widget_get_style_context(window),
                                "archive-window");

    aw = g_new0(ArchiveWindow, 1);
    aw->window = window;
    aw->store = store;
    aw->sidebar_tags = FALSE;
    aw->cal_today = calendar_today_date();
    aw->cal_year = date_valid(aw->cal_today)
                       ? aw->cal_today / 10000u
                       : (guint32)DATE_MIN_YEAR;
    aw->cal_focus = date_valid(aw->cal_today)
                        ? aw->cal_today
                        : aw->cal_year * 10000u + 101u;
    aw->day_counts = g_hash_table_new(g_direct_hash, g_direct_equal);
    aw->selection_anchor = -1;
    aw->css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(aw->css, ARCHIVE_CSS, -1, NULL);

    header = gtk_header_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(header),
                                "archive-header");
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Notes Archive");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    aw->header = header;
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(content),
                                "archive-content");
    gtk_container_add(GTK_CONTAINER(window), content);

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar),
                                "archive-toolbar");

    tags_toggle = gtk_toggle_button_new_with_label("Tags");
    aw->tags_button = tags_toggle;
    gtk_style_context_add_class(gtk_widget_get_style_context(tags_toggle),
                                "archive-tool-button");
    g_object_set_data(G_OBJECT(tags_toggle), "archive-tags",
                      GINT_TO_POINTER(1));
    set_accessible_label(tags_toggle, "Show tags");
    g_signal_connect(tags_toggle, "toggled",
                     G_CALLBACK(on_sidebar_toggled), aw);
    gtk_box_pack_start(GTK_BOX(toolbar), tags_toggle, FALSE, FALSE, 0);

    date_toggle = gtk_toggle_button_new_with_label("Date");
    aw->date_button = date_toggle;
    gtk_style_context_add_class(gtk_widget_get_style_context(date_toggle),
                                "archive-tool-button");
    g_object_set_data(G_OBJECT(date_toggle), "archive-tags",
                      GINT_TO_POINTER(0));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(date_toggle), TRUE);
    set_accessible_label(date_toggle, "Show dates");
    g_signal_connect(date_toggle, "toggled",
                     G_CALLBACK(on_sidebar_toggled), aw);
    gtk_box_pack_start(GTK_BOX(toolbar), date_toggle, FALSE, FALSE, 0);

    aw->search = gtk_search_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(aw->search), 511);
    gtk_entry_set_placeholder_text(GTK_ENTRY(aw->search),
                                   "Search notes and tags");
    gtk_widget_set_hexpand(aw->search, TRUE);
    gtk_widget_set_tooltip_text(aw->search, "Search text and tags");
    gtk_style_context_add_class(gtk_widget_get_style_context(aw->search),
                                "archive-search");
    gtk_box_pack_start(GTK_BOX(toolbar), aw->search, TRUE, TRUE, 0);

    archive_button = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(archive_button),
                                "archive-tool-button");
    archive_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    archive_icon = gtk_image_new_from_icon_name("folder-symbolic",
                                                GTK_ICON_SIZE_MENU);
    archive_label = gtk_label_new("Archive");
    gtk_box_pack_start(GTK_BOX(archive_box), archive_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(archive_box), archive_label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(archive_button), archive_box);
    set_accessible_label(archive_button, "Switch archive folder");
    g_signal_connect(archive_button, "clicked",
                     G_CALLBACK(on_open_folder), aw);
    gtk_box_pack_start(GTK_BOX(toolbar), archive_button, FALSE, FALSE, 0);

    calendar_button = gtk_toggle_button_new();
    aw->calendar_button = calendar_button;
    gtk_widget_set_size_request(calendar_button, 32, 32);
    gtk_style_context_add_class(gtk_widget_get_style_context(calendar_button),
                                "archive-icon-button");
    gtk_button_set_image(GTK_BUTTON(calendar_button),
                         gtk_image_new_from_icon_name(
                             "x-office-calendar-symbolic",
                             GTK_ICON_SIZE_MENU));
    set_accessible_label(calendar_button, "Show calendar");
    gtk_widget_set_tooltip_text(calendar_button, "Show calendar");
    g_signal_connect(calendar_button, "toggled",
                     G_CALLBACK(on_calendar_clicked), aw);
    gtk_box_pack_end(GTK_BOX(toolbar), calendar_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), toolbar, FALSE, FALSE, 0);

    body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(body, TRUE);

    sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    aw->sidebar = sidebar;
    gtk_style_context_add_class(gtk_widget_get_style_context(sidebar),
                                "archive-sidebar");
    gtk_widget_set_size_request(sidebar, 230, -1);
    gtk_box_pack_start(GTK_BOX(body), sidebar, FALSE, FALSE, 0);

    aw->date_model = gtk_tree_store_new(DATE_COL_COUNT,
                                        G_TYPE_STRING, G_TYPE_UINT,
                                        G_TYPE_INT, G_TYPE_UINT);
    aw->date_tree = gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(aw->date_model));
    g_object_unref(aw->date_model);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(aw->date_tree), FALSE);
    gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(aw->date_tree), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(aw->date_tree),
                                "archive-tree");
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
        "Dates", renderer, "text", DATE_COL_LABEL, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(aw->date_tree), column);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(aw->date_tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(on_date_tree_selection_changed), aw);

    date_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(date_scroll),
                                "archive-sidebar-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(date_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(
        GTK_SCROLLED_WINDOW(date_scroll), FALSE);
    gtk_widget_set_vexpand(date_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(date_scroll), aw->date_tree);

    aw->tag_model = gtk_tree_store_new(TAG_COL_N_COLUMNS,
                                       G_TYPE_STRING, G_TYPE_STRING,
                                       G_TYPE_STRING);
    aw->tag_tree = gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(aw->tag_model));
    g_object_unref(aw->tag_model);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(aw->tag_tree), FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(aw->tag_tree),
                                "archive-tree");
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
        "Tags", renderer, "text", TAG_COL_LABEL, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(aw->tag_tree), column);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    column = gtk_tree_view_column_new_with_attributes(
        "Count", renderer, "text", TAG_COL_COUNT, NULL);
    gtk_tree_view_column_set_alignment(column, 1.0f);
    gtk_tree_view_append_column(GTK_TREE_VIEW(aw->tag_tree), column);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(aw->tag_tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(on_tag_tree_selection_changed), aw);

    tag_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(tag_scroll),
                                "archive-sidebar-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tag_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(
        GTK_SCROLLED_WINDOW(tag_scroll), FALSE);
    gtk_widget_set_vexpand(tag_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(tag_scroll), aw->tag_tree);

    aw->sidebar_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(aw->sidebar_stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_add_named(GTK_STACK(aw->sidebar_stack), date_scroll, "dates");
    gtk_stack_add_named(GTK_STACK(aw->sidebar_stack), tag_scroll, "tags");
    gtk_stack_set_visible_child_name(GTK_STACK(aw->sidebar_stack), "dates");
    gtk_widget_set_vexpand(aw->sidebar_stack, TRUE);
    gtk_box_pack_start(GTK_BOX(sidebar), aw->sidebar_stack, TRUE, TRUE, 0);

    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(right),
                                "archive-content");
    gtk_widget_set_hexpand(right, TRUE);
    gtk_box_pack_start(GTK_BOX(body), right, TRUE, TRUE, 0);

    aw->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(aw->stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_widget_set_vexpand(aw->stack, TRUE);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(scroll),
                                "archive-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_overlay_scrolling(
        GTK_SCROLLED_WINDOW(scroll), FALSE);
    aw->flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(aw->flow),
                                    GTK_SELECTION_MULTIPLE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(aw->flow), FALSE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(aw->flow), 9);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(aw->flow), 9);
    gtk_widget_set_valign(aw->flow, GTK_ALIGN_START);
    gtk_widget_set_vexpand(aw->flow, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(aw->flow),
                                "archive-flow");
    gtk_widget_set_margin_start(aw->flow, 10);
    gtk_widget_set_margin_end(aw->flow, 10);
    gtk_widget_set_margin_top(aw->flow, 10);
    gtk_widget_set_margin_bottom(aw->flow, 10);
    g_signal_connect(aw->flow, "child-activated",
                     G_CALLBACK(on_flow_child_activated), aw);
    g_signal_connect(aw->flow, "selected-children-changed",
                     G_CALLBACK(on_flow_selection_changed), aw);
    g_signal_connect(aw->flow, "button-press-event",
                     G_CALLBACK(on_flow_button_press), aw);
    gtk_container_add(GTK_CONTAINER(scroll), aw->flow);
    aw->flow_scroll = scroll;
    gtk_stack_add_named(GTK_STACK(aw->stack), scroll, "list");

    calendar_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(calendar_page),
                                "archive-calendar-page");
    gtk_widget_set_margin_start(calendar_page, 8);
    gtk_widget_set_margin_end(calendar_page, 8);
    gtk_widget_set_margin_top(calendar_page, 8);
    gtk_widget_set_margin_bottom(calendar_page, 8);

    calendar_nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(calendar_nav, GTK_ALIGN_CENTER);
    aw->calendar_prev = gtk_button_new_with_label("\342\200\271");
    gtk_style_context_add_class(gtk_widget_get_style_context(aw->calendar_prev),
                                "archive-tool-button");
    set_accessible_label(aw->calendar_prev, "Previous year");
    g_object_set_data(G_OBJECT(aw->calendar_prev), "step",
                      GINT_TO_POINTER(-1));
    g_signal_connect(aw->calendar_prev, "clicked",
                     G_CALLBACK(on_calendar_year_step), aw);
    gtk_box_pack_start(GTK_BOX(calendar_nav), aw->calendar_prev,
                       FALSE, FALSE, 0);

    aw->calendar_year = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(aw->calendar_year), 4);
    gtk_entry_set_width_chars(GTK_ENTRY(aw->calendar_year), 6);
    gtk_entry_set_alignment(GTK_ENTRY(aw->calendar_year), 0.5f);
    set_accessible_label(aw->calendar_year, "Calendar year");
    gtk_box_pack_start(GTK_BOX(calendar_nav), aw->calendar_year,
                       FALSE, FALSE, 0);

    aw->calendar_next = gtk_button_new_with_label("\342\200\272");
    gtk_style_context_add_class(gtk_widget_get_style_context(aw->calendar_next),
                                "archive-tool-button");
    set_accessible_label(aw->calendar_next, "Next year");
    g_object_set_data(G_OBJECT(aw->calendar_next), "step",
                      GINT_TO_POINTER(1));
    g_signal_connect(aw->calendar_next, "clicked",
                     G_CALLBACK(on_calendar_year_step), aw);
    gtk_box_pack_start(GTK_BOX(calendar_nav), aw->calendar_next,
                       FALSE, FALSE, 0);

    aw->calendar_today_button = gtk_button_new_with_label("Today");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(aw->calendar_today_button),
        "archive-tool-button");
    set_accessible_label(aw->calendar_today_button, "Go to today");
    g_signal_connect(aw->calendar_today_button, "clicked",
                     G_CALLBACK(on_calendar_today_clicked), aw);
    gtk_box_pack_start(GTK_BOX(calendar_nav), aw->calendar_today_button,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(calendar_page), calendar_nav,
                       FALSE, FALSE, 0);

    aw->calendar = gtk_drawing_area_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(aw->calendar),
                                "archive-calendar");
    gtk_widget_set_size_request(aw->calendar, 640, 360);
    gtk_widget_set_vexpand(aw->calendar, TRUE);
    gtk_widget_set_can_focus(aw->calendar, TRUE);
    gtk_widget_add_events(aw->calendar,
                          GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK |
                          GDK_LEAVE_NOTIFY_MASK);
    set_accessible_label(aw->calendar, "Year calendar");
    g_signal_connect(aw->calendar, "draw", G_CALLBACK(on_calendar_draw), aw);
    g_signal_connect(aw->calendar, "button-press-event",
                     G_CALLBACK(on_calendar_button_press), aw);
    g_signal_connect(aw->calendar, "motion-notify-event",
                     G_CALLBACK(on_calendar_motion), aw);
    g_signal_connect(aw->calendar, "leave-notify-event",
                     G_CALLBACK(on_calendar_leave), aw);
    g_signal_connect(aw->calendar, "key-press-event",
                     G_CALLBACK(on_calendar_key_press), aw);
    gtk_box_pack_start(GTK_BOX(calendar_page), aw->calendar, TRUE, TRUE, 0);
    calendar_sync_year_entry(aw);

    aw->calendar_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(aw->calendar_label), 0.0f);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(aw->calendar_label),
        "archive-calendar-label");
    gtk_box_pack_start(GTK_BOX(calendar_page), aw->calendar_label,
                       FALSE, FALSE, 0);

    calendar_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(calendar_scroll),
                                "archive-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(calendar_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(
        GTK_SCROLLED_WINDOW(calendar_scroll), FALSE);
    gtk_widget_set_vexpand(calendar_scroll, TRUE);
    aw->calendar_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(aw->calendar_list),
                                    GTK_SELECTION_SINGLE);
    g_signal_connect(aw->calendar_list, "row-activated",
                     G_CALLBACK(on_row_activated), aw);
    g_signal_connect(aw->calendar_list, "key-press-event",
                     G_CALLBACK(on_day_list_key_press), aw);
    g_signal_connect(aw->calendar_list, "button-press-event",
                     G_CALLBACK(on_list_button_press), aw);
    gtk_container_add(GTK_CONTAINER(calendar_scroll), aw->calendar_list);
    gtk_box_pack_start(GTK_BOX(calendar_page), calendar_scroll, TRUE, TRUE, 0);
    gtk_stack_add_named(GTK_STACK(aw->stack), calendar_page, "calendar");

    gtk_box_pack_start(GTK_BOX(right), aw->stack, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), body, TRUE, TRUE, 0);

    g_object_set_data(G_OBJECT(window), ARCHIVE_STORE_KEY, store);
    g_object_set_data_full(G_OBJECT(window), ARCHIVE_STATE_KEY, aw,
                           archive_state_free);

    g_signal_connect(aw->search, "search-changed",
                     G_CALLBACK(on_search_changed), aw);
    g_signal_connect(aw->search, "key-press-event",
                     G_CALLBACK(on_search_key_press), aw);
    g_signal_connect(aw->calendar_year, "activate",
                     G_CALLBACK(on_calendar_year_activate), aw);
    g_signal_connect(aw->calendar_year, "focus-out-event",
                     G_CALLBACK(on_calendar_year_focus_out), aw);
    g_signal_connect(window, "key-press-event",
                     G_CALLBACK(on_window_key_press), aw);

    apply_card_provider(header, aw->css);
    apply_card_provider(content, aw->css);
    populate_sidebar(aw);
    repopulate_list(aw);
    update_calendar(aw);
    update_sidebar_buttons(aw);
    gtk_widget_show_all(window);
    return window;
}

/* Ricarica tag, alberi, lista e calendario. */
void archive_window_refresh(GtkWidget *window) {
    ArchiveWindow *aw = get_state(window);

    if (!aw) return;
    cancel_search_timeout(aw);
    capture_flow_scroll(aw);
    populate_sidebar(aw);
    repopulate_list(aw);
    update_calendar(aw);
    restore_flow_scroll_later(aw);
}

/* Imposta il callback di apertura nota. */
void archive_window_set_open_callback(GtkWidget *window, ArchiveOpenFunc cb,
                                      gpointer data) {
    ArchiveWindow *aw = get_state(window);

    if (!aw) return;
    aw->open_cb = cb;
    aw->open_data = data;
}

/* Porta il focus sulla casella di ricerca. */
void archive_window_focus_search(GtkWidget *window) {
    ArchiveWindow *aw = get_state(window);

    if (!aw || !aw->search) return;
    gtk_widget_grab_focus(aw->search);
    gtk_editable_select_region(GTK_EDITABLE(aw->search), 0, -1);
}

/* Imposta il callback di cambio cartella archivio. */
void archive_window_set_change_callback(GtkWidget *window,
                                        ArchiveChangeFunc cb, gpointer data) {
    ArchiveWindow *aw = get_state(window);

    if (!aw) return;
    aw->change_cb = cb;
    aw->change_data = data;
}

