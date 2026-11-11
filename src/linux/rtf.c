/* Conversione tra GtkTextBuffer e formato RTF. */
/* Parsing subset RTF e generazione RTF da buffer. */
#include "rtf.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RTF_COLOR_EPSILON 0.0001
/* Dimensione predefinita in half-points RTF (20 = 10pt). */
#define RTF_DEFAULT_SIZE_HALF 20

typedef struct {
    gboolean bold;
    gboolean italic;
    gboolean underline;
    gboolean strike;
    gboolean has_color;
    GdkRGBA color;
    int size_half;
} CharAttr;

typedef struct {
    gboolean bold;
    gboolean italic;
    gboolean underline;
    gboolean strike;
    int color;
    int size_half;
} RunAttr;

typedef struct {
    int start;
    int end;
    RunAttr attr;
} Span;

typedef struct {
    GString *text;
    GArray *spans;
    GArray *colors;
    RunAttr cur;
    int chars;
    int depth;
    int skip_depth;
    gboolean skipping;
    int colortbl_depth;
    gboolean in_colortbl;
    int cr;
    int cg;
    int cb;
    gboolean has_comp;
    gboolean colortbl_first;
    gboolean star;
    int codepage;
    int uc;
    int pending_fallback;
    gunichar high_surrogate;
    gboolean ok;
} ParseCtx;

/* Confronta colori RGBA con epsilon. */
static gboolean color_equal(const GdkRGBA *a, const GdkRGBA *b) {
    return fabs(a->red - b->red) < RTF_COLOR_EPSILON &&
           fabs(a->green - b->green) < RTF_COLOR_EPSILON &&
           fabs(a->blue - b->blue) < RTF_COLOR_EPSILON;
}

/* Cerca indice colore in tabella. */
static int color_lookup(GArray *colors, const GdkRGBA *color) {
    guint i;

    for (i = 0; i < colors->len; i++) {
        GdkRGBA *entry = &g_array_index(colors, GdkRGBA, i);
        if (color_equal(entry, color)) {
            return (int)i;
        }
    }
    return -1;
}

/* Estrae attributi carattere all'iteratore. */
static void char_attr_at(GtkTextIter *iter, CharAttr *attr) {
    GSList *tags;
    GSList *node;

    memset(attr, 0, sizeof(*attr));

    tags = gtk_text_iter_get_tags(iter);
    for (node = tags; node != NULL; node = node->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(node->data);
        gchar *name = NULL;
        gboolean weight_set = FALSE;
        gboolean style_set = FALSE;
        gboolean underline_set = FALSE;
        gboolean strike_set = FALSE;
        gint weight = PANGO_WEIGHT_NORMAL;
        PangoStyle style = PANGO_STYLE_NORMAL;
        PangoUnderline underline = PANGO_UNDERLINE_NONE;
        gboolean strike = FALSE;

        g_object_get(tag,
                     "name", &name,
                     "weight-set", &weight_set,
                     "weight", &weight,
                     "style-set", &style_set,
                     "style", &style,
                     "underline-set", &underline_set,
                     "underline", &underline,
                     "strikethrough-set", &strike_set,
                     "strikethrough", &strike,
                     NULL);

        if ((name != NULL && strcmp(name, "bold") == 0) ||
            (weight_set && weight >= PANGO_WEIGHT_BOLD)) {
            attr->bold = TRUE;
        }
        if ((name != NULL && strcmp(name, "italic") == 0) ||
            (style_set && style == PANGO_STYLE_ITALIC)) {
            attr->italic = TRUE;
        }
        if ((name != NULL && strcmp(name, "underline") == 0) ||
            (underline_set && underline != PANGO_UNDERLINE_NONE)) {
            attr->underline = TRUE;
        }
        if ((name != NULL && strcmp(name, "strike") == 0) ||
            (strike_set && strike)) {
            attr->strike = TRUE;
        }

        g_free(name);
    }

    g_slist_free(tags);

    {
        GtkTextAttributes *attrs = gtk_text_attributes_new();

        gtk_text_iter_get_attributes(iter, attrs);
        if (attrs->appearance.rgba[1] != NULL) {
            attr->color = *attrs->appearance.rgba[1];
            attr->has_color = TRUE;
        }
        if (attrs->font != NULL) {
            int pango_size = pango_font_description_get_size(attrs->font);

            if (pango_size > 0) {
                double points = (double)pango_size / PANGO_SCALE;
                int half = (int)(points * 2.0 + 0.5);

                if (half > 0) {
                    attr->size_half = half;
                }
            }
        }
        gtk_text_attributes_unref(attrs);
    }
}

/* Accoda carattere con escape RTF. */
static void append_escaped_char(GString *out, gunichar ch) {
    if (ch == '\n') {
        g_string_append(out, "\\par ");
    } else if (ch == '\t') {
        g_string_append(out, "\\tab ");
    } else if (ch == '\\') {
        g_string_append(out, "\\\\");
    } else if (ch == '{') {
        g_string_append(out, "\\{");
    } else if (ch == '}') {
        g_string_append(out, "\\}");
    } else if (ch < 0x20) {
        g_string_append_printf(out, "\\'%02x", (unsigned int)ch);
    } else if (ch < 0x80) {
        g_string_append_c(out, (char)ch);
    } else if (ch <= 0xFFFF) {
        long value = ch > 0x7FFF ? (long)ch - 65536 : (long)ch;

        g_string_append_printf(out, "\\u%ld?", value);
    } else {
        gunichar value = ch - 0x10000;
        long high = (long)(0xD800 + (value >> 10)) - 65536;
        long low = (long)(0xDC00 + (value & 0x3FF)) - 65536;

        g_string_append_printf(out, "\\u%ld?\\u%ld?", high, low);
    }
}

/* Genera RTF da buffer GTK. */
char *rtf_from_buffer(GtkTextBuffer *buffer) {
    GString *out;
    GArray *used;
    GtkTextIter iter;
    gboolean prev_bold = FALSE;
    gboolean prev_italic = FALSE;
    gboolean prev_underline = FALSE;
    gboolean prev_strike = FALSE;
    int prev_color = 0;
    int prev_size_half = RTF_DEFAULT_SIZE_HALF;

    if (buffer == NULL) {
        return g_strdup("{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0{\\fonttbl{\\f0 Sans;}}{\\colortbl;}}");
    }

    used = g_array_new(FALSE, FALSE, sizeof(GdkRGBA));

    gtk_text_buffer_get_start_iter(buffer, &iter);
    while (!gtk_text_iter_is_end(&iter)) {
        CharAttr attr;

        char_attr_at(&iter, &attr);
        if (attr.has_color && color_lookup(used, &attr.color) < 0) {
            g_array_append_val(used, attr.color);
        }
        gtk_text_iter_forward_char(&iter);
    }

    out = g_string_new(NULL);
    g_string_append(out, "{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0{\\fonttbl{\\f0 Sans;}}");
    g_string_append(out, "{\\colortbl;");
    {
        guint i;

        for (i = 0; i < used->len; i++) {
            GdkRGBA *color = &g_array_index(used, GdkRGBA, i);
            int r = (int)(color->red * 255.0 + 0.5);
            int g = (int)(color->green * 255.0 + 0.5);
            int b = (int)(color->blue * 255.0 + 0.5);

            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            if (b < 0) b = 0;
            if (b > 255) b = 255;

            g_string_append_printf(out, "\\red%d\\green%d\\blue%d;", r, g, b);
        }
    }
    g_string_append_c(out, '}');
    g_string_append_printf(out, "\\fs%d ", RTF_DEFAULT_SIZE_HALF);

    gtk_text_buffer_get_start_iter(buffer, &iter);
    while (!gtk_text_iter_is_end(&iter)) {
        CharAttr attr;
        int color_index = 0;
        gunichar ch;

        char_attr_at(&iter, &attr);
        if (attr.has_color) {
            int found = color_lookup(used, &attr.color);
            if (found >= 0) {
                color_index = found + 1;
            }
        }

        if (attr.bold != prev_bold) {
            g_string_append(out, attr.bold ? "\\b " : "\\b0 ");
            prev_bold = attr.bold;
        }
        if (attr.italic != prev_italic) {
            g_string_append(out, attr.italic ? "\\i " : "\\i0 ");
            prev_italic = attr.italic;
        }
        if (attr.underline != prev_underline) {
            g_string_append(out, attr.underline ? "\\ul " : "\\ul0 ");
            prev_underline = attr.underline;
        }
        if (attr.strike != prev_strike) {
            g_string_append(out, attr.strike ? "\\strike " : "\\strike0 ");
            prev_strike = attr.strike;
        }
        if (color_index != prev_color) {
            if (color_index > 0) {
                g_string_append_printf(out, "\\cf%d ", color_index);
            } else {
                g_string_append(out, "\\cf0 ");
            }
            prev_color = color_index;
        }

        {
            int size_half = attr.size_half > 0 ? attr.size_half
                                               : RTF_DEFAULT_SIZE_HALF;

            if (size_half != prev_size_half) {
                g_string_append_printf(out, "\\fs%d ", size_half);
                prev_size_half = size_half;
            }
        }

        ch = gtk_text_iter_get_char(&iter);
        append_escaped_char(out, ch);
        gtk_text_iter_forward_char(&iter);
    }

    g_string_append_c(out, '}');
    g_array_free(used, TRUE);

    return g_string_free(out, FALSE);
}

/* Estrae control word e parametro RTF. */
static size_t parse_control_word(const char *s, size_t n, size_t i,
                                 gchar **word, long *param,
                                 gboolean *has_param) {
    size_t j = i + 1;

    while (j < n && g_ascii_isalpha((unsigned char)s[j])) {
        j++;
    }

    *word = g_strndup(s + i + 1, j - (i + 1));
    *param = 0;
    *has_param = FALSE;

    if (j < n && (s[j] == '-' || g_ascii_isdigit((unsigned char)s[j]))) {
        char *end = NULL;
        long value = strtol(s + j, &end, 10);

        if (end != NULL && end > s + j) {
            *param = value;
            *has_param = TRUE;
            j = (size_t)(end - s);
        }
    }

    if (j < n && s[j] == ' ') {
        j++;
    }

    return j;
}

/* Verifica destinazione RTF da saltare. */
static gboolean is_skippable_destination(const gchar *word) {
    static const char *const destinations[] = {
        "fonttbl", "stylesheet", "info", "pict", "header", "footer",
        "headerl", "headerr", "footerl", "footerr", "footnote",
        "annotation", "listtable", "listoverridetable", "generator",
        "xmlnstbl", "filetbl", "revtbl", "thesaurustbl", "datastore",
        "colorschememapping", "latentstyles", "rsidtbl", "mmathPr",
        NULL
    };
    int i;

    for (i = 0; destinations[i] != NULL; i++) {
        if (g_strcmp0(word, destinations[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Confronta attributi run. */
static gboolean run_attr_equal(const RunAttr *a, const RunAttr *b) {
    return a->bold == b->bold && a->italic == b->italic &&
           a->underline == b->underline && a->strike == b->strike &&
           a->color == b->color && a->size_half == b->size_half;
}

/* Emette byte e gestisce span attributi. */
static void parse_emit_byte(ParseCtx *p, unsigned char byte) {
    int before = p->chars;
    int after;
    Span span;

    g_string_append_c(p->text, (char)byte);
    /* Conta code point UTF-8, non byte di continuazione. */
    if ((byte & 0xC0) != 0x80) {
        p->chars++;
    }
    after = p->chars;

    if (p->spans->len > 0) {
        Span *last = &g_array_index(p->spans, Span, p->spans->len - 1);

        if (last->end == before && run_attr_equal(&last->attr, &p->cur)) {
            last->end = after;
            return;
        }
    }

    span.start = before;
    span.end = after;
    span.attr = p->cur;
    g_array_append_val(p->spans, span);
}

/* Emette un byte nella codepage RTF corrente convertendolo in UTF-8. */
static void parse_emit_codepage_byte(ParseCtx *p, unsigned char byte) {
    if (p->codepage == 65001) {
        parse_emit_byte(p, byte);
    } else {
        char encoding[16];
        gsize written = 0;
        char *converted;

        g_snprintf(encoding, sizeof(encoding), "CP%d", p->codepage);
        converted = g_convert((const char *)&byte, 1, "UTF-8", encoding,
                              NULL, &written, NULL);
        if (converted == NULL && p->codepage != 1252)
            converted = g_convert((const char *)&byte, 1, "UTF-8", "CP1252",
                                  NULL, &written, NULL);

        if (converted != NULL) {
            gsize index;

            for (index = 0; index < written; index++)
                parse_emit_byte(p, (unsigned char)converted[index]);
            g_free(converted);
        } else {
            parse_emit_byte(p, byte);
        }
    }
}

/* Emette un code point Unicode RTF (\uN). */
static void parse_emit_unicode(ParseCtx *p, long value) {
    gunichar codepoint = (gunichar)(value < 0 ? value + 65536 : value);
    char utf8[6];
    int length;
    int index;

    if (p->high_surrogate) {
        gunichar high = p->high_surrogate;

        p->high_surrogate = 0;
        if (codepoint >= 0xDC00 && codepoint <= 0xDFFF)
            codepoint = 0x10000 + ((high - 0xD800) << 10) +
                        (codepoint - 0xDC00);
        else
            codepoint = 0xFFFD;
    } else if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
        p->high_surrogate = codepoint;
        return;
    } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
        codepoint = 0xFFFD;
    }
    length = g_unichar_to_utf8(codepoint, utf8);
    for (index = 0; index < length; index++)
        parse_emit_byte(p, (unsigned char)utf8[index]);
}

/* Aggiunge colore corrente alla tabella. */
static void parse_push_color(ParseCtx *p) {
    GdkRGBA color;

    color.red = (p->cr < 0 ? 0 : (p->cr > 255 ? 255 : p->cr)) / 255.0;
    color.green = (p->cg < 0 ? 0 : (p->cg > 255 ? 255 : p->cg)) / 255.0;
    color.blue = (p->cb < 0 ? 0 : (p->cb > 255 ? 255 : p->cb)) / 255.0;
    color.alpha = 1.0;
    g_array_append_val(p->colors, color);
}

/* Parser subset RTF in testo e span. */
static gboolean rtf_parse_subset(const char *rtf, GString **out_text,
                                 GArray **out_spans, GArray **out_colors) {
    ParseCtx p;
    const char *s = rtf;
    size_t n = strlen(rtf);
    size_t i = 0;

    memset(&p, 0, sizeof(p));
    p.text = g_string_new(NULL);
    p.spans = g_array_new(FALSE, FALSE, sizeof(Span));
    p.colors = g_array_new(FALSE, FALSE, sizeof(GdkRGBA));
    p.skip_depth = -1;
    p.colortbl_depth = -1;
    p.codepage = 1252;
    p.uc = 1;
    p.cur.size_half = RTF_DEFAULT_SIZE_HALF;
    p.ok = TRUE;
    {
        GdkRGBA def = {0.0, 0.0, 0.0, 1.0};
        g_array_append_val(p.colors, def);
    }

    while (i < n) {
        char c = s[i];

        if (p.skipping) {
            if (c == '{') {
                p.depth++;
                i++;
            } else if (c == '}') {
                p.depth--;
                if (p.depth < 0) {
                    p.ok = FALSE;
                    break;
                }
                if (p.depth < p.skip_depth) {
                    p.skipping = FALSE;
                    p.skip_depth = -1;
                }
                i++;
            } else {
                i++;
            }
            continue;
        }

        if (p.in_colortbl) {
            if (c == '{') {
                p.depth++;
                i++;
                continue;
            }
            if (c == '}') {
                p.depth--;
                if (p.depth < 0) {
                    p.ok = FALSE;
                    break;
                }
                if (p.depth < p.colortbl_depth) {
                    p.in_colortbl = FALSE;
                    p.colortbl_depth = -1;
                }
                i++;
                continue;
            }
            if (c == '\\') {
                if (i + 1 >= n) {
                    p.ok = FALSE;
                    break;
                }
                if (g_ascii_isalpha((unsigned char)s[i + 1])) {
                    gchar *word = NULL;
                    long param = 0;
                    gboolean has_param = FALSE;
                    size_t next;

                    next = parse_control_word(s, n, i, &word, &param,
                                              &has_param);
                    if (g_strcmp0(word, "red") == 0 && has_param) {
                        p.cr = (int)param;
                        p.has_comp = TRUE;
                    } else if (g_strcmp0(word, "green") == 0 && has_param) {
                        p.cg = (int)param;
                        p.has_comp = TRUE;
                    } else if (g_strcmp0(word, "blue") == 0 && has_param) {
                        p.cb = (int)param;
                        p.has_comp = TRUE;
                    }
                    g_free(word);
                    i = next;
                } else {
                    i += 2;
                }
                continue;
            }
            if (c == ';') {
                /* Prima voce tabella colori è default, saltata se vuota. */
                if (p.colortbl_first) {
                    p.colortbl_first = FALSE;
                    if (p.has_comp) {
                        parse_push_color(&p);
                    }
                } else {
                    parse_push_color(&p);
                }
                p.cr = 0;
                p.cg = 0;
                p.cb = 0;
                p.has_comp = FALSE;
                i++;
                continue;
            }
            i++;
            continue;
        }

        if (p.pending_fallback > 0) {
            if (c == '\\' && i + 3 < n && s[i + 1] == '\'') {
                i += 4;
                --p.pending_fallback;
                continue;
            }
            if (c == '\\' && i + 1 < n &&
                g_ascii_isalpha((unsigned char)s[i + 1])) {
                p.pending_fallback = 0;
            } else if (c == '\r' || c == '\n') {
                i++;
                continue;
            } else {
                i++;
                --p.pending_fallback;
                continue;
            }
        }

        if (c == '{') {
            p.depth++;
            i++;
            continue;
        }
        if (c == '}') {
            p.depth--;
            if (p.depth < 0) {
                p.ok = FALSE;
                break;
            }
            i++;
            continue;
        }

        if (c == '\\') {
            char d;

            if (i + 1 >= n) {
                p.ok = FALSE;
                break;
            }
            d = s[i + 1];

            if (d == '\\' || d == '{' || d == '}') {
                parse_emit_byte(&p, (unsigned char)d);
                i += 2;
                continue;
            }
            if (d == '\'') {
                int hi;
                int lo;

                if (i + 3 >= n) {
                    p.ok = FALSE;
                    break;
                }
                hi = g_ascii_xdigit_value(s[i + 2]);
                lo = g_ascii_xdigit_value(s[i + 3]);
                if (hi < 0 || lo < 0) {
                    p.ok = FALSE;
                    break;
                }
                parse_emit_codepage_byte(&p, (unsigned char)((hi << 4) | lo));
                i += 4;
                continue;
            }
            if (d == '*') {
                p.star = TRUE;
                i += 2;
                continue;
            }
            if (g_ascii_isalpha((unsigned char)d)) {
                gchar *word = NULL;
                long param = 0;
                gboolean has_param = FALSE;
                size_t next;

                next = parse_control_word(s, n, i, &word, &param,
                                          &has_param);

                if (p.star) {
                    p.star = FALSE;
                    p.skipping = TRUE;
                    p.skip_depth = p.depth;
                    g_free(word);
                    i = next;
                    continue;
                }

                if (g_strcmp0(word, "colortbl") == 0) {
                    p.in_colortbl = TRUE;
                    p.colortbl_depth = p.depth;
                    p.cr = 0;
                    p.cg = 0;
                    p.cb = 0;
                    p.has_comp = FALSE;
                    p.colortbl_first = TRUE;
                } else if (is_skippable_destination(word)) {
                    p.skipping = TRUE;
                    p.skip_depth = p.depth;
                } else if (g_strcmp0(word, "b") == 0) {
                    p.cur.bold = !has_param || param != 0;
                } else if (g_strcmp0(word, "i") == 0) {
                    p.cur.italic = !has_param || param != 0;
                } else if (g_strcmp0(word, "ul") == 0) {
                    p.cur.underline = !has_param || param != 0;
                } else if (g_strcmp0(word, "ulnone") == 0) {
                    p.cur.underline = FALSE;
                } else if (g_strcmp0(word, "strike") == 0) {
                    p.cur.strike = !has_param || param != 0;
                } else if (g_strcmp0(word, "cf") == 0) {
                    p.cur.color = (has_param && param > 0) ? (int)param : 0;
                } else if (g_strcmp0(word, "ansicpg") == 0) {
                    if (has_param && param > 0)
                        p.codepage = (int)param;
                } else if (g_strcmp0(word, "uc") == 0) {
                    p.uc = has_param && param >= 0 ? (int)param : 1;
                } else if (g_strcmp0(word, "u") == 0) {
                    if (has_param) {
                        parse_emit_unicode(&p, param);
                        p.pending_fallback = p.uc;
                    }
                } else if (g_strcmp0(word, "fs") == 0) {
                    if (has_param && param > 0) {
                        long half = param;

                        if (half < 1) {
                            half = 1;
                        } else if (half > 1638) {
                            half = 1638;
                        }
                        p.cur.size_half = (int)half;
                    }
                } else if (g_strcmp0(word, "par") == 0 ||
                           g_strcmp0(word, "line") == 0) {
                    parse_emit_byte(&p, (unsigned char)'\n');
                } else if (g_strcmp0(word, "tab") == 0) {
                    parse_emit_byte(&p, (unsigned char)'\t');
                } else if (g_strcmp0(word, "emdash") == 0) {
                    parse_emit_unicode(&p, 8212);
                } else if (g_strcmp0(word, "endash") == 0) {
                    parse_emit_unicode(&p, 8211);
                } else if (g_strcmp0(word, "lquote") == 0) {
                    parse_emit_unicode(&p, 8216);
                } else if (g_strcmp0(word, "rquote") == 0) {
                    parse_emit_unicode(&p, 8217);
                } else if (g_strcmp0(word, "ldblquote") == 0) {
                    parse_emit_unicode(&p, 8220);
                } else if (g_strcmp0(word, "rdblquote") == 0) {
                    parse_emit_unicode(&p, 8221);
                } else if (g_strcmp0(word, "bullet") == 0) {
                    parse_emit_unicode(&p, 8226);
                } else if (g_strcmp0(word, "emspace") == 0) {
                    parse_emit_unicode(&p, 8195);
                } else if (g_strcmp0(word, "enspace") == 0) {
                    parse_emit_unicode(&p, 8194);
                } else if (g_strcmp0(word, "plain") == 0) {
                    memset(&p.cur, 0, sizeof(p.cur));
                    p.cur.size_half = RTF_DEFAULT_SIZE_HALF;
                }

                g_free(word);
                i = next;
                continue;
            }

            if (d == '~') {
                parse_emit_byte(&p, (unsigned char)' ');
            } else if (d == '_') {
                parse_emit_byte(&p, (unsigned char)'-');
            } else if (d != '-') {
                parse_emit_byte(&p, (unsigned char)d);
            }
            i += 2;
            continue;
        }

        if (c == '\r' || c == '\n') {
            /* RTF ignora i ritorni a capo usati per formattare il sorgente. */
            i++;
            continue;
        }
        parse_emit_byte(&p, (unsigned char)c);
        i++;
    }

    if (p.depth != 0) {
        p.ok = FALSE;
    }

    *out_text = p.text;
    *out_spans = p.spans;
    *out_colors = p.colors;
    return p.ok;
}

/* Cerca tag per nome. */
static GtkTextTag *lookup_tag(GtkTextBuffer *buffer, const char *name) {
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);

    return gtk_text_tag_table_lookup(table, name);
}

/* Crea/ottiene tag grassetto. */
static GtkTextTag *ensure_bold_tag(GtkTextBuffer *buffer) {
    GtkTextTag *tag = lookup_tag(buffer, "bold");

    if (tag == NULL) {
        tag = gtk_text_buffer_create_tag(buffer, "bold",
                                         "weight", PANGO_WEIGHT_BOLD,
                                         NULL);
    }
    return tag;
}

/* Crea/ottiene tag corsivo. */
static GtkTextTag *ensure_italic_tag(GtkTextBuffer *buffer) {
    GtkTextTag *tag = lookup_tag(buffer, "italic");

    if (tag == NULL) {
        tag = gtk_text_buffer_create_tag(buffer, "italic",
                                         "style", PANGO_STYLE_ITALIC,
                                         NULL);
    }
    return tag;
}

/* Crea/ottiene tag sottolineato. */
static GtkTextTag *ensure_underline_tag(GtkTextBuffer *buffer) {
    GtkTextTag *tag = lookup_tag(buffer, "underline");

    if (tag == NULL) {
        tag = gtk_text_buffer_create_tag(buffer, "underline",
                                         "underline", PANGO_UNDERLINE_SINGLE,
                                         NULL);
    }
    return tag;
}

/* Crea/ottiene tag barrato. */
static GtkTextTag *ensure_strike_tag(GtkTextBuffer *buffer) {
    GtkTextTag *tag = lookup_tag(buffer, "strike");

    if (tag == NULL) {
        tag = gtk_text_buffer_create_tag(buffer, "strike",
                                         "strikethrough", TRUE,
                                         NULL);
    }
    return tag;
}

/* Crea/ottiene tag dimensione font. */
static GtkTextTag *ensure_size_tag(GtkTextBuffer *buffer, GHashTable *cache,
                                   int size_half) {
    gpointer cached = g_hash_table_lookup(cache, GINT_TO_POINTER(size_half));
    GtkTextTag *tag;
    char name[32];
    double points;

    if (cached != NULL) {
        return GTK_TEXT_TAG(cached);
    }

    points = (double)size_half / 2.0;
    if (size_half % 2 == 0) {
        g_snprintf(name, sizeof(name), "fs-%d", size_half / 2);
    } else {
        g_snprintf(name, sizeof(name), "fs-%g", points);
    }

    tag = lookup_tag(buffer, name);
    if (tag == NULL) {
        tag = gtk_text_buffer_create_tag(buffer, name,
                                         "size-points", points,
                                         NULL);
    }
    if (tag != NULL) {
        g_hash_table_insert(cache, GINT_TO_POINTER(size_half), tag);
    }
    return tag;
}

/* Crea/ottiene tag colore foreground. */
static GtkTextTag *ensure_foreground_tag(GtkTextBuffer *buffer,
                                         GHashTable *cache, int index,
                                         const GdkRGBA *color) {
    gpointer cached = g_hash_table_lookup(cache, GINT_TO_POINTER(index));
    GtkTextTag *tag;
    char name[32];

    if (cached != NULL) {
        return GTK_TEXT_TAG(cached);
    }

    g_snprintf(name, sizeof(name), "fg-%d", index);
    tag = lookup_tag(buffer, name);
    if (tag == NULL) {
        GdkRGBA copy = *color;

        tag = gtk_text_buffer_create_tag(buffer, name,
                                         "foreground-rgba", &copy,
                                         "foreground-set", TRUE,
                                         NULL);
    }
    if (tag != NULL) {
        g_hash_table_insert(cache, GINT_TO_POINTER(index), tag);
    }
    return tag;
}

/* Fallback: estrae testo semplice da RTF. */
static void insert_plain_fallback(const char *rtf, GtkTextBuffer *buffer) {
    GString *plain = g_string_new(NULL);
    size_t n = strlen(rtf);
    size_t i = 0;

    while (i < n) {
        char c = rtf[i];

        if (c == '{' || c == '}') {
            i++;
            continue;
        }
        if (c == '\\') {
            char d;

            if (i + 1 >= n) {
                break;
            }
            d = rtf[i + 1];

            if (d == '\\' || d == '{' || d == '}') {
                g_string_append_c(plain, d);
                i += 2;
                continue;
            }
            if (d == '\'') {
                if (i + 3 < n) {
                    int hi = g_ascii_xdigit_value(rtf[i + 2]);
                    int lo = g_ascii_xdigit_value(rtf[i + 3]);

                    if (hi >= 0 && lo >= 0) {
                        g_string_append_c(plain, (char)((hi << 4) | lo));
                    }
                }
                i += 4;
                continue;
            }
            if (g_ascii_isalpha((unsigned char)d)) {
                size_t j = i + 1;
                gchar *word;
                gboolean is_par;
                gboolean is_tab;

                while (j < n && g_ascii_isalpha((unsigned char)rtf[j])) {
                    j++;
                }
                word = g_strndup(rtf + i + 1, j - (i + 1));
                while (j < n &&
                       (rtf[j] == '-' ||
                        g_ascii_isdigit((unsigned char)rtf[j]))) {
                    j++;
                }
                if (j < n && rtf[j] == ' ') {
                    j++;
                }
                is_par = g_strcmp0(word, "par") == 0 ||
                         g_strcmp0(word, "line") == 0;
                is_tab = g_strcmp0(word, "tab") == 0;
                if (is_par) {
                    g_string_append_c(plain, '\n');
                } else if (is_tab) {
                    g_string_append_c(plain, '\t');
                }
                g_free(word);
                i = j;
                continue;
            }
            if (d == '~') {
                g_string_append_c(plain, ' ');
            } else if (d == '_') {
                g_string_append_c(plain, '-');
            }
            i += 2;
            continue;
        }

        g_string_append_c(plain, c);
        i++;
    }

    {
        gchar *valid = g_utf8_make_valid(plain->str, -1);

        gtk_text_buffer_begin_user_action(buffer);
        gtk_text_buffer_set_text(buffer, valid, -1);
        gtk_text_buffer_end_user_action(buffer);
        g_free(valid);
    }

    g_string_free(plain, TRUE);
}

/* Estrae il testo semplice da un RTF valido, senza applicare formattazione. */
char *rtf_plain_text(const char *rtf) {
    GString *text = NULL;
    GArray *spans = NULL;
    GArray *colors = NULL;
    char *result = NULL;

    if (rtf == NULL || rtf[0] == '\0')
        return NULL;
    if (!rtf_parse_subset(rtf, &text, &spans, &colors))
        goto done;
    if (text != NULL && g_utf8_validate(text->str, (gssize)text->len, NULL)) {
        result = g_string_free(text, FALSE);
        text = NULL;
    }
done:
    if (text != NULL)
        g_string_free(text, TRUE);
    if (spans != NULL)
        g_array_free(spans, TRUE);
    if (colors != NULL)
        g_array_free(colors, TRUE);
    return result;
}

/* Carica RTF in buffer GTK. */
void rtf_to_buffer(const char *rtf, GtkTextBuffer *buffer) {    GString *text = NULL;
    GArray *spans = NULL;
    GArray *colors = NULL;
    gboolean ok;

    if (buffer == NULL) {
        return;
    }

    if (rtf == NULL || rtf[0] == '\0') {
        gtk_text_buffer_begin_user_action(buffer);
        gtk_text_buffer_set_text(buffer, "", -1);
        gtk_text_buffer_end_user_action(buffer);
        return;
    }

    ok = rtf_parse_subset(rtf, &text, &spans, &colors);
    if (!ok || text == NULL ||
        !g_utf8_validate(text->str, (gssize)text->len, NULL)) {
        if (text != NULL) {
            g_string_free(text, TRUE);
        }
        if (spans != NULL) {
            g_array_free(spans, TRUE);
        }
        if (colors != NULL) {
            g_array_free(colors, TRUE);
        }
        insert_plain_fallback(rtf, buffer);
        return;
    }

    {
        GtkTextTag *bold_tag = ensure_bold_tag(buffer);
        GtkTextTag *italic_tag = ensure_italic_tag(buffer);
        GtkTextTag *underline_tag = ensure_underline_tag(buffer);
        GtkTextTag *strike_tag = ensure_strike_tag(buffer);
        GHashTable *fg_cache = g_hash_table_new(g_direct_hash, g_direct_equal);
        GHashTable *size_cache = g_hash_table_new(g_direct_hash, g_direct_equal);
        guint i;

        gtk_text_buffer_begin_user_action(buffer);
        gtk_text_buffer_set_text(buffer, text->str, (gint)text->len);

        for (i = 0; i < spans->len; i++) {
            Span *span = &g_array_index(spans, Span, i);
            GtkTextIter start;
            GtkTextIter end;

            if (span->end <= span->start) {
                continue;
            }

            gtk_text_buffer_get_iter_at_offset(buffer, &start, span->start);
            gtk_text_buffer_get_iter_at_offset(buffer, &end, span->end);

            if (span->attr.bold && bold_tag != NULL) {
                gtk_text_buffer_apply_tag(buffer, bold_tag, &start, &end);
            }
            if (span->attr.italic && italic_tag != NULL) {
                gtk_text_buffer_apply_tag(buffer, italic_tag, &start, &end);
            }
            if (span->attr.underline && underline_tag != NULL) {
                gtk_text_buffer_apply_tag(buffer, underline_tag, &start, &end);
            }
            if (span->attr.strike && strike_tag != NULL) {
                gtk_text_buffer_apply_tag(buffer, strike_tag, &start, &end);
            }
            if (span->attr.size_half > 0) {
                GtkTextTag *size_tag = ensure_size_tag(buffer, size_cache,
                                                       span->attr.size_half);

                if (size_tag != NULL) {
                    gtk_text_buffer_apply_tag(buffer, size_tag, &start, &end);
                }
            }
            if (span->attr.color > 0 &&
                (guint)span->attr.color < colors->len) {
                GdkRGBA *color = &g_array_index(colors, GdkRGBA,
                                                (guint)span->attr.color);
                GtkTextTag *fg_tag = ensure_foreground_tag(buffer, fg_cache,
                                                           span->attr.color,
                                                           color);

                if (fg_tag != NULL) {
                    gtk_text_buffer_apply_tag(buffer, fg_tag, &start, &end);
                }
            }
        }

        gtk_text_buffer_end_user_action(buffer);
        g_hash_table_destroy(fg_cache);
        g_hash_table_destroy(size_cache);
    }

    g_string_free(text, TRUE);
    g_array_free(spans, TRUE);
    g_array_free(colors, TRUE);
}
