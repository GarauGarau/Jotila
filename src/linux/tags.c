/* Normalizzazione, ricerca e suddivisione di tag separati da ';'. */

#include <string.h>
#include <glib.h>
#include "tags.h"

#define TAGS_MAX 24
#define TAG_UNITS_MAX 64

/* Verifica che il nome del tag non contenga caratteri di controllo o delimitatori. */
static gboolean tag_name_valid(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    if (!*p) return FALSE;
    for (; *p; ++p) {
        if (*p < 0x20) return FALSE;
        if (*p == '#' || *p == '"') return FALSE;
    }
    return TRUE;
}

/* Windows/macOS limitano il nome a 63 unita' UTF-16. */
static gboolean tag_too_long(const char *name) {
    glong units = 0;
    gunichar2 *utf16 = g_utf8_to_utf16(name, -1, NULL, &units, NULL);

    if (utf16 == NULL) return TRUE;
    g_free(utf16);
    return units >= TAG_UNITS_MAX;
}

/* Confronto ordinale dei code point, come le controparti Windows/macOS. */
gint tags_compare(const char *left, const char *right) {
    const char *a = left ? left : "";
    const char *b = right ? right : "";

    while (*a && *b) {
        gunichar ca = g_utf8_get_char(a);
        gunichar cb = g_utf8_get_char(b);

        if (ca != cb) return ca < cb ? -1 : 1;
        a = g_utf8_next_char(a);
        b = g_utf8_next_char(b);
    }
    return (*a != 0) - (*b != 0);
}

/* Comparatore per ordinamento dei tag. */
static gint tag_compare(gconstpointer a, gconstpointer b) {
    return tags_compare(*(const char *const *)a, *(const char *const *)b);
}

/* Normalizza in NFC e poi applica il case folding Unicode. */
static char *tag_fold(const char *text) {
    char *normalized;
    char *folded;
    if (!text) return NULL;
    normalized = g_utf8_normalize(text, -1, G_NORMALIZE_DEFAULT_COMPOSE);
    if (!normalized) return NULL;
    folded = g_utf8_casefold(normalized, -1);
    g_free(normalized);
    return folded;
}

/* Normalizza input: trim, rimuove '#', NFC, casefold, deduplica, ordina. */
char *tags_normalize(const char *input) {
    GPtrArray *names;
    GString *output;
    char **parts;
    guint i;

    if (!input) return g_strdup("");

    names = g_ptr_array_new_with_free_func(g_free);
    parts = g_strsplit(input, ";", -1);
    for (i = 0; parts[i]; ++i) {
        char *raw, *trimmed, *folded;
        gboolean duplicate = FALSE;
        guint j;

        raw = g_strdup(parts[i]);
        trimmed = g_strstrip(raw);
        if (!*trimmed) {
            g_free(raw);
            continue;
        }
        if (!tag_name_valid(trimmed) || tag_too_long(trimmed)) {
            g_free(raw);
            g_strfreev(parts);
            g_ptr_array_free(names, TRUE);
            return NULL;
        }

        folded = tag_fold(trimmed);
        g_free(raw);
        if (!folded || !*folded) {
            g_free(folded);
            g_strfreev(parts);
            g_ptr_array_free(names, TRUE);
            return NULL;
        }

        for (j = 0; j < names->len; ++j)
            if (!g_strcmp0((const char *)g_ptr_array_index(names, j), folded)) {
                duplicate = TRUE;
                break;
            }
        if (duplicate) {
            g_free(folded);
            continue;
        }
        if (names->len >= TAGS_MAX) {
            g_free(folded);
            g_strfreev(parts);
            g_ptr_array_free(names, TRUE);
            return NULL;
        }
        g_ptr_array_add(names, folded);
    }
    g_strfreev(parts);

    g_ptr_array_sort(names, tag_compare);
    output = g_string_new("");
    for (i = 0; i < names->len; ++i) {
        if (i) g_string_append_c(output, ';');
        g_string_append(output, (const char *)g_ptr_array_index(names, i));
    }
    g_ptr_array_free(names, TRUE);
    return g_string_free(output, FALSE);
}

/* Verifica se un tag è presente nella lista (case-insensitive). */
gboolean tags_contains(const char *tags, const char *name) {
    char *key;
    char **parts;
    guint i;
    gboolean found = FALSE;

    if (!tags || !name || !*name) return FALSE;
    key = g_utf8_casefold(name, -1);
    if (!key) return FALSE;

    parts = g_strsplit(tags, ";", -1);
    for (i = 0; parts[i] && !found; ++i) {
        char *item = g_strstrip(g_strdup(parts[i]));
        if (*item) {
            char *folded = g_utf8_casefold(item, -1);
            if (folded && !g_strcmp0(folded, key)) found = TRUE;
            g_free(folded);
        }
        g_free(item);
    }
    g_strfreev(parts);
    g_free(key);
    return found;
}

/* Suddivide la stringa dei tag in array di stringhe. */
GPtrArray *tags_split(const char *tags) {
    GPtrArray *items = g_ptr_array_new_with_free_func(g_free);
    char **parts;
    guint i;

    if (!tags) return items;

    parts = g_strsplit(tags, ";", -1);
    for (i = 0; parts[i]; ++i) {
        char *item = g_strstrip(g_strdup(parts[i]));
        if (*item) g_ptr_array_add(items, item);
        else g_free(item);
    }
    g_strfreev(parts);
    return items;
}

typedef struct {
    char *name;
    gint64 count;
} TagSuggestion;

static void suggestion_free(gpointer data) {
    TagSuggestion *item = (TagSuggestion *)data;
    if (!item) return;
    g_free(item->name);
    g_free(item);
}

/* Ordina per conteggio decrescente, poi per nome. */
static gint suggestion_compare(gconstpointer a, gconstpointer b) {
    const TagSuggestion *left = *(const TagSuggestion *const *)a;
    const TagSuggestion *right = *(const TagSuggestion *const *)b;
    if (left->count != right->count)
        return left->count > right->count ? -1 : 1;
    return g_utf8_collate(left->name, right->name);
}

/* Suggerisce tag per prefisso a partire dal catalogo "tag\tconteggio". */
GPtrArray *tags_suggest_from(const char *counts, const char *prefix, const char *assigned, int max) {
    GPtrArray *candidates;
    GPtrArray *results;
    char *folded_prefix;
    char **lines;
    guint i;

    results = g_ptr_array_new_with_free_func(g_free);
    if (max <= 0) return results;

    candidates = g_ptr_array_new_with_free_func(suggestion_free);
    folded_prefix = tag_fold(prefix ? prefix : "");
    if (!folded_prefix) folded_prefix = g_strdup("");
    lines = g_strsplit(counts ? counts : "", "\n", -1);

    for (i = 0; lines[i]; ++i) {
        char *line = lines[i];
        char *tab = strchr(line, '\t');
        char *name;
        char *folded;
        gint64 count;
        TagSuggestion *item;

        if (!tab || tab == line) continue;
        name = g_strndup(line, (gsize)(tab - line));
        g_strstrip(name);
        if (!*name) {
            g_free(name);
            continue;
        }
        folded = tag_fold(name);
        if (!folded || !g_str_has_prefix(folded, folded_prefix) ||
            (assigned && tags_contains(assigned, name))) {
            g_free(folded);
            g_free(name);
            continue;
        }
        g_free(folded);
        count = g_ascii_strtoll(tab + 1, NULL, 10);
        item = g_new0(TagSuggestion, 1);
        item->name = name;
        item->count = count;
        g_ptr_array_add(candidates, item);
    }

    g_strfreev(lines);
    g_free(folded_prefix);
    g_ptr_array_sort(candidates, suggestion_compare);

    for (i = 0; i < candidates->len && (int)i < max; ++i) {
        TagSuggestion *item = g_ptr_array_index(candidates, i);
        g_ptr_array_add(results, g_strdup(item->name));
    }

    g_ptr_array_free(candidates, TRUE);
    return results;
}
