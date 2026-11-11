/* Dichiarazioni per la gestione dei tag. */

#ifndef JOTILLA_TAGS_H
#define JOTILLA_TAGS_H

#include <glib.h>

/* Normalizza una stringa di tag. */
char *tags_normalize(const char *input);
/* Confronto ordinale (come Windows CompareStringOrdinal / macOS compare). */
gint tags_compare(const char *left, const char *right);
/* Verifica la presenza di un tag. */
gboolean tags_contains(const char *tags, const char *name);
/* Suddivide i tag in array. */
GPtrArray *tags_split(const char *tags);
/* Suggerisce tag per prefisso dal catalogo "tag\tconteggio". */
GPtrArray *tags_suggest_from(const char *counts, const char *prefix, const char *assigned, int max);

#endif
