/* Strutture Note/Store e API pubbliche del modulo store. */
/* Strutture Note/Store e API dello store note. */

#ifndef JOTILLA_H
#define JOTILLA_H

#include <glib.h>

typedef struct {
    char *id;
    char *text;       /* corpo della nota (senza il prefisso #titolo) */
    char *title;      /* titolo promosso (NULL se nessuno) */
    char *rtf;
    char *tags;
    guint32 date;
    guint32 created;
    int x, y, w, h;
    int palette;
    int color;
    int size;
    int opacity;
    gboolean open;
    gboolean collapsed;
    gboolean has_title; /* TRUE se il titolo e' stato promosso */
    gboolean custom_ink; /* TRUE se il colore testo e' stato scelto dall'utente */
    gboolean scrolling; /* TRUE se il testo puo' superare l'area visibile */
} Note;

typedef struct {
    char *archive_dir;
    char *config_dir;
    char *notes_ini;
    char *archive_path_file;
    int lock_fd;
    gboolean portable;
    int session_palette;
    GPtrArray *notes;
} Store;

/* Apre lo store note. */
/* Apre store e carica note da archivio. */
Store *store_open(const char *archive_dir, const char *config_dir, GError **err);
/* Libera lo store. */
/* Libera store e note. */
void store_free(Store *store);
/* Crea una nuova nota. */
/* Crea nuova nota con id univoco. */
Note *store_new(Store *store, const char *text);
/* Recupera nota per id. */
/* Cerca nota per id. */
Note *store_get(Store *store, const char *id);
/* Salva una nota. */
/* Salva nota su disco e metadati. */
gboolean store_save(Store *store, Note *note, GError **err);
/* Elimina una nota. */
/* Elimina nota e suoi file. */
gboolean store_delete(Store *store, const char *id, GError **err);
/* Cerca note per query/tag. */
/* Cerca note per query/tag. */
GPtrArray *store_search(Store *store, const char *query, const char *tag, GError **err);
/* Catalogo dei tag. */
/* Catalogo tag unici ordinati. */
char *store_tags_catalog(Store *store);
char *store_tag_counts(Store *store);
int store_app_get_int(Store *store, const char *key, int fallback);
void store_app_set_int(Store *store, const char *key, int value);
char *store_app_get_string(Store *store, const char *key);
gboolean store_app_set_string(Store *store, const char *key, const char *value);
gboolean note_id_supported(const char *id);
void store_set_archive_path(Store *store, const char *path);
/* Imposta i tag di una nota. */
/* Imposta tag normalizzati e salva. */
gboolean store_set_tags(Store *store, Note *note, const char *raw);
/* Imposta la data di una nota. */
/* Imposta data calendario e salva. */
void store_set_date(Store *store, Note *note, guint32 date);
/* Directory archivio. */
/* Restituisce directory archivio. */
const char *store_archive_dir(Store *store);
/* Directory archivio predefinita. */
/* Percorso archivio predefinito. */
char *store_default_archive_dir(void);
/* Directory config predefinita. */
/* Percorso config predefinito. */
char *store_default_config_dir(void);

#endif
