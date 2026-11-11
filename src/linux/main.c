/* Punto di ingresso Jotilla: gestisce finestre note, archivio e ciclo GTK. */
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-unix.h>
#include <cairo.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include "jotilla.h"
#include "note_window.h"
#include "archive_window.h"
#include "platform.h"

/*
 * Le due funzioni seguenti sono dichiarate negli header ma implementate in
 * moduli in fase di modifica. La dichiarazione weak mantiene il build pulito
 * e si aggancia automaticamente alle definizioni forti quando disponibili.
 */
#if defined(__GNUC__)
extern void note_window_save(GtkWidget *window) __attribute__((weak));
extern gboolean note_window_flush(GtkWidget *window) __attribute__((weak));
extern void archive_window_set_change_callback(GtkWidget *window,
                                               ArchiveChangeFunc cb,
                                               gpointer data)
    __attribute__((weak));
#endif

typedef struct {
    Store *store;
    GtkWidget *archive;
    char *archive_dir;
    char *config_dir;
    int window_count;
    GPtrArray *note_windows;
    GtkStatusIcon *tray_icon;
    GtkWidget *tray_menu;
    GtkWidget *tray_startup_item;
    guint tray_click_timer;
    int tray_clicks;
    char *last_note;
    gboolean last_note_dirty;
    gboolean restoring;
    gboolean preserving_open;
    GApplication *gapp;
} AppCtx;

typedef struct {
    AppCtx *ctx;
    Note *note;
} NoteRef;

static void open_note(AppCtx *ctx, Note *note);
static void on_new_note(gpointer data);
static void on_show_archive(gpointer data);
static void on_archive_destroy(GtkWidget *widget, gpointer data);

/* Mostra un errore all'utente (equivalente delle MessageBox Windows). */
static void report_ui_error(const char *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
        "%s", message != NULL ? message : "Unknown error");

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Carica l'identita' dell'ultima nota usata da questo archivio. */
static void load_last_note(AppCtx *ctx)
{    char *id;

    if (ctx == NULL)
        return;
    g_free(ctx->last_note);
    ctx->last_note = NULL;
    ctx->last_note_dirty = FALSE;
    if (ctx->store == NULL)
        return;
    id = store_app_get_string(ctx->store, "last_note");
    if (id != NULL && note_id_supported(id) &&
        strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") ==
            strlen(id)) {
        ctx->last_note = id;
    } else {
        g_free(id);
    }
}

/* Ricorda la nota attiva senza trattenere puntatori a note liberate. */
static void remember_note(AppCtx *ctx, Note *note)
{    if (ctx == NULL || note == NULL || note->id == NULL || ctx->restoring)
        return;
    if (g_strcmp0(ctx->last_note, note->id) == 0)
        return;
    g_free(ctx->last_note);
    ctx->last_note = g_strdup(note->id);
    ctx->last_note_dirty = TRUE;
}

/* Persiste l'ultima nota nella sezione [app] dell'archivio. */
static gboolean save_last_note(AppCtx *ctx)
{    if (ctx == NULL || ctx->store == NULL || !ctx->last_note_dirty)
        return TRUE;
    if (!store_app_set_string(ctx->store, "last_note",
                              ctx->last_note != NULL ? ctx->last_note : ""))
        return FALSE;
    ctx->last_note_dirty = FALSE;
    return TRUE;
}

/* Callback di focus della nota. */
static void on_note_focused(Note *note, gpointer data)
{    remember_note(data, note);
}

/* Salva tutte le finestre nota aperte (usato alla chiusura). */
static gboolean save_open_notes(AppCtx *ctx)
{    guint i;
    gboolean ok = TRUE;

    if (ctx == NULL || ctx->note_windows == NULL)
        return TRUE;

    for (i = 0; i < ctx->note_windows->len; ++i) {
        GtkWidget *window = g_ptr_array_index(ctx->note_windows, i);

        if (window == NULL)
            continue;
        if (note_window_flush != NULL)
            ok = note_window_flush(window) && ok;
        else if (note_window_save != NULL)
            note_window_save(window);
    }
    return save_last_note(ctx) && ok;
}

/* Callback GApplication: mostra l'archivio o crea la prima nota. */
static void on_app_activate(GApplication *app, gpointer data)
{    AppCtx *ctx = data;

    (void)app;
    if (ctx == NULL || ctx->store == NULL)
        return;

    on_new_note(ctx);
}

/* Aggiorna l'archivio quando una nota cambia. */
static void on_note_changed(Note *note, gpointer data)
{    AppCtx *ctx = data;
    (void)note;
    if (ctx->archive != NULL)
        archive_window_refresh(ctx->archive);
}

/* Crea una nuova nota e la apre. */
static void on_new_note(gpointer data)
{    AppCtx *ctx = data;
    Note *note = store_new(ctx->store, "");
    if (note != NULL)
        open_note(ctx, note);
}

/* Nuova nota dall'hotkey globale: usa il puntatore quando disponibile. */
static void on_hotkey_new_note(gpointer data)
{    AppCtx *ctx = data;
    Note *note;
    gint x = -1, y = -1;

    if (ctx == NULL || ctx->store == NULL)
        return;
    if (!platform_is_wayland()) {
        GdkDisplay *display = gdk_display_get_default();
        GdkSeat *seat = display ? gdk_display_get_default_seat(display) : NULL;
        GdkDevice *pointer = seat ? gdk_seat_get_pointer(seat) : NULL;

        if (pointer != NULL)
            (void)gdk_device_get_position(pointer, NULL, &x, &y);
    }
    note = store_new(ctx->store, "");
    if (note == NULL)
        return;
    if (x >= 0) {
        note->x = x;
        note->y = y;
    }
    open_note(ctx, note);
}

/* Chiude una finestra nota prima che l'archivio ne elimini i file. */
static void on_archive_delete(const char *id, gpointer data)
{    AppCtx *ctx = data;
    guint i;

    if (ctx == NULL || ctx->note_windows == NULL || id == NULL)
        return;
    for (i = 0; i < ctx->note_windows->len; ++i) {
        GtkWidget *window = g_ptr_array_index(ctx->note_windows, i);
        Note *note = window != NULL ? note_window_get_note(window) : NULL;

        if (note != NULL && g_strcmp0(note->id, id) == 0) {
            g_object_set_data(G_OBJECT(window), "deleted", GINT_TO_POINTER(1));
            gtk_widget_destroy(window);
            break;
        }
    }
}

/* Apre una nota selezionata dall'archivio. */
static void on_archive_open(Note *note, gpointer data)
{    open_note(data, note);
}

/* Sposta i file delle note e i metadati tra due directory. */
static gboolean move_note_files(const char *from_dir, const char *to_dir,
                                GError **err)
{    static const char *extensions[] = { ".txt", ".rtf" };
    GDir *dir;
    const char *name;
    gboolean ok = TRUE;

    dir = g_dir_open(from_dir, 0, err);
    if (dir == NULL)
        return FALSE;

    while (ok && (name = g_dir_read_name(dir)) != NULL) {
        char *src;
        char *dst;
        gboolean matches = g_ascii_strcasecmp(name, "notes.ini") == 0;
        guint i;

        for (i = 0; i < G_N_ELEMENTS(extensions); ++i) {
            size_t length = strlen(name);

            if (length >= strlen(extensions[i]) &&
                g_ascii_strcasecmp(name + length - strlen(extensions[i]),
                                   extensions[i]) == 0) {
                matches = TRUE;
                break;
            }
        }
        if (!matches)
            continue;

        src = g_build_filename(from_dir, name, NULL);
        dst = g_build_filename(to_dir, name, NULL);

        if (g_rename(src, dst) != 0) {
            /* Fallback per rename tra filesystem diversi, con verifica byte. */
            char *contents = NULL;
            gsize length = 0;

            if (!g_file_get_contents(src, &contents, &length, err)) {
                ok = FALSE;
            } else if (!g_file_set_contents(dst, contents, (gssize)length,
                                            err)) {
                g_free(contents);
                ok = FALSE;
            } else {
                char *check = NULL;
                gsize check_length = 0;
                gboolean same = g_file_get_contents(dst, &check,
                                                    &check_length, NULL) &&
                                check_length == length &&
                                memcmp(check, contents, length) == 0;

                g_free(check);
                g_free(contents);
                if (!same) {
                    (void)g_remove(dst);
                    g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_IO,
                                "The copied file '%s' did not verify", dst);
                    ok = FALSE;
                } else if (g_remove(src) != 0 && errno != ENOENT) {
                    g_set_error(err, G_FILE_ERROR,
                                g_file_error_from_errno(errno),
                                "Failed to remove '%s'", src);
                    ok = FALSE;
                }
            }
        }
        g_free(src);
        g_free(dst);
    }
    g_dir_close(dir);
    return ok;
}

/* True se la directory non esiste o e' vuota (il lock file non conta). */
static gboolean archive_dir_empty(const char *path)
{
    GDir *dir;
    const char *name;
    gboolean empty = TRUE;

    dir = g_dir_open(path, 0, NULL);
    if (dir == NULL)
        return TRUE;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (g_strcmp0(name, ".jotila.lock") != 0) {
            empty = FALSE;
            break;
        }
    }
    g_dir_close(dir);
    return empty;
}

/* Riapre store e archivio quando cambia il percorso dell'archivio. */
static void on_archive_change(const char *path, gboolean move, gpointer data)
{    AppCtx *ctx = data;
    GError *err = NULL;
    Store *new_store;

    if (ctx == NULL || path == NULL || *path == '\0')
        return;
    if (!save_open_notes(ctx)) {
        g_warning("Could not save all notes; the archive was not changed");
        return;
    }

    if (move) {
        char *old_path = ctx->store != NULL && ctx->store->archive_dir != NULL
                             ? g_canonicalize_filename(ctx->store->archive_dir, NULL)
                             : NULL;
        char *new_path = g_canonicalize_filename(path, NULL);
        size_t old_length = old_path != NULL ? strlen(old_path) : 0;
        size_t new_length = strlen(new_path);
        gboolean nested = old_path != NULL &&
            ((g_str_has_prefix(new_path, old_path) && new_path[old_length] == '/') ||
             (g_str_has_prefix(old_path, new_path) && old_path[new_length] == '/'));

        if (old_path != NULL && g_strcmp0(old_path, new_path) == 0) {
            g_free(old_path);
            g_free(new_path);
            return;
        }
        g_free(old_path);
        g_free(new_path);
        if (nested) {
            report_ui_error("The destination cannot contain or be inside the current archive.");
            return;
        }
        if (ctx->store != NULL && ctx->store->archive_dir != NULL &&
            g_strcmp0(ctx->store->archive_dir, path) != 0 &&
            g_file_test(path, G_FILE_TEST_IS_DIR) && !archive_dir_empty(path)) {
            report_ui_error("To move the archive, choose an empty folder.");
            return;
        }
        /* Salva e chiude le note esistenti prima di spostare i file. */
        ctx->preserving_open = TRUE;
        if (ctx->note_windows != NULL) {
            while (ctx->note_windows->len > 0) {
                GtkWidget *window = g_ptr_array_index(
                    ctx->note_windows, ctx->note_windows->len - 1);
                if (note_window_save != NULL)
                    note_window_save(window);
                gtk_widget_destroy(window);
            }
        }

        if (g_mkdir_with_parents(path, 0700) != 0) {
            g_warning("Could not create archive directory '%s'", path);
            return;
        }

        if (ctx->store != NULL && ctx->store->archive_dir != NULL &&
            g_strcmp0(ctx->store->archive_dir, path) != 0) {
            if (!move_note_files(ctx->store->archive_dir, path, &err)) {
                char *message = g_strdup_printf(
                    "The archive could not be moved: %s",
                    err != NULL ? err->message : "unknown error");

                report_ui_error(message);
                g_free(message);
                g_clear_error(&err);
                return;
            }
        }
    }

    new_store = store_open(path, ctx->config_dir, &err);
    if (new_store == NULL) {
        char *message = g_strdup_printf(
            "The archive could not be opened: %s",
            err != NULL ? err->message : "unknown error");

        report_ui_error(message);
        g_free(message);
        g_clear_error(&err);
        return;
    }

    /* Salva e chiude le note esistenti prima di liberare il vecchio store. */
    ctx->preserving_open = TRUE;
    if (ctx->note_windows != NULL) {
        while (ctx->note_windows->len > 0) {
            GtkWidget *window = g_ptr_array_index(ctx->note_windows,
                                                  ctx->note_windows->len - 1);
            if (note_window_save != NULL)
                note_window_save(window);
            gtk_widget_destroy(window);
        }
    }

    if (ctx->archive != NULL)
        gtk_widget_destroy(ctx->archive);

    store_free(ctx->store);
    ctx->store = new_store;
    store_set_archive_path(ctx->store, path);
    ctx->preserving_open = FALSE;
    load_last_note(ctx);

    g_free(ctx->archive_dir);
    ctx->archive_dir = g_strdup(path);

    ctx->archive = archive_window_new(ctx->store);
    archive_window_set_open_callback(ctx->archive, on_archive_open, ctx);
    archive_window_set_delete_callback(ctx->archive, on_archive_delete, ctx);
    if (archive_window_set_change_callback != NULL)
        archive_window_set_change_callback(ctx->archive, on_archive_change, ctx);
    g_signal_connect(ctx->archive, "destroy",
                     G_CALLBACK(on_archive_destroy), ctx);
    ctx->window_count++;
    gtk_widget_show_all(ctx->archive);
    gtk_window_present(GTK_WINDOW(ctx->archive));

    ctx->restoring = TRUE;
    if (ctx->store->notes->len == 0) {
        on_new_note(ctx);
    } else {
        guint i;

        for (i = 0; i < ctx->store->notes->len; ++i) {
            Note *note = g_ptr_array_index(ctx->store->notes, i);

            if (note->open)
                open_note(ctx, note);
        }
    }
    ctx->restoring = FALSE;
    if (ctx->last_note == NULL && ctx->store->notes->len > 0)
        remember_note(ctx, g_ptr_array_index(ctx->store->notes, 0));
}

/* Mostra o crea la finestra archivio. */
static void on_show_archive(gpointer data)
{    AppCtx *ctx = data;
    if (ctx->archive == NULL) {
        ctx->archive = archive_window_new(ctx->store);
        archive_window_set_open_callback(ctx->archive, on_archive_open, ctx);
    archive_window_set_delete_callback(ctx->archive, on_archive_delete, ctx);
        if (archive_window_set_change_callback != NULL)
            archive_window_set_change_callback(ctx->archive, on_archive_change,
                                               ctx);
        g_signal_connect(ctx->archive, "destroy",
                         G_CALLBACK(on_archive_destroy), ctx);
        ctx->window_count++;
    }
    gtk_widget_show_all(ctx->archive);
    gtk_window_present(GTK_WINDOW(ctx->archive));
}

/* Richiama l'ultima nota usata; se manca apre l'archivio. */
static void on_recall_note(gpointer data)
{    AppCtx *ctx = data;
    Note *note = NULL;
    guint i;

    if (ctx == NULL || ctx->store == NULL)
        return;
    if (ctx->last_note != NULL)
        note = store_get(ctx->store, ctx->last_note);
    if (note == NULL) {
        on_show_archive(ctx);
        return;
    }
    open_note(ctx, note);
    for (i = 0; i < ctx->note_windows->len; ++i) {
        GtkWidget *window = g_ptr_array_index(ctx->note_windows, i);
        Note *current = window != NULL ? note_window_get_note(window) : NULL;

        if (current == note) {
            note_window_expand(window);
            gtk_window_present(GTK_WINDOW(window));
            break;
        }
    }
    remember_note(ctx, note);
}

/* Esce dall'app se non restano finestre. */
static gboolean maybe_quit_idle(gpointer data)
{    AppCtx *ctx = data;

    if (ctx == NULL)
        return G_SOURCE_REMOVE;

    if (ctx->window_count <= 0) {
        /* Come Windows/macOS resta nel tray/menu bar quando e' disponibile. */
        if (ctx->tray_icon != NULL && gtk_status_icon_is_embedded(ctx->tray_icon))
            return G_SOURCE_REMOVE;
        gtk_main_quit();
    }
    return G_SOURCE_REMOVE;
}

/* Salva la nota e aggiorna il contatore finestre alla chiusura. */
static void on_note_destroy(GtkWidget *widget, gpointer data)
{    NoteRef *ref = data;

    if (ref != NULL) {
        if (ref->ctx->note_windows != NULL)
            g_ptr_array_remove(ref->ctx->note_windows, widget);
        if (ref->note != NULL &&
            !GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "deleted")) &&
            !ref->ctx->preserving_open) {
            GError *err = NULL;
            ref->note->open = FALSE;
            if (!store_save(ref->ctx->store, ref->note, &err)) {
                if (err != NULL) {
                    g_warning("Could not save note: %s", err->message);
                    g_error_free(err);
                }
            }
        }
        {
            AppCtx *ctx = ref->ctx;
            if (ctx->window_count > 0)
                ctx->window_count--;
            g_free(ref);
            g_idle_add(maybe_quit_idle, ctx);
        }
    }
}

/* Resetta l'archivio e aggiorna il contatore finestre. */
static void on_archive_destroy(GtkWidget *widget, gpointer data)
{    AppCtx *ctx = data;
    (void)widget;
    if (ctx != NULL) {
        ctx->archive = NULL;
        if (ctx->window_count > 0)
            ctx->window_count--;
    }
    g_idle_add(maybe_quit_idle, ctx);
}

/* Apre una nota in nuova finestra, salvandola e collegando callback. */
static void open_note(AppCtx *ctx, Note *note)
{    GtkWidget *window;
    NoteRef *ref;
    GError *err = NULL;

    if (ctx == NULL || note == NULL)
        return;

    /* Riusa la finestra esistente: due finestre sulla stessa nota
       condividerebbero il Note e diventerebbero use-after-free. */
    if (ctx->note_windows != NULL) {
        guint i;

        for (i = 0; i < ctx->note_windows->len; ++i) {
            GtkWidget *existing = g_ptr_array_index(ctx->note_windows, i);
            Note *current = existing != NULL ? note_window_get_note(existing) : NULL;

            if (current == note ||
                (current != NULL && g_strcmp0(current->id, note->id) == 0)) {
                remember_note(ctx, note);
                gtk_window_present(GTK_WINDOW(existing));
                return;
            }
        }
    }

    note->open = TRUE;
    if (!store_save(ctx->store, note, &err)) {
        if (err != NULL) {
            g_warning("Could not save note: %s", err->message);
            g_error_free(err);
        }
    }

    window = note_window_new(ctx->store, note);
    if (window == NULL)
        return;

    note_window_set_changed_callback(window, on_note_changed, ctx);
    note_window_set_focus_callback(window, on_note_focused, ctx);
    note_window_set_new_callback(window, on_new_note, ctx);
    note_window_set_archive_callback(window, on_show_archive, ctx);

    ref = g_new0(NoteRef, 1);
    ref->ctx = ctx;
    ref->note = note;
    g_signal_connect(window, "destroy", G_CALLBACK(on_note_destroy), ref);

    if (ctx->note_windows == NULL)
        ctx->note_windows = g_ptr_array_new();
    g_ptr_array_add(ctx->note_windows, window);

    ctx->window_count++;
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    remember_note(ctx, note);
}

/* Forza l'uscita dal ciclo GTK (smoke test). */
static gboolean quit_loop(gpointer data)
{    (void)data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

/* ------------------------------- Tray -------------------------------- */

/* Disegna l'icona del tray: quadrato giallo arrotondato con punto scuro. */
static GdkPixbuf *make_tray_pixbuf(void)
{    const int size = 22;
    cairo_surface_t *surface;
    cairo_t *cr;
    GdkPixbuf *pixbuf;

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    cr = cairo_create(surface);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    cairo_new_sub_path(cr);
    cairo_arc(cr, 16.0, 6.0, 4.0, -G_PI / 2.0, 0.0);
    cairo_arc(cr, 16.0, 16.0, 4.0, 0.0, G_PI / 2.0);
    cairo_arc(cr, 6.0, 16.0, 4.0, G_PI / 2.0, G_PI);
    cairo_arc(cr, 6.0, 6.0, 4.0, G_PI, 3.0 * G_PI / 2.0);
    cairo_close_path(cr);

    cairo_set_source_rgb(cr, 0.98, 0.85, 0.24);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.16, 0.13, 0.07);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    cairo_arc(cr, 11.0, 11.0, 2.6, 0.0, 2.0 * G_PI);
    cairo_set_source_rgb(cr, 0.16, 0.13, 0.07);
    cairo_fill(cr);

    cairo_destroy(cr);
    cairo_surface_flush(surface);
    pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, size, size);
    cairo_surface_destroy(surface);
    return pixbuf;
}

/* Timer del click singolo: crea una nota solo se non arriva un doppio. */
static gboolean tray_click_timeout(gpointer data)
{    AppCtx *ctx = data;

    ctx->tray_click_timer = 0;
    if (ctx->tray_clicks == 1)
        on_recall_note(ctx);
    ctx->tray_clicks = 0;
    return G_SOURCE_REMOVE;
}

/* Click sinistro sul tray: singolo = ultima nota, doppio = archivio. */
static void on_tray_activate(GtkStatusIcon *icon, gpointer data)
{    AppCtx *ctx = data;

    (void)icon;
    ctx->tray_clicks++;

    if (ctx->tray_clicks >= 2) {
        if (ctx->tray_click_timer != 0) {
            g_source_remove(ctx->tray_click_timer);
            ctx->tray_click_timer = 0;
        }
        ctx->tray_clicks = 0;
        on_show_archive(ctx);
    } else if (ctx->tray_click_timer == 0) {
        ctx->tray_click_timer = g_timeout_add(250, tray_click_timeout, ctx);
    }
}

/* Voce menu "Browse notes". */
static void on_tray_browse(GtkMenuItem *item, gpointer data)
{    (void)item;
    on_show_archive(data);
}

/* Voce menu "New note". */
static void on_tray_new(GtkMenuItem *item, gpointer data)
{    (void)item;
    on_new_note(data);
}

/* Voce menu "Exit": salva le note e termina il ciclo. */
static void on_tray_exit(GtkMenuItem *item, gpointer data)
{    AppCtx *ctx = data;

    (void)item;
    if (!save_open_notes(ctx)) {
        GtkWidget *dialog = gtk_message_dialog_new(
            NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Some notes could not be saved. Check that the archive is writable.");

        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    gtk_main_quit();
}

/* Voce menu "Start at login". */
static void on_tray_startup_toggled(GtkCheckMenuItem *item, gpointer data)
{    AppCtx *ctx = data;
    gboolean enabled = gtk_check_menu_item_get_active(item);

    if (!platform_autostart_set(enabled)) {
        g_warning("Could not %s the autostart entry.",
                  enabled ? "create" : "remove");
        g_signal_handlers_block_by_func(item,
                                        G_CALLBACK(on_tray_startup_toggled),
                                        ctx);
        gtk_check_menu_item_set_active(item, !enabled);
        g_signal_handlers_unblock_by_func(item,
                                          G_CALLBACK(on_tray_startup_toggled),
                                          ctx);
    }
}

/* Sincronizza il check "Start at login" con lo stato su disco. */
static void update_tray_startup_item(AppCtx *ctx)
{    gboolean enabled;

    if (ctx->tray_startup_item == NULL)
        return;

    enabled = platform_autostart_enabled();
    g_signal_handlers_block_by_func(ctx->tray_startup_item,
                                    G_CALLBACK(on_tray_startup_toggled), ctx);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ctx->tray_startup_item),
                                   enabled);
    g_signal_handlers_unblock_by_func(ctx->tray_startup_item,
                                      G_CALLBACK(on_tray_startup_toggled), ctx);
}

/* Apre il menu contestuale del tray. */
static void on_tray_popup(GtkStatusIcon *icon, guint button,
                          guint activate_time, gpointer data)
{    AppCtx *ctx = data;

    if (ctx->tray_menu == NULL)
        return;

    update_tray_startup_item(ctx);
    gtk_widget_show_all(ctx->tray_menu);
    gtk_menu_popup(GTK_MENU(ctx->tray_menu), NULL, NULL,
                   gtk_status_icon_position_menu, icon, button, activate_time);
}

/* Crea icona e menu del tray (solo X11/XWayland). */
static void setup_tray(AppCtx *ctx)
{
#if defined(GDK_WINDOWING_X11)
    GdkPixbuf *pixbuf;
    GtkWidget *item;

    if (platform_is_wayland()) {
        g_warning("System tray is only available on X11/XWayland; "
                  "skipping the tray icon.");
        return;
    }

    ctx->tray_menu = gtk_menu_new();

    item = gtk_menu_item_new_with_label("Browse notes");
    g_signal_connect(item, "activate", G_CALLBACK(on_tray_browse), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(ctx->tray_menu), item);

    item = gtk_menu_item_new_with_label("New note");
    g_signal_connect(item, "activate", G_CALLBACK(on_tray_new), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(ctx->tray_menu), item);

    gtk_menu_shell_append(GTK_MENU_SHELL(ctx->tray_menu),
                          gtk_separator_menu_item_new());

    ctx->tray_startup_item = gtk_check_menu_item_new_with_label("Start at login");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ctx->tray_startup_item),
                                   platform_autostart_enabled());
    g_signal_connect(ctx->tray_startup_item, "toggled",
                     G_CALLBACK(on_tray_startup_toggled), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(ctx->tray_menu),
                          ctx->tray_startup_item);

    item = gtk_menu_item_new_with_label("Exit");
    g_signal_connect(item, "activate", G_CALLBACK(on_tray_exit), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(ctx->tray_menu), item);

    gtk_widget_show_all(ctx->tray_menu);

    pixbuf = make_tray_pixbuf();
    if (pixbuf == NULL) {
        g_warning("Could not create the tray icon.");
        return;
    }

    ctx->tray_icon = gtk_status_icon_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    gtk_status_icon_set_tooltip_text(
        ctx->tray_icon,
        "Jotilla - last note; double-click for explorer; Ctrl+Alt++ for new note");
    gtk_status_icon_set_visible(ctx->tray_icon, TRUE);
    g_signal_connect(ctx->tray_icon, "activate",
                     G_CALLBACK(on_tray_activate), ctx);
    g_signal_connect(ctx->tray_icon, "popup-menu",
                     G_CALLBACK(on_tray_popup), ctx);
#else
    (void)ctx;
    g_warning("System tray is not available: GTK built without X11 support.");
#endif
}

/* Salva le note e termina su SIGTERM/SIGINT. */
static gboolean on_terminate_signal(gpointer data)
{    (void)save_open_notes(data);
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

/* Rilascia risorse GUI prima di liberare lo store. */
static void teardown(AppCtx *ctx, GApplication *gapp)
{
    platform_hotkey_uninstall();

    if (ctx->tray_icon != NULL) {
        gtk_status_icon_set_visible(ctx->tray_icon, FALSE);
        g_object_unref(ctx->tray_icon);
        ctx->tray_icon = NULL;
    }

    if (ctx->tray_menu != NULL) {
        gtk_widget_destroy(ctx->tray_menu);
        ctx->tray_menu = NULL;
    }

    ctx->preserving_open = TRUE;
    if (ctx->note_windows != NULL) {
        while (ctx->note_windows->len > 0) {
            GtkWidget *window = g_ptr_array_index(ctx->note_windows,
                                                  ctx->note_windows->len - 1);
            gtk_widget_destroy(window);
        }
        g_ptr_array_free(ctx->note_windows, TRUE);
        ctx->note_windows = NULL;
    }

    if (ctx->archive != NULL)
        gtk_widget_destroy(ctx->archive);

    (void)save_last_note(ctx);

    if (gapp != NULL)
        g_object_unref(gapp);
}

/* Esegue test interni su store e ricerca, poi pulisce. */
static int run_self_test(const char *archive_dir, const char *config_dir)
{    char *tmp = g_dir_make_tmp("jotilla-selftest-XXXXXX", NULL);
    char *notes = archive_dir != NULL ? g_strdup(archive_dir) : g_build_filename(tmp, "notes", NULL);
    char *cfg = config_dir != NULL ? g_strdup(config_dir) : g_build_filename(tmp, "cfg", NULL);
    GError *err = NULL;
    Store *store;
    Note *note;
    GPtrArray *hits;
    int rc = 0;

    if (tmp == NULL) {
        g_printerr("self-test: cannot create temp dir\n");
        return 1;
    }

    store = store_open(notes, cfg, &err);
    if (store == NULL) {
        g_printerr("self-test: store_open failed: %s\n", err != NULL ? err->message : "unknown");
        g_clear_error(&err);
        rc = 1;
        goto out;
    }

    note = store_new(store, "hello self test");
    if (note == NULL) {
        g_printerr("self-test: store_new failed\n");
        rc = 1;
        goto out_store;
    }

    store_set_tags(store, note, "Work; urgent");
    store_set_date(store, note, 20260101);

    hits = store_search(store, "hello", NULL, &err);
    if (hits == NULL || hits->len != 1) {
        g_printerr("self-test: search failed\n");
        rc = 1;
    }
    if (hits != NULL)
        g_ptr_array_free(hits, TRUE);

    hits = store_search(store, "", "urgent", &err);
    if (hits == NULL || hits->len != 1) {
        g_printerr("self-test: tag search failed\n");
        rc = 1;
    }
    if (hits != NULL)
        g_ptr_array_free(hits, TRUE);

    if (store_get(store, note->id) == NULL) {
        g_printerr("self-test: lookup failed\n");
        rc = 1;
    }

    if (rc == 0)
        g_print("SELF-TEST OK\n");

out_store:
    store_free(store);
out:
    if (tmp != NULL) {
        char *cmd = g_strdup_printf("rm -rf '%s'", tmp);
        g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);
        g_free(cmd);
    }
    g_free(tmp);
    g_free(notes);
    g_free(cfg);
    return rc;
}

/* Inizializza GTK, apre store e note, avvia il ciclo principale. */
int main(int argc, char **argv)
{    char *archive_dir = NULL;
    char *config_dir = NULL;
    gboolean self_test = FALSE;
    gboolean smoke_test = FALSE;
    GError *err = NULL;
    GOptionEntry entries[] = {
        {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &archive_dir, "Archive directory", "DIR"},
        {"config", 'c', 0, G_OPTION_ARG_FILENAME, &config_dir, "Config directory", "DIR"},
        {"self-test", 0, 0, G_OPTION_ARG_NONE, &self_test, "Run internal self test and exit", NULL},
        {"smoke-test", 0, 0, G_OPTION_ARG_NONE, &smoke_test, "Open windows, run briefly, exit", NULL},
        {NULL}
    };
    GOptionContext *opt = g_option_context_new(NULL);
    AppCtx ctx = {0};
    GApplication *gapp = NULL;

    g_option_context_add_main_entries(opt, entries, NULL);
    g_option_context_add_group(opt, gtk_get_option_group(TRUE));

    if (!g_option_context_parse(opt, &argc, &argv, &err)) {
        g_printerr("option parsing failed: %s\n", err != NULL ? err->message : "unknown");
        g_clear_error(&err);
        g_option_context_free(opt);
        return 2;
    }
    g_option_context_free(opt);

    if (self_test) {
        int rc = run_self_test(archive_dir, config_dir);
        g_free(archive_dir);
        g_free(config_dir);
        return rc;
    }

    /* Istanza unica tramite GApplication (D-Bus). */
    {
        char *app_id;
        if (g_getenv("JOTILLA_NO_SINGLE_INSTANCE") != NULL)
            app_id = g_strdup_printf("org.jotilla.Jotilla.test%d", (int)getpid());
        else
            app_id = g_strdup("org.jotilla.Jotilla");
        gapp = g_application_new(app_id, G_APPLICATION_DEFAULT_FLAGS);
        g_free(app_id);
    }
    g_signal_connect(gapp, "activate", G_CALLBACK(on_app_activate), &ctx);

    if (!g_application_register(gapp, NULL, &err)) {
        g_clear_error(&err);
        g_object_unref(gapp);
        g_free(archive_dir);
        g_free(config_dir);
        return 0;
    }

    if (g_application_get_is_remote(gapp)) {
        /* Un'altra istanza e' gia' attiva: chiedile di mostrarsi. */
        g_application_activate(gapp);
        g_object_unref(gapp);
        g_free(archive_dir);
        g_free(config_dir);
        return 0;
    }

    ctx.gapp = gapp;

    ctx.store = store_open(archive_dir, config_dir, &err);
    if (ctx.store == NULL) {
        char *message = g_strdup_printf(
            "The notes archive could not be opened: %s",
            err != NULL ? err->message : "unknown error");

        report_ui_error(message);
        g_free(message);
        g_clear_error(&err);
        g_object_unref(gapp);
        g_free(archive_dir);
        g_free(config_dir);
        return 1;
    }
    ctx.archive_dir = g_strdup(archive_dir != NULL ? archive_dir : store_archive_dir(ctx.store));
    ctx.config_dir = g_strdup(config_dir);
    ctx.note_windows = g_ptr_array_new();
    load_last_note(&ctx);
    ctx.restoring = TRUE;

    if (ctx.store->notes->len == 0) { /* nessuna nota: crea una vuota */
        Note *note = store_new(ctx.store, smoke_test ? "smoke test" : "");
        if (note != NULL)
            open_note(&ctx, note);
    } else {        guint i;
        for (i = 0; i < ctx.store->notes->len; ++i) {
            Note *note = g_ptr_array_index(ctx.store->notes, i);
            if (note->open)
                open_note(&ctx, note);
        }
        if (ctx.store->notes->len > 0) {
            gboolean any = FALSE;
            for (i = 0; i < ctx.store->notes->len; ++i) {
                Note *note = g_ptr_array_index(ctx.store->notes, i);
                if (note->open) {
                    any = TRUE;
                    break;
                }
            }
            if (!any) {
                Note *note = store_new(ctx.store, "");

                if (note != NULL)
                    open_note(&ctx, note);
            }
        }
    }
    ctx.restoring = FALSE;
    if (ctx.last_note == NULL && ctx.store->notes->len > 0)
        remember_note(&ctx, g_ptr_array_index(ctx.store->notes, 0));

    g_unix_signal_add(SIGTERM, on_terminate_signal, &ctx);
    g_unix_signal_add(SIGINT, on_terminate_signal, &ctx);

    setup_tray(&ctx);
    platform_hotkey_install(on_hotkey_new_note, &ctx);

    if (smoke_test) {
        on_show_archive(&ctx);
        g_timeout_add(1200, quit_loop, NULL);
    }

    gtk_main();

    teardown(&ctx, gapp);
    store_free(ctx.store);
    g_free(ctx.last_note);
    g_free(ctx.archive_dir);
    g_free(ctx.config_dir);
    g_free(archive_dir);
    g_free(config_dir);
    return 0;
}
