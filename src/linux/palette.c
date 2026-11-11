/* Definizioni delle palette colori e accesso ai colori. */

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "palette.h"

#define HEX(n) RGB(((n) >> 16) & 255, ((n) >> 8) & 255, (n) & 255)

const COLORREF PALETTES[PALETTE_COUNT][COLOR_COUNT] = {
    {RGB(255, 238, 151), RGB(199, 229, 255), RGB(213, 239, 187), RGB(255, 210, 218), RGB(229, 214, 255), RGB(255, 222, 180)},
    {RGB(255, 255, 255), RGB(28, 28, 28), RGB(255, 255, 255), RGB(28, 28, 28), RGB(255, 255, 255), RGB(28, 28, 28)},
    {RGB(255, 238, 151), RGB(255, 224, 112), RGB(255, 245, 184), RGB(250, 218, 128), RGB(255, 239, 105), RGB(255, 250, 205)},
#define POPULAR_PALETTE(id, order, source, name, ink, bar, a, b, c, d, e) [id] = {HEX(a), HEX(b), HEX(c), HEX(d), HEX(e)},
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};

const wchar_t *const PALETTE_NAMES[PALETTE_COUNT] = {
    L"Pastel", L"Black and white", L"Yellow only",
#define POPULAR_PALETTE(id, order, source, name, ink, bar, a, b, c, d, e) [id] = name,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};

const unsigned char PALETTE_SIZES[PALETTE_COUNT] = {
    6, 6, 6,
#define POPULAR_PALETTE(id, order, source, name, ink, bar, a, b, c, d, e) [id] = 5,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};

const unsigned char PALETTE_LIGHT_INK[PALETTE_COUNT] = {
    0, 42, 0,
#define POPULAR_PALETTE(id, order, source, name, ink, bar, a, b, c, d, e) [id] = ink,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};

const unsigned char PALETTE_BAR_LIGHT_INK[PALETTE_COUNT] = {
    0, 42, 0,
#define POPULAR_PALETTE(id, order, source, name, ink, bar, a, b, c, d, e) [id] = bar,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};

const unsigned char POPULAR_PALETTES[POPULAR_PALETTE_COUNT] = {
#define POPULAR_PALETTE(id, order, source, name, ink, bar, a, b, c, d, e) [order] = id,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};

const signed char PALETTE_POPULAR_INDEX[PALETTE_COUNT] = {
    -1, -1, -1,
#define POPULAR_PALETTE(id, order, source, name, ink, bar, a, b, c, d, e) [id] = order,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};

/* Limita l'indice palette all'intervallo valido (default 0). */
static int clamp_palette(int palette) {
    if (palette < 0 || palette >= PALETTE_COUNT) return 0;
    return palette;
}

/* Limita l'indice colore all'intervallo valido (default 0). */
static int clamp_color(int color) {
    if (color < 0 || color >= COLOR_COUNT) return 0;
    return color;
}

/* Nome della palette. */
const char *palette_name(int palette) {
    static char names[PALETTE_COUNT][128];
    int p = clamp_palette(palette);
    if (names[p][0] == '\0') {
        const wchar_t *w = PALETTE_NAMES[p];
        size_t i = 0;
        while (w[i] != 0 && i + 1 < sizeof(names[p])) {
            names[p][i] = (char)w[i];
            ++i;
        }
        names[p][i] = '\0';
    }
    return names[p];
}

/* Colore esadecimale della palette/colore. */
const char *palette_color_hex(int palette, int color) {
    static char hexbuf[PALETTE_COUNT][COLOR_COUNT][8];
    int p = clamp_palette(palette), c = clamp_color(color);
    if (hexbuf[p][c][0] == '\0') {
        COLORREF rgb = PALETTES[p][c];
        unsigned r = GetRValue(rgb);
        unsigned g = GetGValue(rgb);
        unsigned b = GetBValue(rgb);
        g_snprintf(hexbuf[p][c], sizeof(hexbuf[p][c]), "#%02x%02x%02x", r, g, b);
    }
    return hexbuf[p][c];
}

/* Colore inchiostro: bianco se il bit della maschera è attivo. */
const char *palette_ink_hex(int palette, int color) {
    unsigned mask = PALETTE_LIGHT_INK[clamp_palette(palette)];
    return (mask & (1u << clamp_color(color))) ? "#ffffff" : "#000000";
}

/* Colore scurito per la barra del titolo (cache statica). */
const char *palette_bar_hex(int palette, int color) {
    static char bar[PALETTE_COUNT][COLOR_COUNT][8];
    int p = clamp_palette(palette), c = clamp_color(color);
    if (bar[p][c][0] == '\0') {
        COLORREF rgb = PALETTES[p][c];
        unsigned r = GetRValue(rgb) * 94 / 100;
        unsigned g = GetGValue(rgb) * 94 / 100;
        unsigned b = GetBValue(rgb) * 94 / 100;
        g_snprintf(bar[p][c], sizeof(bar[p][c]), "#%02x%02x%02x", r, g, b);
    }
    return bar[p][c];
}

/* Colore predefinito per una nota in base all'indice. */
int palette_default_for_note(int index) {
    int value = index % COLOR_COUNT;
    if (value < 0) value = 0;
    if (value >= COLOR_COUNT) value = COLOR_COUNT - 1;
    return value;
}
