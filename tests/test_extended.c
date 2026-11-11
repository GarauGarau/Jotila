/* Test estesi: casi limite di date, tag, store e RTF.
 * Include robustezza, Unicode ed escape. */
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <string.h>

#include "date.h"
#include "tags.h"
#include "palette.h"
#include "jotilla.h"
#include "rtf.h"

static gboolean gtk_ready = FALSE;

static void remove_tree(const char *path) {
    GDir *dir = g_dir_open(path, 0, NULL);
    if (dir) {
        const char *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            char *child = g_build_filename(path, name, NULL);
            if (g_file_test(child, G_FILE_TEST_IS_DIR))
                remove_tree(child);
            else
                g_remove(child);
            g_free(child);
        }
        g_dir_close(dir);
    }
    g_rmdir(path);
}

static guint tag_count(const char *tags) {
    guint count = 0;
    const char *p;

    if (!tags || !*tags)
        return 0;
    count = 1;
    for (p = tags; *p; ++p)
        if (*p == ';')
            ++count;
    return count;
}

/* ------------------------------------------------------------------ date */

static void test_date_extended(void) {
    char *formatted;

    /* 1600 is outside the supported range (minimum year 1601). */
    g_assert_cmpint(date_days(1600, 2), ==, 0);
    g_assert_cmpint(date_days(1601, 2), ==, 28);
    g_assert_cmpint(date_days(1900, 2), ==, 28);
    g_assert_cmpint(date_days(2000, 2), ==, 29);
    g_assert_cmpint(date_days(2024, 2), ==, 29);

    g_assert_cmpint(date_valid(20000229), ==, 1);
    g_assert_cmpint(date_valid(19000229), ==, 0);
    g_assert_cmpint(date_valid(16000229), ==, 0);
    g_assert_cmpint(date_valid(16010101), ==, 1);
    g_assert_cmpint(date_valid(99991231), ==, 1);

    /* Year boundaries. */
    g_assert_cmpuint(date_shift(20241231, 1), ==, 20250101u);
    g_assert_cmpuint(date_shift(20250101, -1), ==, 20241231u);
    g_assert_cmpuint(date_shift(20240229, 1), ==, 20240301u);
    g_assert_cmpuint(date_shift(20240301, -1), ==, 20240229u);
    g_assert_cmpuint(date_shift(20231231, 1), ==, 20240101u);

    /* Clamping at the supported minimum and maximum. */
    g_assert_cmpuint(date_shift(16010101, -1), ==, 16010101u);
    g_assert_cmpuint(date_shift(16010101, -1000), ==, 16010101u);
    g_assert_cmpuint(date_shift(99991231, 1), ==, 99991231u);
    g_assert_cmpuint(date_shift(99991231, 1000), ==, 99991231u);

    /* Invalid input. */
    g_assert_cmpuint(date_shift(0, 1), ==, 0u);
    g_assert_cmpuint(date_parse(NULL), ==, 0u);
    g_assert_cmpuint(date_parse(""), ==, 0u);
    g_assert_cmpuint(date_parse("1234567"), ==, 0u);
    g_assert_cmpuint(date_parse("123456789"), ==, 0u);
    g_assert_cmpuint(date_parse("abcdefgh"), ==, 0u);
    g_assert_cmpuint(date_parse("20241301"), ==, 0u);
    g_assert_cmpuint(date_parse("20240230"), ==, 0u);
    g_assert_cmpuint(date_parse("2024-1-1"), ==, 0u);

    g_assert_cmpuint(date_parse("20000229"), ==, 20000229u);
    g_assert_cmpuint(date_parse("19000228"), ==, 19000228u);

    formatted = date_format(20240229);
    g_assert_cmpstr(formatted, ==, "2024-02-29");
    g_free(formatted);

    formatted = date_format(20240230);
    g_assert_cmpstr(formatted, ==, "");
    g_free(formatted);
}

/* ------------------------------------------------------------------ tags */

static void test_tags_extended(void) {
    GString *input;
    char *out;
    char *t63;
    char *t64;
    char *joined;
    GPtrArray *parts;
    guint i;

    /* NULL and empty input. */
    out = tags_normalize(NULL);
    g_assert_cmpstr(out, ==, "");
    g_free(out);

    out = tags_normalize("");
    g_assert_cmpstr(out, ==, "");
    g_free(out);

    out = tags_normalize(";;; ; \t ;");
    g_assert_cmpstr(out, ==, "");
    g_free(out);

    /* 24 tag distinti sono validi; il 25esimo invalida l'elenco. */
    input = g_string_new(NULL);
    for (i = 0; i < 24; ++i) {
        if (i)
            g_string_append_c(input, ';');
        g_string_append_printf(input, "tag%02u", i);
    }
    out = tags_normalize(input->str);
    g_assert_cmpuint(tag_count(out), ==, 24);
    g_assert_true(tags_contains(out, "tag00"));
    g_assert_true(tags_contains(out, "tag23"));
    g_free(out);
    g_string_append(input, ";tag24");
    g_assert_null(tags_normalize(input->str));
    g_string_free(input, TRUE);

    /* 63 bytes allowed, 64 bytes rejected. */
    t63 = g_strnfill(63, 'a');
    t64 = g_strnfill(64, 'b');
    out = tags_normalize(t63);
    g_assert_cmpstr(out, ==, t63);
    g_free(out);
    joined = g_strdup_printf("%s;%s", t63, t64);
    g_assert_null(tags_normalize(joined));
    g_free(joined);
    g_free(t63);
    g_free(t64);

    /* Un tag non valido invalida l'intero elenco (Windows/macOS). */
    g_assert_null(tags_normalize("good;bad#tag"));
    g_assert_null(tags_normalize("good;has\"quote"));
    g_assert_null(tags_normalize("good;ctrl\x01char"));
    g_assert_null(tags_normalize("#Work"));

    /* Duplicates with different case collapse. */
    out = tags_normalize("Work;work;WORK");
    g_assert_cmpstr(out, ==, "work");
    g_free(out);

    /* Unicode case folding deduplicates Cafe/CAFE variants. */
    out = tags_normalize("Caf\xc3\xa9;CAF\xc3\x89");
    g_assert_cmpuint(tag_count(out), ==, 1);
    g_assert_true(tags_contains(out, "CAF\xc3\x89"));
    g_free(out);

    /* Whitespace and separators are trimmed. */
    out = tags_normalize(" a ; ; b ;\t c\t");
    g_assert_cmpstr(out, ==, "a;b;c");
    g_free(out);

    /* tags_split ignores empty entries. */
    parts = tags_split(" a ;; b ; ");
    g_assert_nonnull(parts);
    g_assert_cmpuint(parts->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(parts, 0), ==, "a");
    g_assert_cmpstr(g_ptr_array_index(parts, 1), ==, "b");
    g_ptr_array_free(parts, TRUE);

    parts = tags_split("");
    g_assert_nonnull(parts);
    g_assert_cmpuint(parts->len, ==, 0);
    g_ptr_array_free(parts, TRUE);
}

/* ----------------------------------------------------------------- store */

static guint search_count(Store *store, const char *query, const char *tag) {
    GError *err = NULL;
    GPtrArray *results;
    guint count;

    results = store_search(store, query, tag, &err);
    g_assert_no_error(err);
    g_assert_nonnull(results);
    count = results->len;
    g_ptr_array_free(results, TRUE);
    return count;
}

static void make_dirs(const char *base, char **archive, char **config) {
    *archive = g_build_filename(base, "archive dir \xc3\xbc\xc3\xb1\xc3\xae", NULL);
    *config = g_build_filename(base, "config dir \xc3\xbc\xc3\xb1\xc3\xae", NULL);
    g_assert_cmpint(g_mkdir_with_parents(*archive, 0700), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(*config, 0700), ==, 0);
}

static void test_store_extended(void) {
    GError *err = NULL;
    char *base;
    char *archive;
    char *config;
    char **ids;
    Store *store;
    Store *reopened;
    Store *final_store;
    Store *cleared;
    Note *note;
    Note *found;
    char *catalog;
    char *big;
    char *big_id;
    guint i;
    const gsize big_size = 1024 * 1024;

    base = g_dir_make_tmp("jotilla-ext-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(base);
    make_dirs(base, &archive, &config);

    store = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);

    ids = g_new0(char *, 100);

    note = store_new(store, "alpha beta special");
    g_assert_nonnull(note);
    ids[0] = g_strdup(note->id);
    store_set_tags(store, note, "Work");
    store_set_date(store, note, 20240229);

    note = store_new(store, "alpha gamma special");
    g_assert_nonnull(note);
    ids[1] = g_strdup(note->id);
    store_set_tags(store, note, "misc");
    store_set_date(store, note, 20240115);

    note = store_new(store, "The Quick Brown Fox jumps");
    g_assert_nonnull(note);
    ids[2] = g_strdup(note->id);
    store_set_tags(store, note, "zeta");

    note = store_new(store, "alpha x beta nonadjacent");
    g_assert_nonnull(note);
    ids[3] = g_strdup(note->id);

    note = store_new(store, "alpha tagged alpha");
    g_assert_nonnull(note);
    ids[4] = g_strdup(note->id);
    store_set_tags(store, note, "alpha");

    for (i = 5; i < 100; ++i) {
        char buffer[160];
        g_snprintf(buffer, sizeof(buffer),
                   "note %u varied text lorem ipsum dolor sit amet %u", i, i * 7);
        note = store_new(store, buffer);
        g_assert_nonnull(note);
        ids[i] = g_strdup(note->id);
    }

    g_assert_cmpuint(store->notes->len, ==, 100);
    for (i = 0; i < 100; ++i)
        g_assert_nonnull(ids[i]);

    g_assert_true(store_save(store, store_get(store, ids[0]), &err));
    g_assert_no_error(err);
    store_free(store);

    reopened = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(reopened);
    g_assert_cmpuint(reopened->notes->len, ==, 100);

    found = store_get(reopened, ids[0]);
    g_assert_nonnull(found);
    g_assert_cmpstr(found->text, ==, "alpha beta special");
    g_assert_cmpstr(found->tags, ==, "work");
    g_assert_cmpuint(found->date, ==, 20240229u);

    found = store_get(reopened, ids[1]);
    g_assert_nonnull(found);
    g_assert_cmpstr(found->tags, ==, "misc");
    g_assert_cmpuint(found->date, ==, 20240115u);

    found = store_get(reopened, ids[2]);
    g_assert_nonnull(found);
    g_assert_null(found->rtf);

    /* AND semantics: both words must be present. */
    g_assert_cmpuint(search_count(reopened, "alpha beta", NULL), ==, 2);
    /* Quoted phrase is matched as an adjacent substring. */
    g_assert_cmpuint(search_count(reopened, "\"alpha beta\"", NULL), ==, 1);
    /* Tag search, case insensitive. */
    g_assert_cmpuint(search_count(reopened, "#work", NULL), ==, 1);
    g_assert_cmpuint(search_count(reopened, "#WORK", NULL), ==, 1);
    /* Untagged notes. */
    g_assert_cmpuint(search_count(reopened, "-#", NULL), ==, 96);
    /* Text search is case insensitive. */
    g_assert_cmpuint(search_count(reopened, "ALPHA", NULL), ==, 4);
    /* Combined query + tag filter. */
    g_assert_cmpuint(search_count(reopened, "alpha beta", "work"), ==, 1);

    catalog = store_tags_catalog(reopened);
    g_assert_cmpstr(catalog, ==, "alpha\nmisc\nwork\nzeta");
    g_free(catalog);

    /* Delete an existing note, then verify it stays gone. */
    g_assert_true(store_delete(reopened, ids[1], &err));
    g_assert_no_error(err);
    store_free(reopened);

    final_store = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(final_store);
    g_assert_cmpuint(final_store->notes->len, ==, 99);
    g_assert_null(store_get(final_store, ids[1]));
    g_assert_nonnull(store_get(final_store, ids[0]));

    /* Clear a date and verify it round-trips as zero. */
    found = store_get(final_store, ids[0]);
    g_assert_nonnull(found);
    store_set_date(final_store, found, 0);
    store_free(final_store);

    cleared = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(cleared);
    found = store_get(cleared, ids[0]);
    g_assert_nonnull(found);
    g_assert_cmpuint(found->date, ==, 0u);

    /* 1 MB text note round-trip. */
    big = g_malloc(big_size + 1);
    memset(big, 'x', big_size);
    memcpy(big, "BEGIN", 5);
    memcpy(big + big_size - 3, "END", 3);
    big[big_size] = '\0';

    note = store_new(cleared, big);
    g_assert_nonnull(note);
    big_id = g_strdup(note->id);
    store_free(cleared);

    cleared = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(cleared);
    found = store_get(cleared, big_id);
    g_assert_nonnull(found);
    g_assert_cmpuint(strlen(found->text), ==, big_size);
    g_assert_cmpint(memcmp(found->text, big, big_size), ==, 0);
    store_free(cleared);

    g_free(big);
    g_free(big_id);

    for (i = 0; i < 100; ++i)
        g_free(ids[i]);
    g_free(ids);

    remove_tree(archive);
    remove_tree(config);
    remove_tree(base);
    g_free(archive);
    g_free(config);
    g_free(base);
}

static void test_store_robustness(void) {
    GError *err = NULL;
    char *base;
    char *archive;
    char *config;
    char *path;
    char *ini_path;
    char *bom_path;
    char *nortf_path;
    const char bom_file[] = "\xef\xbb\xbf" "bom text";
    const char nortf_text[] = "no rtf here";
    const char garbage[] = "this is not an ini\n[unclosed\nkey=value\n\x01\x02\n";
    Store *store;
    Note *note;

    base = g_dir_make_tmp("jotilla-ext-robust-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(base);
    make_dirs(base, &archive, &config);

    /* UTF-8 BOM must be stripped from note text files. */
    bom_path = g_build_filename(archive, "note-bom-manual.txt", NULL);
    g_assert_true(g_file_set_contents(bom_path, bom_file,
                                      (gssize)(sizeof(bom_file) - 1), NULL));
    g_free(bom_path);

    /* Note without its .rtf companion. */
    nortf_path = g_build_filename(archive, "note-nortf-manual.txt", NULL);
    g_assert_true(g_file_set_contents(nortf_path, nortf_text,
                                      (gssize)(sizeof(nortf_text) - 1), NULL));
    g_free(nortf_path);

    /* Malformed notes.ini must not prevent notes from loading. */
    ini_path = g_build_filename(config, "notes.ini", NULL);
    g_assert_true(g_file_set_contents(ini_path, garbage,
                                      (gssize)(sizeof(garbage) - 1), NULL));
    g_free(ini_path);

    store = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);
    g_assert_cmpuint(store->notes->len, ==, 2);

    note = store_get(store, "note-bom-manual");
    g_assert_nonnull(note);
    g_assert_cmpstr(note->text, ==, "bom text");

    note = store_get(store, "note-nortf-manual");
    g_assert_nonnull(note);
    g_assert_cmpstr(note->text, ==, "no rtf here");
    g_assert_null(note->rtf);

    store_free(store);

    path = g_build_filename(config, "notes.ini", NULL);
    g_remove(path);
    g_free(path);

    remove_tree(archive);
    remove_tree(config);
    remove_tree(base);
    g_free(archive);
    g_free(config);
    g_free(base);
}

static void test_store_delete_nonexistent(void) {
    GError *err = NULL;
    char *base;
    char *archive;
    char *config;
    Store *store;
    gboolean deleted;

    base = g_dir_make_tmp("jotilla-ext-del-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(base);
    make_dirs(base, &archive, &config);

    store = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);

    deleted = store_delete(store, "note-does-not-exist", &err);
    g_assert_no_error(err);
    g_assert_cmpint(deleted, ==, FALSE);

    store_free(store);

    remove_tree(archive);
    remove_tree(config);
    remove_tree(base);
    g_free(archive);
    g_free(config);
    g_free(base);
}

static void test_search_accent_insensitive(void) {
    GError *err = NULL;
    char *base;
    char *archive;
    char *config;
    Store *store;
    Note *note;

    base = g_dir_make_tmp("jotilla-ext-accent-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(base);
    make_dirs(base, &archive, &config);

    store = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);

    note = store_new(store, "Caf\xc3\xa9");
    g_assert_nonnull(note);
    g_assert_true(store_save(store, note, &err));
    g_assert_no_error(err);

    /* fold_text rimuove gli accenti: "cafe" trova "Café". */
    g_assert_cmpuint(search_count(store, "cafe", NULL), ==, 1);
    g_assert_cmpuint(search_count(store, "CAFE", NULL), ==, 1);
    g_assert_cmpuint(search_count(store, "caf\xc3\xa9", NULL), ==, 1);
    g_assert_cmpuint(search_count(store, "CAF\xc3\x89", NULL), ==, 1);
    g_assert_cmpuint(search_count(store, "tea", NULL), ==, 0);

    store_free(store);
    remove_tree(archive);
    remove_tree(config);
    remove_tree(base);
    g_free(archive);
    g_free(config);
    g_free(base);
}

static void test_custom_ink_persistence(void) {
    GError *err = NULL;
    char *base;
    char *archive;
    char *config;
    char *id;
    Store *store;
    Store *reopened;
    Note *note;
    Note *found;

    base = g_dir_make_tmp("jotilla-ext-ink-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(base);
    make_dirs(base, &archive, &config);

    store = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);

    note = store_new(store, "custom ink");
    g_assert_nonnull(note);
    g_assert_false(note->custom_ink);
    id = g_strdup(note->id);
    g_assert_nonnull(id);

    note->custom_ink = TRUE;
    g_assert_true(store_save(store, note, &err));
    g_assert_no_error(err);
    store_free(store);

    reopened = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(reopened);
    found = store_get(reopened, id);
    g_assert_nonnull(found);
    g_assert_true(found->custom_ink);
    store_free(reopened);

    g_free(id);
    remove_tree(archive);
    remove_tree(config);
    remove_tree(base);
    g_free(archive);
    g_free(config);
    g_free(base);
}

static void test_search_term_limit(void) {
    GError *err = NULL;
    char *base;
    char *archive;
    char *config;
    GString *full;
    GString *partial;
    GString *query;
    GPtrArray *results;
    Store *store;
    Note *note;
    guint i;

    base = g_dir_make_tmp("jotilla-ext-terms-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(base);
    make_dirs(base, &archive, &config);

    store = store_open(archive, config, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);

    /* Nota con tutti i 20 termini. */
    full = g_string_new(NULL);
    for (i = 1; i <= 20; ++i)
        g_string_append_printf(full, "t%02u ", i);
    note = store_new(store, full->str);
    g_assert_nonnull(note);

    /* Nota limitata ai primi 16 termini. */
    partial = g_string_new(NULL);
    for (i = 1; i <= 16; ++i)
        g_string_append_printf(partial, "t%02u ", i);
    note = store_new(store, partial->str);
    g_assert_nonnull(note);

    /* La query con 20 termini e' invalida, come su Windows e macOS. */
    query = g_string_new(NULL);
    for (i = 1; i <= 20; ++i) {
        if (i > 1)
            g_string_append_c(query, ' ');
        g_string_append_printf(query, "t%02u", i);
    }
    results = store_search(store, query->str, NULL, &err);
    g_assert_nonnull(err);
    g_assert_null(results);
    g_clear_error(&err);

    /* Sedici termini restano validi e trovano entrambe le note. */
    g_string_truncate(query, 0);
    for (i = 1; i <= 16; ++i) {
        if (i > 1)
            g_string_append_c(query, ' ');
        g_string_append_printf(query, "t%02u", i);
    }
    results = store_search(store, query->str, NULL, &err);
    g_assert_no_error(err);
    g_assert_nonnull(results);
    g_assert_cmpuint(results->len, ==, 2);
    g_ptr_array_free(results, TRUE);

    results = store_search(store, "\"unclosed", NULL, &err);
    g_assert_no_error(err);
    g_assert_nonnull(results);
    g_assert_cmpuint(results->len, ==, 0);
    g_ptr_array_free(results, TRUE);

    results = store_search(store, "#bad;tag", NULL, &err);
    g_assert_nonnull(err);
    g_assert_null(results);
    g_clear_error(&err);

    g_string_free(query, TRUE);
    g_string_free(partial, TRUE);
    g_string_free(full, TRUE);
    store_free(store);
    remove_tree(archive);
    remove_tree(config);
    remove_tree(base);
    g_free(archive);
    g_free(config);
    g_free(base);
}

/* ------------------------------------------------------------------- RTF */

static GtkTextBuffer *new_buffer(void) {
    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
    g_assert_nonnull(buffer);
    return buffer;
}

static void apply_range(GtkTextBuffer *buffer, GtkTextTag *tag, int start, int end) {
    GtkTextIter iter_start;
    GtkTextIter iter_end;

    gtk_text_buffer_get_iter_at_offset(buffer, &iter_start, start);
    gtk_text_buffer_get_iter_at_offset(buffer, &iter_end, end);
    gtk_text_buffer_apply_tag(buffer, tag, &iter_start, &iter_end);
}

static gboolean iter_has_named_tag(GtkTextBuffer *buffer, int offset, const char *name) {
    GtkTextIter iter;
    GtkTextTag *tag;

    gtk_text_buffer_get_iter_at_offset(buffer, &iter, offset);
    tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), name);
    return tag != NULL && gtk_text_iter_has_tag(&iter, tag);
}

static gboolean iter_fg_color(GtkTextBuffer *buffer, int offset, GdkRGBA *out) {
    GtkTextIter iter;
    GSList *tags;
    GSList *node;
    gboolean found = FALSE;

    gtk_text_buffer_get_iter_at_offset(buffer, &iter, offset);
    tags = gtk_text_iter_get_tags(&iter);
    for (node = tags; node != NULL && !found; node = node->next) {
        gboolean is_set = FALSE;
        GdkRGBA *color = NULL;

        g_object_get(GTK_TEXT_TAG(node->data),
                     "foreground-set", &is_set,
                     "foreground-rgba", &color,
                     NULL);
        if (is_set && color != NULL) {
            *out = *color;
            found = TRUE;
        }
        if (color != NULL)
            g_boxed_free(GDK_TYPE_RGBA, color);
    }
    g_slist_free(tags);
    return found;
}

static char *buffer_text(GtkTextBuffer *buffer) {
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void test_rtf_multiple_runs(void) {
    GtkTextBuffer *buffer;
    GtkTextBuffer *copy;
    GtkTextTag *bold;
    GtkTextTag *italic;
    GtkTextTag *underline;
    GtkTextTag *strike;
    GtkTextTag *red;
    GtkTextTag *blue;
    GdkRGBA color;
    char *rtf;
    char *text;
    const char *source = "one two three four five";

    if (!gtk_ready) {
        g_test_skip("GTK could not be initialized (no display)");
        return;
    }

    buffer = new_buffer();

    bold = gtk_text_buffer_create_tag(buffer, "bold",
                                      "weight", PANGO_WEIGHT_BOLD, NULL);
    italic = gtk_text_buffer_create_tag(buffer, "italic",
                                        "style", PANGO_STYLE_ITALIC, NULL);
    underline = gtk_text_buffer_create_tag(buffer, "underline",
                                           "underline", PANGO_UNDERLINE_SINGLE, NULL);
    strike = gtk_text_buffer_create_tag(buffer, "strike",
                                        "strikethrough", TRUE, NULL);
    red = gtk_text_buffer_create_tag(buffer, "red",
                                     "foreground", "#ff0000", NULL);
    blue = gtk_text_buffer_create_tag(buffer, "blue",
                                      "foreground", "#0000ff", NULL);
    g_assert_nonnull(bold);
    g_assert_nonnull(italic);
    g_assert_nonnull(underline);
    g_assert_nonnull(strike);
    g_assert_nonnull(red);
    g_assert_nonnull(blue);

    gtk_text_buffer_set_text(buffer, source, -1);
    apply_range(buffer, bold, 0, 3);
    apply_range(buffer, italic, 4, 7);
    apply_range(buffer, underline, 4, 7);
    apply_range(buffer, strike, 8, 13);
    apply_range(buffer, red, 14, 18);
    apply_range(buffer, bold, 19, 23);
    apply_range(buffer, italic, 19, 23);
    apply_range(buffer, underline, 19, 23);
    apply_range(buffer, strike, 19, 23);
    apply_range(buffer, blue, 19, 23);

    rtf = rtf_from_buffer(buffer);
    g_assert_nonnull(rtf);
    g_assert_nonnull(strstr(rtf, "\\rtf1"));

    copy = new_buffer();
    rtf_to_buffer(rtf, copy);

    text = buffer_text(copy);
    g_assert_cmpstr(text, ==, source);
    g_free(text);

    g_assert_true(iter_has_named_tag(copy, 0, "bold"));
    g_assert_false(iter_has_named_tag(copy, 4, "bold"));
    g_assert_true(iter_has_named_tag(copy, 4, "italic"));
    g_assert_true(iter_has_named_tag(copy, 4, "underline"));
    g_assert_false(iter_has_named_tag(copy, 4, "strike"));
    g_assert_true(iter_has_named_tag(copy, 8, "strike"));
    g_assert_false(iter_has_named_tag(copy, 8, "bold"));

    g_assert_true(iter_has_named_tag(copy, 19, "bold"));
    g_assert_true(iter_has_named_tag(copy, 19, "italic"));
    g_assert_true(iter_has_named_tag(copy, 19, "underline"));
    g_assert_true(iter_has_named_tag(copy, 19, "strike"));

    g_assert_false(iter_fg_color(copy, 0, &color));
    g_assert_true(iter_fg_color(copy, 14, &color));
    g_assert_cmpfloat(color.red, >, 0.99);
    g_assert_cmpfloat(color.green, <, 0.01);
    g_assert_cmpfloat(color.blue, <, 0.01);

    g_assert_true(iter_fg_color(copy, 19, &color));
    g_assert_cmpfloat(color.red, <, 0.01);
    g_assert_cmpfloat(color.green, <, 0.01);
    g_assert_cmpfloat(color.blue, >, 0.99);

    g_free(rtf);
    g_object_unref(copy);
    g_object_unref(buffer);
}

static void test_rtf_escapes_and_newlines(void) {
    GtkTextBuffer *buffer;
    GtkTextBuffer *copy;
    char *rtf;
    char *text;
    const char *source = "line1\nline2\\path{brace}end\ttab";

    if (!gtk_ready) {
        g_test_skip("GTK could not be initialized (no display)");
        return;
    }

    buffer = new_buffer();
    gtk_text_buffer_set_text(buffer, source, -1);

    rtf = rtf_from_buffer(buffer);
    g_assert_nonnull(rtf);
    g_assert_nonnull(strstr(rtf, "\\\\"));
    g_assert_nonnull(strstr(rtf, "\\{"));
    g_assert_nonnull(strstr(rtf, "\\}"));
    g_assert_nonnull(strstr(rtf, "\\par"));

    copy = new_buffer();
    rtf_to_buffer(rtf, copy);

    text = buffer_text(copy);
    g_assert_cmpstr(text, ==, source);
    g_free(text);

    g_free(rtf);
    g_object_unref(copy);
    g_object_unref(buffer);
}

static void test_rtf_empty_and_malformed(void) {
    GtkTextBuffer *buffer;
    GtkTextBuffer *copy;
    char *rtf;
    char *text;

    if (!gtk_ready) {
        g_test_skip("GTK could not be initialized (no display)");
        return;
    }

    buffer = new_buffer();
    rtf = rtf_from_buffer(buffer);
    g_assert_nonnull(rtf);
    g_assert_nonnull(strstr(rtf, "\\rtf1"));

    copy = new_buffer();
    rtf_to_buffer(rtf, copy);
    text = buffer_text(copy);
    g_assert_cmpstr(text, ==, "");
    g_free(text);

    /* Empty input clears the buffer. */
    gtk_text_buffer_set_text(copy, "stale", -1);
    rtf_to_buffer("", copy);
    text = buffer_text(copy);
    g_assert_cmpstr(text, ==, "");
    g_free(text);

    /* NULL input is harmless. */
    rtf_to_buffer(NULL, copy);
    text = buffer_text(copy);
    g_assert_cmpstr(text, ==, "");
    g_free(text);

    /* Malformed RTF falls back to plain text without crashing. */
    rtf_to_buffer("{\\rtf1 \\b broken {unclosed", copy);
    text = buffer_text(copy);
    g_assert_nonnull(text);
    g_assert_true(g_utf8_validate(text, -1, NULL));
    g_free(text);

    rtf_to_buffer("{\\rtf1 \\'zz bad escape}", copy);
    text = buffer_text(copy);
    g_assert_nonnull(text);
    g_assert_true(g_utf8_validate(text, -1, NULL));
    g_free(text);

    g_free(rtf);
    g_object_unref(copy);
    g_object_unref(buffer);
}

static void test_rtf_unicode(void) {
    GtkTextBuffer *buffer;
    GtkTextBuffer *copy;
    char *rtf;
    char *text;
    const char *source = "Caf\xc3\xa9 \xc3\xbcn\xc3\xaf" "code "
                         "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e "
                         "\xf0\x9f\x98\x80";

    if (!gtk_ready) {
        g_test_skip("GTK could not be initialized (no display)");
        return;
    }

    buffer = new_buffer();
    gtk_text_buffer_set_text(buffer, source, -1);

    rtf = rtf_from_buffer(buffer);
    g_assert_nonnull(rtf);

    copy = new_buffer();
    rtf_to_buffer(rtf, copy);

    text = buffer_text(copy);
    g_assert_cmpstr(text, ==, source);
    g_free(text);

    g_free(rtf);
    g_object_unref(copy);
    g_object_unref(buffer);
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    gtk_ready = gtk_init_check(NULL, NULL);

    g_test_add_func("/extended/date", test_date_extended);
    g_test_add_func("/extended/tags", test_tags_extended);
    g_test_add_func("/extended/store", test_store_extended);
    g_test_add_func("/extended/store_robustness", test_store_robustness);
    g_test_add_func("/extended/store_delete_nonexistent", test_store_delete_nonexistent);
    g_test_add_func("/extended/search_accent_insensitive", test_search_accent_insensitive);
    g_test_add_func("/extended/custom_ink_persistence", test_custom_ink_persistence);
    g_test_add_func("/extended/search_term_limit", test_search_term_limit);
    g_test_add_func("/extended/rtf_multiple_runs", test_rtf_multiple_runs);
    g_test_add_func("/extended/rtf_escapes_newlines", test_rtf_escapes_and_newlines);
    g_test_add_func("/extended/rtf_empty_malformed", test_rtf_empty_and_malformed);
    g_test_add_func("/extended/rtf_unicode", test_rtf_unicode);

    return g_test_run();
}
