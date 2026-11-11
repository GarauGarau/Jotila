/* Dichiarazioni per la gestione delle palette. */

#ifndef JOTILLA_PALETTE_H
#define JOTILLA_PALETTE_H

#include <stddef.h>
#include <wchar.h>

#define COLOR_COUNT 6
#define DEFAULT_PALETTE_COUNT 3
enum { POPULAR_PALETTE_COUNT = 0
#define POPULAR_PALETTE(...) + 1
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};
#define PALETTE_COUNT (DEFAULT_PALETTE_COUNT + POPULAR_PALETTE_COUNT)

typedef unsigned int COLORREF;
#define RGB(r, g, b) ((COLORREF)((unsigned char)(r) | ((unsigned char)(g) << 8) | ((unsigned char)(b) << 16)))
#define GetRValue(c) ((unsigned char)(c))
#define GetGValue(c) ((unsigned char)(((c) >> 8) & 0xff))
#define GetBValue(c) ((unsigned char)(((c) >> 16) & 0xff))

/* Persisted indices are append-only. The first three palettes and their six
   slots retain the original new-note sequence, including monochrome repeats. */
extern const COLORREF PALETTES[PALETTE_COUNT][COLOR_COUNT];
extern const wchar_t *const PALETTE_NAMES[PALETTE_COUNT];
extern const unsigned char PALETTE_SIZES[PALETTE_COUNT];
extern const unsigned char PALETTE_LIGHT_INK[PALETTE_COUNT];
extern const unsigned char PALETTE_BAR_LIGHT_INK[PALETTE_COUNT];
extern const unsigned char POPULAR_PALETTES[POPULAR_PALETTE_COUNT];
extern const signed char PALETTE_POPULAR_INDEX[PALETTE_COUNT];

/* Nome della palette. */
const char *palette_name(int palette);
/* Colore esadecimale. */
const char *palette_color_hex(int palette, int color);
/* Colore inchiostro. */
const char *palette_ink_hex(int palette, int color);
/* Colore barra (scurito). */
const char *palette_bar_hex(int palette, int color);
/* Colore predefinito per nota. */
int palette_default_for_note(int index);

#endif
