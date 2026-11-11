/* Persistenza note su disco: archivio testo/RTF e metadati INI. */
/* Fornisce apertura, salvataggio, cancellazione e ricerca. */
#include "jotilla.h"
#include "date.h"
#include "palette.h"
#include "tags.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#define NOTE_READ_MAX (64u * 1024u * 1024u)
#define NOTE_TEXT_MAX (16u * 1024u * 1024u)
#define QUERY_TERMS_MAX 16u

static gboolean write_atomic(const char *path, const char *data, gsize length,
                             GError **err);

/* Modello INI minimale e senza escape, compatibile byte per byte con i file
   notes.ini UTF-16LE scritti da Windows e macOS. GKeyFile non va usato qui:
   interpreta le barre rovesciate e non preserva i valori grezzi. */
typedef struct {
    char *section;
    char *key;
    char *value;
    char *raw;
} IniEntry;

typedef struct {
    GPtrArray *entries;
} Ini;

static void ini_entry_free(gpointer data)
{
    IniEntry *entry = data;

    if (!entry)
        return;
    g_free(entry->section);
    g_free(entry->key);
    g_free(entry->value);
    g_free(entry->raw);
    g_free(entry);
}

static Ini *ini_new(void)
{
    Ini *ini = g_new0(Ini, 1);

    ini->entries = g_ptr_array_new_with_free_func(ini_entry_free);
    return ini;
}

static void ini_free(Ini *ini)
{
    if (!ini)
        return;
    g_ptr_array_free(ini->entries, TRUE);
    g_free(ini);
}

static gboolean ini_same(const char *left, const char *right)
{
    return g_ascii_strcasecmp(left ? left : "", right ? right : "") == 0;
}

static char *trim_copy(const char *start, gsize length)
{
    while (length && (*start == ' ' || *start == '\t' || *start == '\r'))
        ++start, --length;
    while (length && (start[length - 1] == ' ' || start[length - 1] == '\t' ||
                      start[length - 1] == '\r'))
        --length;
    return g_strndup(start, length);
}

static Ini *ini_parse(const char *text)
{
    Ini *ini = ini_new();
    const char *cursor = text ? text : "";
    char *current_section = NULL;

    while (*cursor) {
        const char *end = strchr(cursor, '\n');
        gsize length = end ? (gsize)(end - cursor) : strlen(cursor);
        const char *first;
        IniEntry *entry;
        char *line;

        if (length && cursor[length - 1] == '\r')
            --length;
        line = g_strndup(cursor, length);
        first = line;
        while (*first == ' ' || *first == '\t')
            ++first;
        entry = g_new0(IniEntry, 1);
        if (!*first || *first == ';' || *first == '#') {
            entry->raw = g_strdup(line);
        } else if (*first == '[') {
            const char *close = strchr(first, ']');

            if (close) {
                g_free(current_section);
                current_section = trim_copy(first + 1, (gsize)(close - first - 1));
                entry->section = g_strdup(current_section);
            } else {
                entry->raw = g_strdup(line);
            }
        } else {
            const char *equal = strchr(first, '=');

            if (equal) {
                entry->section = g_strdup(current_section ? current_section : "");
                entry->key = trim_copy(first, (gsize)(equal - first));
                entry->value = trim_copy(equal + 1, strlen(equal + 1));
            } else {
                entry->raw = g_strdup(line);
            }
        }
        g_ptr_array_add(ini->entries, entry);
        g_free(line);
        if (!end)
            break;
        cursor = end + 1;
    }
    g_free(current_section);
    return ini;
}

static const char *ini_value(Ini *ini, const char *section, const char *key)
{
    guint i;

    if (!ini)
        return NULL;
    for (i = 0; i < ini->entries->len; ++i) {
        IniEntry *entry = g_ptr_array_index(ini->entries, i);

        if (entry->key && ini_same(entry->section, section) &&
            ini_same(entry->key, key))
            return entry->value;
    }
    return NULL;
}

static void ini_set(Ini *ini, const char *section, const char *key,
                    const char *value)
{
    gssize insert_at = -1;
    gboolean have_section = FALSE;
    IniEntry *entry;
    guint i;

    for (i = 0; i < ini->entries->len; ++i) {
        entry = g_ptr_array_index(ini->entries, i);
        if (entry->key && ini_same(entry->section, section) &&
            ini_same(entry->key, key)) {
            g_free(entry->value);
            entry->value = g_strdup(value ? value : "");
            return;
        }
        if (entry->section && ini_same(entry->section, section)) {
            have_section = TRUE;
            insert_at = (gssize)i + 1;
        }
    }

    entry = g_new0(IniEntry, 1);
    entry->section = g_strdup(section);
    entry->key = g_strdup(key);
    entry->value = g_strdup(value ? value : "");
    if (have_section) {
        g_ptr_array_insert(ini->entries, (guint)insert_at, entry);
        return;
    }
    if (ini->entries->len) {
        IniEntry *last = g_ptr_array_index(ini->entries, ini->entries->len - 1);

        if (!(last->raw && last->raw[0] == '\0')) {
            IniEntry *blank = g_new0(IniEntry, 1);

            blank->raw = g_strdup("");
            g_ptr_array_add(ini->entries, blank);
        }
    }
    {
        IniEntry *header = g_new0(IniEntry, 1);

        header->section = g_strdup(section);
        g_ptr_array_add(ini->entries, header);
    }
    g_ptr_array_add(ini->entries, entry);
}

static void ini_remove_key(Ini *ini, const char *section, const char *key)
{
    gint i;

    if (!ini)
        return;
    for (i = (gint)ini->entries->len - 1; i >= 0; --i) {
        IniEntry *entry = g_ptr_array_index(ini->entries, (guint)i);

        if (entry->key && ini_same(entry->section, section) &&
            ini_same(entry->key, key))
            g_ptr_array_remove_index(ini->entries, (guint)i);
    }
}

static void ini_remove_section(Ini *ini, const char *section)
{
    gint i;

    if (!ini)
        return;
    for (i = (gint)ini->entries->len - 1; i >= 0; --i) {
        IniEntry *entry = g_ptr_array_index(ini->entries, (guint)i);

        if (entry->section && ini_same(entry->section, section))
            g_ptr_array_remove_index(ini->entries, (guint)i);
    }
}

static char *ini_serialize(Ini *ini)
{
    GString *text = g_string_new(NULL);
    guint i;

    for (i = 0; i < ini->entries->len; ++i) {
        IniEntry *entry = g_ptr_array_index(ini->entries, i);

        if (entry->key) {
            g_string_append(text, entry->key);
            g_string_append_c(text, '=');
            g_string_append(text, entry->value ? entry->value : "");
            g_string_append(text, "\r\n");
        } else if (entry->section) {
            g_string_append_c(text, '[');
            g_string_append(text, entry->section);
            g_string_append(text, "]\r\n");
        } else if (entry->raw) {
            g_string_append(text, entry->raw);
            g_string_append(text, "\r\n");
        } else {
            g_string_append(text, "\r\n");
        }
    }
    return g_string_free(text, FALSE);
}

/* Converte i byte INI (UTF-16LE/BE con BOM, UTF-8 con BOM o CP1252 legacy)
   nella rappresentazione UTF-8 usata internamente. */
static char *decode_ini(const char *data, gsize length, gsize *out_length)
{
    if (length >= 2 && (guchar)data[0] == 0xff && (guchar)data[1] == 0xfe)
        return g_convert(data + 2, (gssize)(length - 2), "UTF-8", "UTF-16LE",
                         NULL, out_length, NULL);
    if (length >= 2 && (guchar)data[0] == 0xfe && (guchar)data[1] == 0xff)
        return g_convert(data + 2, (gssize)(length - 2), "UTF-8", "UTF-16BE",
                         NULL, out_length, NULL);
    if (length >= 3 && (guchar)data[0] == 0xef && (guchar)data[1] == 0xbb &&
        (guchar)data[2] == 0xbf) {
        *out_length = length - 3;
        return g_strndup(data + 3, length - 3);
    }
    if (g_utf8_validate(data, (gssize)length, NULL)) {
        *out_length = length;
        return g_strndup(data, length);
    }
    return g_convert(data, (gssize)length, "UTF-8", "CP1252", NULL, out_length,
                     NULL);
}

static Ini *ini_load_file(const char *path)
{
    char *data = NULL;
    gsize length = 0;
    Ini *ini;
    GStatBuf info;

    if (!path || g_stat(path, &info) != 0 || info.st_size < 0 ||
        (guint64)info.st_size > NOTE_READ_MAX)
        return ini_new();
    if (!g_file_get_contents(path, &data, &length, NULL))
        return ini_new();
    {
        gsize text_length = 0;
        char *text = decode_ini(data, length, &text_length);

        ini = text ? ini_parse(text) : ini_new();
        g_free(text);
    }
    g_free(data);
    return ini;
}

static gboolean ini_write_file(Store *store, Ini *ini, GError **err)
{
    char *utf8 = ini_serialize(ini);
    gsize utf16_length = 0;
    char *utf16 = g_convert(utf8, (gssize)strlen(utf8), "UTF-16LE", "UTF-8",
                            NULL, &utf16_length, err);
    GString *bytes;
    gboolean ok;

    g_free(utf8);
    if (!utf16)
        return FALSE;
    bytes = g_string_sized_new(utf16_length + 2);
    g_string_append_c(bytes, (char)0xff);
    g_string_append_c(bytes, (char)0xfe);
    g_string_append_len(bytes, utf16, (gssize)utf16_length);
    ok = write_atomic(store->notes_ini, bytes->str, bytes->len, err);
    g_string_free(bytes, TRUE);
    g_free(utf16);
    return ok;
}

static gint ini_int(Ini *ini, const char *section, const char *key,
                    gint fallback)
{
    const char *value = ini_value(ini, section, key);
    char *end = NULL;
    gint64 parsed;

    if (!value || !*value)
        return fallback;
    parsed = g_ascii_strtoll(value, &end, 10);
    if (end == value)
        return fallback;
    if (parsed < G_MININT)
        return G_MININT;
    if (parsed > G_MAXINT)
        return G_MAXINT;
    return (gint)parsed;
}

static gboolean ini_bool(Ini *ini, const char *section, const char *key,
                         gboolean fallback)
{
    const char *value = ini_value(ini, section, key);

    if (!value || !*value)
        return fallback;
    if (!g_ascii_strcasecmp(value, "1") || !g_ascii_strcasecmp(value, "true") ||
        !g_ascii_strcasecmp(value, "yes") || !g_ascii_strcasecmp(value, "on"))
        return TRUE;
    if (!g_ascii_strcasecmp(value, "0") ||
        !g_ascii_strcasecmp(value, "false") ||
        !g_ascii_strcasecmp(value, "no") || !g_ascii_strcasecmp(value, "off"))
        return FALSE;
    return fallback;
}

static void ini_set_int(Ini *ini, const char *section, const char *key,
                        gint value)
{
    char *text = g_strdup_printf("%d", value);

    ini_set(ini, section, key, text);
    g_free(text);
}

static void ini_set_bool(Ini *ini, const char *section, const char *key,
                         gboolean value)
{
    ini_set(ini, section, key, value ? "1" : "0");
}

/* Il percorso dell'archivio e' locale alla macchina, non fa parte
   dell'archivio sincronizzato: vive accanto alla configurazione. */
static char *read_archive_path(const char *path)
{
    char *data = NULL;
    gsize length = 0;
    char *result;

    if (!path || !g_file_get_contents(path, &data, &length, NULL))
        return NULL;
    if (length >= 3 && (guchar)data[0] == 0xef && (guchar)data[1] == 0xbb &&
        (guchar)data[2] == 0xbf) {
        result = g_strndup(data + 3, length - 3);
    } else {
        result = g_strndup(data, length);
    }
    g_free(data);
    g_strstrip(result);
    if (!*result) {
        g_free(result);
        return NULL;
    }
    return result;
}

static gboolean write_archive_path(Store *store, const char *path,
                                   GError **err)
{
    return write_atomic(store->archive_path_file, path, strlen(path), err);
}

/* Contatore globale per il suffisso degli id delle note. */
static guint note_id_counter = 0;

/* Libera una nota e le sue stringhe. */
static void note_free(gpointer data)
{
    Note *note = (Note *)data;

    if (!note)
        return;
    g_free(note->id);
    g_free(note->text);
    g_free(note->title);
    g_free(note->rtf);
    g_free(note->tags);
    g_free(note);
}

/* Costruisce il percorso file di una nota per estensione. */
static char *note_file_path(const Store *store, const char *id, const char *extension)
{
    char *name = g_strdup_printf("%s.%s", id, extension);
    char *path = g_build_filename(store->archive_dir, name, NULL);

    g_free(name);
    return path;
}

/* Imposta errore GIO da errno. */
static void set_file_error(GError **err, int error_code, const char *action, const char *path)
{
    if (err && !*err)
        g_set_error(err, G_FILE_ERROR, g_file_error_from_errno(error_code),
                    "%s \"%s\": %s", action, path, g_strerror(error_code));
}

/* Scrive file in modo atomico con fallback diretto. */
static gboolean write_atomic(const char *path, const char *data, gsize length, GError **err)
{
    char *temporary = g_strconcat(path, ".tmp", NULL);
    GError *local = NULL;
    gboolean written = FALSE;

    if (g_file_set_contents(temporary, data ? data : "", (gssize)length, &local)) {
        if (g_rename(temporary, path) == 0)
            written = TRUE;
        else
            (void)g_unlink(temporary);
    } else {
        (void)g_unlink(temporary);
    }
    g_clear_error(&local);
    g_free(temporary);

    /* Fallback diretto se rename atomico fallisce. */
    if (!written)
        written = g_file_set_contents(path, data ? data : "", (gssize)length, err);
    return written;
}

/* Legge file, rimuovendo il BOM UTF-8 se richiesto. */
static char *read_file(const char *path, gboolean strip_bom)
{
    char *contents = NULL;
    gsize length = 0;
    GError *error = NULL;
    GStatBuf info;

    if (g_stat(path, &info) != 0)
        return NULL;
    if (info.st_size < 0 || (guint64)info.st_size > NOTE_READ_MAX)
        return NULL;

    if (!g_file_get_contents(path, &contents, &length, &error)) {
        g_clear_error(&error);
        return NULL;
    }
    if (strip_bom && length >= 3 &&
        (guchar)contents[0] == 0xef && (guchar)contents[1] == 0xbb &&
        (guchar)contents[2] == 0xbf) {
        char *stripped = g_strndup(contents + 3, length - 3);
        g_free(contents);
        contents = stripped;
    }
    if (!g_utf8_validate(contents, -1, NULL)) {
        g_free(contents);
        return NULL;
    }
    return contents;
}

/* Data di creazione dall'id Windows/macOS o, per gli UUID, dal file. */
static guint32 created_from_id(const char *id)
{
    unsigned year = 0, month = 0, day = 0;
    guint32 date;

    if (g_ascii_strncasecmp(id, "note-", 5) != 0 &&
        g_ascii_strncasecmp(id, "nota-", 5) != 0)
        return 0;
    if (sscanf(id + 5, "%4u-%2u-%2u", &year, &month, &day) != 3)
        return 0;
    date = year * 10000u + month * 100u + day;
    return date_valid(date) ? date : 0;
}

static guint32 created_from_file(const char *path)
{
    GFile *file = g_file_new_for_path(path);
    GFileInfo *info = g_file_query_info(
        file, G_FILE_ATTRIBUTE_TIME_CREATED "," G_FILE_ATTRIBUTE_TIME_MODIFIED,
        G_FILE_QUERY_INFO_NONE, NULL, NULL);
    guint64 stamp = 0;
    guint32 date = 0;

    if (info != NULL) {
        stamp = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_CREATED);
        if (stamp == 0)
            stamp = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
        g_object_unref(info);
    }
    g_object_unref(file);
    if (stamp != 0) {
        GDateTime *time = g_date_time_new_from_unix_local((gint64)stamp);

        if (time != NULL) {
            date = (guint32)(g_date_time_get_year(time) * 10000 +
                             g_date_time_get_month(time) * 100 +
                             g_date_time_get_day_of_month(time));
            g_date_time_unref(time);
        }
    }
    return date;
}

/* Converte CRLF e CR in LF, come fanno Windows e macOS durante la lettura. */
static void normalize_newlines(char *text)
{
    char *read = text;
    char *write = text;

    while (*read) {
        if (*read == '\r') {
            *write++ = '\n';
            ++read;
            if (*read == '\n')
                ++read;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static glong text_utf16_units(const char *text)
{
    glong units = 0;
    gunichar2 *utf16;

    if (text == NULL)
        return 0;
    utf16 = g_utf8_to_utf16(text, -1, NULL, &units, NULL);
    if (utf16 == NULL)
        return -1;
    g_free(utf16);
    return units;
}

static gboolean text_is_blank(const char *text)
{
    const char *cursor;

    if (!text)
        return TRUE;
    for (cursor = text; *cursor; cursor = g_utf8_next_char(cursor)) {
        if (!g_unichar_isspace(g_utf8_get_char(cursor)))
            return FALSE;
    }
    return TRUE;
}

/* Normalizza Unicode, esegue il case folding e rimuove i segni combinanti. */
static char *fold_text(const char *text)
{
    const char *cursor;
    char *normalized;
    char *folded;
    GString *output;

    if (!text)
        return g_strdup("");
    normalized = g_utf8_normalize(text, -1, G_NORMALIZE_DEFAULT);
    if (!normalized)
        return NULL;
    folded = g_utf8_casefold(normalized, -1);
    g_free(normalized);
    if (!folded)
        return NULL;

    output = g_string_sized_new(strlen(folded));
    for (cursor = folded; *cursor; cursor = g_utf8_next_char(cursor)) {
        gunichar ch = g_utf8_get_char(cursor);
        if (g_unichar_type(ch) != G_UNICODE_NON_SPACING_MARK)
            g_string_append_unichar(output, ch);
    }
    g_free(folded);
    return g_string_free(output, FALSE);
}

/* Alloca una nota con valori predefiniti. */
static Note *note_alloc(const char *id)
{
    Note *note = g_try_new0(Note, 1);

    if (!note)
        return NULL;
    note->id = g_strdup(id);
    note->text = g_strdup("");
    note->title = NULL;
    note->rtf = NULL;
    note->tags = g_strdup("");
    note->date = 0;
    note->x = 100;
    note->y = 100;
    note->w = 300;
    note->h = 240;
    note->palette = 0;
    note->color = 0;
    note->size = 1;
    note->opacity = 100;
    note->open = TRUE;
    note->collapsed = FALSE;
    note->has_title = FALSE;
    note->custom_ink = FALSE;
    return note;
}

/* Confronta note per id. */
/* Accetta gli id Windows (note-/nota-) e gli UUID creati da macOS. */
gboolean note_id_supported(const char *id)
{
    size_t length = strlen(id);
    size_t i;

    if (g_ascii_strncasecmp(id, "note-", 5) == 0 ||
        g_ascii_strncasecmp(id, "nota-", 5) == 0)
        return length > 5;
    if (length != 36 || id[8] != '-' || id[13] != '-' || id[18] != '-' ||
        id[23] != '-')
        return FALSE;
    for (i = 0; i < length; ++i) {
        if (id[i] == '-')
            continue;
        if (!g_ascii_isxdigit(id[i]))
            return FALSE;
    }
    return TRUE;
}

static gint note_id_compare(gconstpointer a, gconstpointer b)
{
    const Note *left = *(const Note * const *)a;
    const Note *right = *(const Note * const *)b;

    /* Windows/macOS mostrano prima le note piu' recenti. */
    if (left->created != right->created)
        return left->created < right->created ? 1 : -1;
    return g_strcmp0(right->id, left->id);
}

/* Confronta tag con collazione UTF-8. */
static gint tag_compare(gconstpointer a, gconstpointer b)
{
    const char *left = *(const char * const *)a;
    const char *right = *(const char * const *)b;

    return tags_compare(left, right);
}

/* Salva metadati di tutte le note in notes.ini (UTF-16LE con BOM). */
static gboolean save_ini(Store *store, GError **err)
{
    Ini *ini = ini_load_file(store->notes_ini);
    gboolean ok;
    guint i;

    ini_remove_key(ini, "app", "archive.path");
    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);

        ini_set_int(ini, note->id, "x", note->x);
        ini_set_int(ini, note->id, "y", note->y);
        ini_set_int(ini, note->id, "width", note->w);
        ini_set_int(ini, note->id, "height", note->h);
        ini_set_int(ini, note->id, "color", note->color);
        ini_set_int(ini, note->id, "palette", note->palette);
        ini_set_int(ini, note->id, "size", note->size);
        ini_set_int(ini, note->id, "opacity", note->opacity);
        ini_set_bool(ini, note->id, "open", note->open);
        ini_set_bool(ini, note->id, "collapsed", note->collapsed);
        ini_set_bool(ini, note->id, "has_title", note->has_title);
        ini_set_bool(ini, note->id, "custom_ink", note->custom_ink);
        ini_set_bool(ini, note->id, "scroll", note->scrolling);
        ini_set(ini, note->id, "tags", note->tags ? note->tags : "");
        {
            char date[16];

            g_snprintf(date, sizeof(date), "%08u", note->date);
            ini_set(ini, note->id, "calendar_date", date);
        }
        ini_remove_key(ini, note->id, "w");
        ini_remove_key(ini, note->id, "h");
    }
    ok = ini_write_file(store, ini, err);
    ini_free(ini);
    return ok;
}

/* Rimuove gruppo INI di una nota. */
static gboolean remove_ini_group(Store *store, const char *id, GError **err)
{
    Ini *ini;
    gboolean ok;

    if (!g_file_test(store->notes_ini, G_FILE_TEST_EXISTS))
        return TRUE;
    ini = ini_load_file(store->notes_ini);
    ini_remove_section(ini, id);
    ok = ini_write_file(store, ini, err);
    ini_free(ini);
    return ok;
}

/* Cancella file txt/rtf di una nota. */
static gboolean remove_note_files(Store *store, const char *id, GError **err)
{
    static const char *extensions[] = { "txt", "rtf" };
    gboolean ok = TRUE;
    int first_error = 0;
    guint i;

    for (i = 0; i < G_N_ELEMENTS(extensions); ++i) {
        char *path = note_file_path(store, id, extensions[i]);
        if (g_remove(path) != 0 && errno != ENOENT) {
            /* Memorizza primo errore. */
            if (ok)
                first_error = errno;
            ok = FALSE;
        }
        g_free(path);
    }

    if (!ok)
        set_file_error(err, first_error, "Failed to remove note files", store->archive_dir);
    return ok;
}

/* Individua la directory portatile tramite portable.flag. */
static char *portable_base_dir(void)
{
    char *exe = g_file_read_link("/proc/self/exe", NULL);

    if (exe) {
        char *dir = g_path_get_dirname(exe);
        char *flag = g_build_filename(dir, "portable.flag", NULL);
        gboolean found = g_file_test(flag, G_FILE_TEST_EXISTS);
        g_free(flag);
        g_free(exe);
        if (found)
            return dir;
        g_free(dir);
    }

    {
        char *cwd = g_get_current_dir();
        if (cwd) {
            char *flag = g_build_filename(cwd, "portable.flag", NULL);
            gboolean found = g_file_test(flag, G_FILE_TEST_EXISTS);
            g_free(flag);
            if (found)
                return cwd;
            g_free(cwd);
        }
    }
    return NULL;
}

/* Percorso archivio predefinito. */
char *store_default_archive_dir(void)
{
    char *base = portable_base_dir();

    if (base) {
        char *path = g_build_filename(base, "notes", NULL);
        g_free(base);
        return path;
    }
    return g_build_filename(g_get_user_data_dir(), "jotilla", "notes", NULL);
}

/* Percorso config predefinito. */
char *store_default_config_dir(void)
{
    char *base = portable_base_dir();

    if (base)
        return base;
    return g_build_filename(g_get_user_config_dir(), "jotilla", NULL);
}

/* Apre store e carica note da archivio. */
Store *store_open(const char *archive_dir, const char *config_dir, GError **err)
{
    Store *store;
    Ini *ini;
    GDir *directory;
    const char *name;
    gboolean explicit_dir = archive_dir != NULL && *archive_dir != '\0';
    char *configured = NULL;

    store = g_try_new0(Store, 1);
    if (!store) {
        g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Not enough memory");
        return NULL;
    }
    store->lock_fd = -1;
    store->session_palette = -1;
    store->config_dir = config_dir ? g_strdup(config_dir) : store_default_config_dir();
    store->portable = portable_base_dir() != NULL;
    store->archive_path_file = g_build_filename(store->config_dir, "archive.path", NULL);
    store->notes = g_ptr_array_new_with_free_func(note_free);

    if (!explicit_dir) {
        configured = read_archive_path(store->archive_path_file);
        if (!configured) {
            /* Migrazione dal layout Linux precedente: [app] archive.path. */
            char *legacy = g_build_filename(store->config_dir, "notes.ini", NULL);

            if (g_file_test(legacy, G_FILE_TEST_IS_REGULAR)) {
                Ini *old = ini_load_file(legacy);
                const char *value = ini_value(old, "app", "archive.path");

                if (value && *value)
                    configured = g_strdup(value);
                ini_free(old);
            }
            g_free(legacy);
        }
    }

    if (explicit_dir) {
        store->archive_dir = g_strdup(archive_dir);
    } else if (configured) {
        if (!g_file_test(configured, G_FILE_TEST_IS_DIR)) {
            g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_NOENT,
                        "The notes archive is unavailable: %s", configured);
            g_free(configured);
            store_free(store);
            return NULL;
        }
        store->archive_dir = g_strdup(configured);
    } else {
        store->archive_dir = store_default_archive_dir();
    }
    g_free(configured);

    if (g_mkdir_with_parents(store->config_dir, 0700) != 0) {
        int error_code = errno;
        set_file_error(err, error_code, "Failed to create directory", store->config_dir);
        store_free(store);
        return NULL;
    }
    if (g_mkdir_with_parents(store->archive_dir, 0700) != 0) {
        int error_code = errno;
        set_file_error(err, error_code, "Failed to create directory", store->archive_dir);
        store_free(store);
        return NULL;
    }

    /* Blocco compatibile con il file .jotila.lock di macOS. */
    {
        char *lock_path = g_build_filename(store->archive_dir, ".jotila.lock", NULL);
        int fd = open(lock_path, O_RDWR | O_CREAT, 0600);
        int error_code = errno;

        g_free(lock_path);
        if (fd < 0) {
            set_file_error(err, error_code, "Failed to open archive lock", store->archive_dir);
            store_free(store);
            return NULL;
        }
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            error_code = errno;
            close(fd);
            set_file_error(err, error_code, "The archive is already open", store->archive_dir);
            store_free(store);
            return NULL;
        }
        store->lock_fd = fd;
    }

    store->notes_ini = g_build_filename(store->archive_dir, "notes.ini", NULL);

    /* I metadati vivono dentro l'archivio, come su Windows e macOS. Migra il
       vecchio notes.ini locale alla prima apertura. */
    if (!g_file_test(store->notes_ini, G_FILE_TEST_EXISTS)) {
        char *legacy = g_build_filename(store->config_dir, "notes.ini", NULL);

        if (g_file_test(legacy, G_FILE_TEST_IS_REGULAR)) {
            Ini *old = ini_load_file(legacy);

            ini_remove_key(old, "app", "archive.path");
            (void)ini_write_file(store, old, NULL);
            ini_free(old);
        }
        g_free(legacy);
    }

    ini = ini_load_file(store->notes_ini);

    directory = g_dir_open(store->archive_dir, 0, err);
    if (!directory) {
        ini_free(ini);
        store_free(store);
        return NULL;
    }

    while ((name = g_dir_read_name(directory)) != NULL) {
        size_t name_length = strlen(name);
        char *path;
        char *id;
        char *text;
        char *rtf_path;
        char *rtf;
        guint32 created;
        Note *note;

        if (name_length < 5 || g_ascii_strcasecmp(name + name_length - 4, ".txt") != 0)
            continue;

        id = g_strndup(name, name_length - 4);
        if (strpbrk(id, "[]\r\n") || !note_id_supported(id)) {
            g_free(id);
            continue;
        }
        created = created_from_id(id);

        path = g_build_filename(store->archive_dir, name, NULL);
        if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            g_free(path);
            g_free(id);
            continue;
        }

        text = read_file(path, TRUE);
        if (created == 0)
            created = created_from_file(path);
        g_free(path);
        if (!text) {
            g_free(id);
            continue;
        }
        normalize_newlines(text);
        if (text_utf16_units(text) > (glong)NOTE_TEXT_MAX) {
            g_free(id);
            g_free(text);
            continue;
        }

        note = note_alloc(id);
        if (!note) {
            g_free(id);
            g_free(text);
            g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Not enough memory");
            g_dir_close(directory);
            ini_free(ini);
            store_free(store);
            return NULL;
        }

        g_free(note->text);
        note->text = text;
        note->created = created;

        g_free(note->tags);
        note->tags = g_strdup(ini_value(ini, id, "tags") ? ini_value(ini, id, "tags") : "");
        note->x = ini_int(ini, id, "x", 100);
        note->y = ini_int(ini, id, "y", 100);
        note->w = ini_int(ini, id, "width", ini_int(ini, id, "w", 300));
        note->h = ini_int(ini, id, "height", ini_int(ini, id, "h", 240));
        note->color = ini_int(ini, id, "color", 0);
        note->palette = ini_int(ini, id, "palette", 0);
        note->size = ini_int(ini, id, "size", 1);
        note->opacity = ini_int(ini, id, "opacity", 100);
        note->open = ini_bool(ini, id, "open", TRUE);
        note->collapsed = ini_bool(ini, id, "collapsed", FALSE);
        note->has_title = ini_bool(ini, id, "has_title", FALSE);
        note->custom_ink = ini_bool(ini, id, "custom_ink", FALSE);
        note->scrolling = ini_bool(ini, id, "scroll", FALSE);
        if (note->palette < 0 || note->palette >= PALETTE_COUNT)
            note->palette = 0;
        if (note->color < 0 || note->color >= PALETTE_SIZES[note->palette])
            note->color = 0;
        if (note->size < 0 || note->size > 4)
            note->size = 1;
        if (note->opacity < 60 || note->opacity > 100)
            note->opacity = 100;
        if (note->w < 160)
            note->w = 300;
        if (note->w > 4096)
            note->w = 4096;
        if (note->h < 100)
            note->h = 240;
        if (note->h > 4096)
            note->h = 4096;
        if (note->has_title && note->text && note->text[0] == '#') {
            char *body = note->text;
            size_t end = strcspn(body, "\n");
            size_t after = end;
            size_t start = 1, finish = end;
            char *title;

            while (start < finish && (body[start] == ' ' || body[start] == '\t'))
                ++start;
            while (finish > start && (body[finish - 1] == ' ' || body[finish - 1] == '\t'))
                --finish;
            title = g_strndup(body + start, finish - start);
            if (body[after] == '\n')
                ++after;
            g_free(note->title);
            note->title = title;
            {
                char *new_text = g_strdup(body + after);

                g_free(note->text);
                note->text = new_text ? new_text : g_strdup("");
            }
            if (!*note->title) {
                g_free(note->title);
                note->title = NULL;
                note->has_title = FALSE;
            }
        } else {
            g_free(note->title);
            note->title = NULL;
            note->has_title = FALSE;
        }
        {
            gint date = ini_int(ini, id, "calendar_date", 0);

            note->date = date > 0 && date_valid((unsigned)date) ? (guint32)date : 0;
        }

        /* Windows elimina i file delle note vuote al caricamento. */
        if (text_is_blank(note->text) &&
            (note->title == NULL || text_is_blank(note->title))) {
            (void)remove_note_files(store, id, NULL);
            note_free(note);
            g_free(id);
            continue;
        }

        rtf_path = note_file_path(store, id, "rtf");
        rtf = read_file(rtf_path, FALSE);
        g_free(rtf_path);
        if (rtf) {
            g_free(note->rtf);
            note->rtf = rtf;
        }

        g_ptr_array_add(store->notes, note);
        g_free(id);
    }

    g_dir_close(directory);
    ini_free(ini);
    g_ptr_array_sort(store->notes, note_id_compare);
    return store;
}

/* Libera store e note. */
void store_free(Store *store)
{
    if (!store)
        return;
    if (store->notes)
        g_ptr_array_free(store->notes, TRUE);
    if (store->lock_fd >= 0) {
        (void)flock(store->lock_fd, LOCK_UN);
        close(store->lock_fd);
    }
    g_free(store->archive_dir);
    g_free(store->config_dir);
    g_free(store->notes_ini);
    g_free(store->archive_path_file);
    g_free(store);
}

/* Sacchetto mescolato dei colori della palette attiva. */
static int shuffle_bag[COLOR_COUNT];
static int shuffle_left = 0;
static int shuffle_palette = -1;

/* Limita un indice di palette all'intervallo valido. */
static int palette_clamp(int palette)
{
    if (palette < 0)
        return 0;
    if (palette >= PALETTE_COUNT)
        return PALETTE_COUNT - 1;
    return palette;
}

/* Estrae un colore dal sacchetto Fisher-Yates della palette indicata. */
static int draw_shuffled_color(int palette)
{
    int size = PALETTE_SIZES[palette];
    int i;

    if (size < 1)
        size = 1;
    if (size > COLOR_COUNT)
        size = COLOR_COUNT;
    if (palette != shuffle_palette || shuffle_left <= 0) {
        for (i = 0; i < size; ++i)
            shuffle_bag[i] = i;
        for (i = size - 1; i > 0; --i) {
            int j = g_random_int_range(0, i + 1);
            int swap = shuffle_bag[i];
            shuffle_bag[i] = shuffle_bag[j];
            shuffle_bag[j] = swap;
        }
        shuffle_left = size;
        shuffle_palette = palette;
    }
    --shuffle_left;
    return shuffle_bag[shuffle_left];
}

/* Crea nuova nota con id univoco. */
Note *store_new(Store *store, const char *text)
{
    GDateTime *now;
    char *stamp;
    char *id = NULL;
    Note *note;
    guint position;
    guint attempt;

    if (!store)
        return NULL;

    now = g_date_time_new_now_local();
    if (!now)
        return NULL;
    stamp = g_date_time_format(now, "%Y-%m-%d_%H-%M-%S");
    if (!stamp) {
        g_date_time_unref(now);
        return NULL;
    }

    /* Tenta id univoco con contatore a 3 cifre, evitando collisioni. */
    for (attempt = 0; attempt < 1000; ++attempt) {
        char *candidate;
        char *text_path;
        char *rtf_path;
        gboolean taken;
        guint suffix = note_id_counter;

        note_id_counter = (note_id_counter + 1) % 1000;
        candidate = g_strdup_printf("note-%s-%03u-%03u", stamp,
                                    (guint)(g_date_time_get_microsecond(now) / 1000),
                                    suffix);
        text_path = note_file_path(store, candidate, "txt");
        rtf_path = note_file_path(store, candidate, "rtf");
        taken = store_get(store, candidate) != NULL ||
                g_file_test(text_path, G_FILE_TEST_EXISTS) ||
                g_file_test(rtf_path, G_FILE_TEST_EXISTS);
        g_free(text_path);
        g_free(rtf_path);
        if (!taken) {
            id = candidate;
            break;
        }
        g_free(candidate);
    }

    g_free(stamp);
    g_date_time_unref(now);
    if (!id)
        return NULL;

    note = note_alloc(id);
    if (!note) {
        g_free(id);
        return NULL;
    }
    g_free(note->text);
    note->text = g_strdup(text ? text : "");
    {
        GDateTime *today = g_date_time_new_now_local();

        if (today != NULL) {
            note->created = (guint32)(g_date_time_get_year(today) * 10000 +
                                      g_date_time_get_month(today) * 100 +
                                      g_date_time_get_day_of_month(today));
            g_date_time_unref(today);
        }
    }

    {
        int palette = palette_clamp(store_app_get_int(store, "palette", 0));
        int favorite = store_app_get_int(store, "favorite_palette", -1);
        int session = store->session_palette;
        int next = store_app_get_int(store, "next_color", 0);
        int size;

        if (next < 0 || next >= COLOR_COUNT)
            next = 0;
        note->x = 100 + next % 7 * 28;
        note->y = 100 + next % 7 * 28;
        if (session >= 0 && session < PALETTE_COUNT) {
            palette = session;
            size = PALETTE_SIZES[palette];
            if (size < 1)
                size = 1;
            note->color = next % size;
        } else if (favorite >= 0 && favorite < PALETTE_COUNT) {
            palette = favorite;
            note->color = draw_shuffled_color(palette);
        } else {
            size = PALETTE_SIZES[palette];
            if (size < 1)
                size = 1;
            note->color = next % size;
        }
        note->palette = palette;
        note->scrolling = store_app_get_int(store, "scroll", 0) != 0;
        store_app_set_int(store, "next_color", (next + 1) % COLOR_COUNT);
    }

    position = store->notes->len;
    for (guint i = 0; i < store->notes->len; ++i) {
        Note *existing = g_ptr_array_index(store->notes, i);
        gboolean after = existing->created != note->created
                             ? existing->created < note->created
                             : g_strcmp0(existing->id, note->id) < 0;

        if (after) {
            position = i;
            break;
        }
    }
    g_ptr_array_insert(store->notes, position, note);
    g_free(id);

    (void)store_save(store, note, NULL);
    return note;
}

/* Cerca nota per id. */
Note *store_get(Store *store, const char *id)
{
    guint i;

    if (!store || !id)
        return NULL;
    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);
        if (g_strcmp0(note->id, id) == 0)
            return note;
    }
    return NULL;
}

/* Salva nota su disco e metadati. */
/* Serializza il corpo in UTF-8 con CRLF, come Windows e macOS. */
static char *encode_txt(const Note *note)
{
    GString *content = g_string_new(NULL);
    const char *cursor;

    if (note->has_title && note->title) {
        g_string_append_c(content, '#');
        g_string_append(content, note->title);
        g_string_append(content, "\r\n");
    }
    for (cursor = note->text ? note->text : ""; *cursor; ++cursor) {
        if (*cursor == '\r') {
            g_string_append(content, "\r\n");
            if (cursor[1] == '\n')
                ++cursor;
        } else if (*cursor == '\n') {
            g_string_append(content, "\r\n");
        } else {
            g_string_append_c(content, *cursor);
        }
    }
    return g_string_free(content, FALSE);
}

gboolean store_save(Store *store, Note *note, GError **err)
{
    char *path;
    gboolean ok;

    if (!store || !note || !note->id) {
        g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid note");
        return FALSE;
    }

    if (note->text && text_utf16_units(note->text) > (glong)NOTE_TEXT_MAX) {
        g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                            "Note text exceeds 16 MB");
        return FALSE;
    }

    /* RTF prima del TXT: se la coppia viene interrotta, il lettore scarta
       l'RTF non coerente e usa il TXT autorevole. */
    if (note->rtf) {
        path = note_file_path(store, note->id, "rtf");
        ok = write_atomic(path, note->rtf, strlen(note->rtf), err);
        g_free(path);
        if (!ok)
            return FALSE;
    }

    {
        char *content = encode_txt(note);

        path = note_file_path(store, note->id, "txt");
        ok = write_atomic(path, content, strlen(content), err);
        g_free(content);
        g_free(path);
    }
    if (!ok)
        return FALSE;

    return save_ini(store, err);
}

/* Elimina nota e suoi file. */
gboolean store_delete(Store *store, const char *id, GError **err)
{
    gssize index = -1;
    guint i;

    if (!store || !id) {
        g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid note");
        return FALSE;
    }

    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);
        if (g_strcmp0(note->id, id) == 0) {
            index = (gssize)i;
            break;
        }
    }

    if (index < 0)
        return FALSE;

    if (!remove_note_files(store, id, err))
        return FALSE;

    (void)g_ptr_array_remove_index(store->notes, (guint)index);
    return remove_ini_group(store, id, err);
}

typedef struct {
    gboolean is_tag;
    gboolean untagged;
    char *value;
} SearchTerm;

/* Libera termine di ricerca. */
static void search_term_free(gpointer data)
{
    SearchTerm *term = (SearchTerm *)data;

    if (!term)
        return;
    g_free(term->value);
    g_free(term);
}

/* Analizza query in termini di ricerca. */
static gboolean query_parse(const char *query, GPtrArray *terms, GError **err)
{
    const char *cursor = query ? query : "";

    if (query != NULL) {
        glong units = 0;
        gunichar2 *utf16 = g_utf8_to_utf16(query, -1, NULL, &units, NULL);

        if (utf16 == NULL || units >= 512) {
            g_free(utf16);
            g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                                "Search is too long");
            return FALSE;
        }
        g_free(utf16);
    }

    while (*cursor) {
        SearchTerm *term;
        GString *value;
        char *raw;

        while (*cursor && g_ascii_isspace(*cursor))
            ++cursor;
        if (!*cursor)
            break;
        /* Oltre i 16 termini la query e' invalida, come su Windows e macOS. */
        if (terms->len >= QUERY_TERMS_MAX) {
            g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                                "Too many search terms");
            return FALSE;
        }

        term = g_try_new0(SearchTerm, 1);
        if (!term) {
            g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Not enough memory");
            return FALSE;
        }

        if (cursor[0] == '-' && cursor[1] == '#' &&
            (cursor[2] == '\0' || g_ascii_isspace(cursor[2]))) {
            term->untagged = TRUE;
            cursor += 2;
            g_ptr_array_add(terms, term);
            continue;
        }

        if (*cursor == '#') {
            term->is_tag = TRUE;
            ++cursor;
        }

        value = g_string_new(NULL);
        if (*cursor == '"') {
            ++cursor;
            while (*cursor && *cursor != '"') {
                g_string_append_c(value, *cursor);
                ++cursor;
            }
            if (*cursor == '"')
                ++cursor;
        } else {
            while (*cursor && !g_ascii_isspace(*cursor)) {
                g_string_append_c(value, *cursor);
                ++cursor;
            }
        }

        if (value->len == 0) {
            g_string_free(value, TRUE);
            search_term_free(term);
            g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Empty search term");
            return FALSE;
        }

        raw = g_string_free(value, FALSE);
        if (term->is_tag) {
            char *normalized = tags_normalize(raw);
            g_free(raw);
            if (!normalized || !*normalized || strchr(normalized, ';')) {
                g_free(normalized);
                search_term_free(term);
                g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid tag in search");
                return FALSE;
            }
            term->value = normalized;
        } else {
            term->value = fold_text(raw);
            g_free(raw);
            if (!term->value || !*term->value) {
                g_free(term->value);
                search_term_free(term);
                g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Empty search term");
                return FALSE;
            }
        }

        g_ptr_array_add(terms, term);
    }

    return TRUE;
}

/* Cerca note per query/tag. */
GPtrArray *store_search(Store *store, const char *query, const char *tag, GError **err)
{
    GPtrArray *terms;
    GPtrArray *results;
    guint i;

    if (!store) {
        g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid store");
        return NULL;
    }

    terms = g_ptr_array_new_with_free_func(search_term_free);
    if (!query_parse(query, terms, err)) {
        g_ptr_array_free(terms, TRUE);
        return NULL;
    }

    results = g_ptr_array_new();
    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);
        char *folded = NULL;
        gboolean matched = TRUE;
        guint j;

        if (tag && !tags_contains(note->tags ? note->tags : "", tag))
            continue;

        for (j = 0; j < terms->len; ++j) {
            SearchTerm *term = g_ptr_array_index(terms, j);

            if (term->untagged) {
                if (note->tags && note->tags[0]) {
                    matched = FALSE;
                    break;
                }
            } else if (term->is_tag) {
                if (!tags_contains(note->tags ? note->tags : "", term->value)) {
                    matched = FALSE;
                    break;
                }
            } else {
                /* Normalizza il testo solo al primo termine testuale. */
                if (!folded)
                    folded = fold_text(note->text ? note->text : "");
                if (!folded || !strstr(folded, term->value)) {
                    matched = FALSE;
                    break;
                }
            }
        }

        g_free(folded);
        if (matched)
            g_ptr_array_add(results, note);
    }

    g_ptr_array_free(terms, TRUE);
    return results;
}

/* Catalogo tag unici ordinati. */
char *store_tags_catalog(Store *store)
{
    GPtrArray *tags;
    GHashTable *seen;
    GString *text;
    char *result;
    guint i;

    if (!store)
        return g_strdup("");

    tags = g_ptr_array_new_with_free_func(g_free);
    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);
        char **parts;
        guint j;

        if (!note->tags || !note->tags[0])
            continue;
        parts = g_strsplit(note->tags, ";", -1);
        for (j = 0; parts[j]; ++j) {
            if (!parts[j][0] || g_hash_table_contains(seen, parts[j]))
                continue;
            g_hash_table_add(seen, g_strdup(parts[j]));
            g_ptr_array_add(tags, g_strdup(parts[j]));
        }
        g_strfreev(parts);
    }

    g_ptr_array_sort(tags, tag_compare);

    text = g_string_new(NULL);
    for (i = 0; i < tags->len; ++i) {
        if (i)
            g_string_append_c(text, '\n');
        g_string_append(text, g_ptr_array_index(tags, i));
    }
    if (text->len)
        result = g_string_free(text, FALSE);
    else {
        g_string_free(text, TRUE);
        result = g_strdup("");
    }

    g_hash_table_destroy(seen);
    g_ptr_array_free(tags, TRUE);
    return result;
}

/* Conteggio dei tag in formato "tag\tconteggio", ordinato per tag. */
char *store_tag_counts(Store *store)
{
    GHashTable *counts;
    GHashTableIter iter;
    GPtrArray *tags;
    gpointer key;
    gpointer value;
    GString *text;
    char *result;
    guint i;

    if (!store)
        return g_strdup("");

    counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);
        char **parts;
        guint j;

        if (!note->tags || !note->tags[0])
            continue;
        parts = g_strsplit(note->tags, ";", -1);
        for (j = 0; parts[j]; ++j) {
            gint current;
            if (!parts[j][0])
                continue;
            current = GPOINTER_TO_INT(g_hash_table_lookup(counts, parts[j]));
            g_hash_table_replace(counts, g_strdup(parts[j]),
                                 GINT_TO_POINTER(current + 1));
        }
        g_strfreev(parts);
    }

    tags = g_ptr_array_new_with_free_func(g_free);
    g_hash_table_iter_init(&iter, counts);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        (void)value;
        g_ptr_array_add(tags, g_strdup((const char *)key));
    }
    g_ptr_array_sort(tags, tag_compare);

    text = g_string_new(NULL);
    for (i = 0; i < tags->len; ++i) {
        const char *name = g_ptr_array_index(tags, i);
        gint count = GPOINTER_TO_INT(g_hash_table_lookup(counts, name));
        if (i)
            g_string_append_c(text, '\n');
        g_string_append_printf(text, "%s\t%d", name, count);
    }
    if (text->len)
        result = g_string_free(text, FALSE);
    else {
        g_string_free(text, TRUE);
        result = g_strdup("");
    }

    g_ptr_array_free(tags, TRUE);
    g_hash_table_destroy(counts);
    return result;
}

/* Legge un intero dalla sezione [app] di notes.ini. */
int store_app_get_int(Store *store, const char *key, int fallback)
{
    Ini *ini;
    gint value;

    if (!store || !key)
        return fallback;
    ini = ini_load_file(store->notes_ini);
    value = ini_int(ini, "app", key, fallback);
    ini_free(ini);
    return value;
}

/* Scrive un intero nella sezione [app], conservando le altre chiavi. */
void store_app_set_int(Store *store, const char *key, int value)
{
    Ini *ini;

    if (!store || !key)
        return;
    ini = ini_load_file(store->notes_ini);
    ini_set_int(ini, "app", key, value);
    (void)ini_write_file(store, ini, NULL);
    ini_free(ini);
}

/* Legge una stringa dalla sezione [app] di notes.ini. */
char *store_app_get_string(Store *store, const char *key)
{
    Ini *ini;
    const char *value;
    char *result = NULL;

    if (!store || !key)
        return NULL;
    ini = ini_load_file(store->notes_ini);
    value = ini_value(ini, "app", key);
    if (value)
        result = g_strdup(value);
    ini_free(ini);
    return result;
}

/* Scrive una stringa nella sezione [app], conservando le altre chiavi. */
gboolean store_app_set_string(Store *store, const char *key, const char *value)
{
    Ini *ini;
    gboolean ok;

    if (!store || !key)
        return FALSE;
    ini = ini_load_file(store->notes_ini);
    ini_set(ini, "app", key, value ? value : "");
    ok = ini_write_file(store, ini, NULL);
    ini_free(ini);
    return ok;
}

/* Il percorso dell'archivio resta nella configurazione locale della macchina. */
void store_set_archive_path(Store *store, const char *path)
{
    Ini *ini;

    if (!store || !path)
        return;
    (void)write_archive_path(store, path, NULL);
    ini = ini_load_file(store->notes_ini);
    ini_remove_key(ini, "app", "archive.path");
    (void)ini_write_file(store, ini, NULL);
    ini_free(ini);
}

/* Imposta tag normalizzati e salva. */
gboolean store_set_tags(Store *store, Note *note, const char *raw)
{
    char *normalized;

    if (!store || !note)
        return FALSE;
    normalized = tags_normalize(raw ? raw : "");
    if (!normalized)
        return FALSE;
    g_free(note->tags);
    note->tags = normalized;
    (void)store_save(store, note, NULL);
    return TRUE;
}

/* Imposta data calendario e salva. */
void store_set_date(Store *store, Note *note, guint32 date)
{
    if (!store || !note)
        return;
    if (text_is_blank(note->text) &&
        (note->title == NULL || text_is_blank(note->title)))
        return;
    note->date = date;
    (void)store_save(store, note, NULL);
}

/* Restituisce directory archivio. */
const char *store_archive_dir(Store *store)
{
    return store ? store->archive_dir : NULL;
}
