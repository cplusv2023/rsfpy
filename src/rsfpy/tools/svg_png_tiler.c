#include "svg_png_tiler.h"

#include <cairo.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#define DEFAULT_TILE_PX 512u
#define DEFAULT_BLEED_PX 4u
#define DEFAULT_MAX_DATA_URI_CHARS (96u * 1024u)
#define DEFAULT_MAX_IMAGE_PIXELS (1024u * 1024u)
#define DEFAULT_MIN_TILE_PX 16u

typedef struct Attr {
    gchar *name;
    gchar *value;
    gboolean has_value;
} Attr;

typedef struct ImageTag {
    gsize start;
    gsize open_end;
    gsize replace_end;
    gchar *qname;
    GPtrArray *attrs; /* Attr* */
} ImageTag;

typedef struct MemRead {
    const guchar *data;
    gsize len;
    gsize pos;
} MemRead;

typedef struct MemWrite {
    GByteArray *bytes;
} MemWrite;

typedef struct TileCtx {
    cairo_surface_t *src;
    int img_w;
    int img_h;
    double svg_x;
    double svg_y;
    double svg_w;
    double svg_h;
    const SvgPngTilerOptions *opt;
    const gchar *href_attr_name;
    const gchar *id_prefix;
    guint *tile_counter;
    GString *out;
} TileCtx;

GQuark svg_png_tiler_error_quark(void)
{
    return g_quark_from_static_string("svg-png-tiler-error-quark");
}

void svg_png_tiler_options_init(SvgPngTilerOptions *opt)
{
    if (!opt) return;
    opt->tile_px = DEFAULT_TILE_PX;
    opt->bleed_px = DEFAULT_BLEED_PX;
    opt->max_data_uri_chars = DEFAULT_MAX_DATA_URI_CHARS;
    opt->max_image_pixels = DEFAULT_MAX_IMAGE_PIXELS;
    opt->min_tile_px = DEFAULT_MIN_TILE_PX;
    opt->force = FALSE;
    opt->disable = FALSE;
    opt->keep_original_id_on_group = TRUE;
    opt->inherit_image_rendering = TRUE;
    opt->emit_comment = TRUE;
    opt->force_href = FALSE;
}

static gboolean env_truthy_local(const gchar *name)
{
    const gchar *v = g_getenv(name);
    if (!v || !*v) return FALSE;
    return g_ascii_strcasecmp(v, "0") != 0 &&
           g_ascii_strcasecmp(v, "n") != 0 &&
           g_ascii_strcasecmp(v, "no") != 0 &&
           g_ascii_strcasecmp(v, "false") != 0 &&
           g_ascii_strcasecmp(v, "off") != 0;
}

static guint env_uint_local(const gchar *name, guint fallback)
{
    const gchar *v = g_getenv(name);
    gchar *end = NULL;
    guint64 parsed;
    if (!v || !*v) return fallback;
    parsed = g_ascii_strtoull(v, &end, 10);
    if (end == v || parsed > G_MAXUINT) return fallback;
    return (guint)parsed;
}

static gsize env_size_mb_or_bytes_local(const gchar *mb_name,
                                        const gchar *bytes_name,
                                        gsize fallback)
{
    const gchar *v = g_getenv(mb_name);
    gchar *end = NULL;
    guint64 parsed;
    if (v && *v) {
        parsed = g_ascii_strtoull(v, &end, 10);
        if (end != v && parsed > 0) {
            if (parsed > G_MAXSIZE / (1024u * 1024u)) return G_MAXSIZE;
            return (gsize)(parsed * 1024u * 1024u);
        }
    }
    v = g_getenv(bytes_name);
    if (v && *v) {
        parsed = g_ascii_strtoull(v, &end, 10);
        if (end != v && parsed > 0) {
            if (parsed > G_MAXSIZE) return G_MAXSIZE;
            return (gsize)parsed;
        }
    }
    return fallback;
}

static gsize env_size_local(const gchar *name, gsize fallback)
{
    const gchar *v = g_getenv(name);
    gchar *end = NULL;
    guint64 parsed;
    if (!v || !*v) return fallback;
    parsed = g_ascii_strtoull(v, &end, 10);
    if (end == v || parsed == 0) return fallback;
    if (parsed > G_MAXSIZE) return G_MAXSIZE;
    return (gsize)parsed;
}

void svg_png_tiler_options_init_from_env(SvgPngTilerOptions *opt)
{
    if (!opt) return;
    svg_png_tiler_options_init(opt);
    opt->tile_px = env_uint_local("RSFPY_SVG_IMAGE_TILE_SIZE", opt->tile_px);
    opt->bleed_px = env_uint_local("RSFPY_SVG_IMAGE_TILE_BLEED", opt->bleed_px);
    opt->min_tile_px = env_uint_local("RSFPY_SVG_IMAGE_TILE_MIN_SIZE", opt->min_tile_px);
    opt->max_image_pixels = env_size_local("RSFPY_SVG_IMAGE_TILE_MAX_PIXELS",
                                           opt->max_image_pixels);
    opt->max_data_uri_chars = env_size_mb_or_bytes_local(
        "RSFPY_SVG_IMAGE_TILE_MAX_URI_MB",
        "RSFPY_SVG_IMAGE_TILE_MAX_URI_CHARS",
        opt->max_data_uri_chars);
    if (env_truthy_local("RSFPY_SVG_IMAGE_TILE_FORCE")) opt->force = TRUE;
    if (env_truthy_local("RSFPY_SVG_IMAGE_TILE_DISABLE")) opt->disable = TRUE;
}

static SvgPngTilerOptions normalized_options(const SvgPngTilerOptions *opt)
{
    SvgPngTilerOptions o;
    svg_png_tiler_options_init(&o);
    if (opt) o = *opt;
    if (o.tile_px == 0) o.tile_px = DEFAULT_TILE_PX;
    if (o.min_tile_px == 0) o.min_tile_px = DEFAULT_MIN_TILE_PX;
    if (o.min_tile_px > o.tile_px) o.min_tile_px = o.tile_px;
    if (o.max_data_uri_chars == 0 && o.max_image_pixels == 0) o.force = TRUE;
    return o;
}

static void attr_free(gpointer p)
{
    Attr *a = (Attr *)p;
    if (!a) return;
    g_free(a->name);
    g_free(a->value);
    g_free(a);
}

static void image_tag_clear(ImageTag *t)
{
    if (!t) return;
    g_free(t->qname);
    t->qname = NULL;
    if (t->attrs) {
        g_ptr_array_free(t->attrs, TRUE);
        t->attrs = NULL;
    }
}

static gboolean is_xml_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static gboolean is_name_break(char c)
{
    return c == '\0' || is_xml_space(c) || c == '/' || c == '>' || c == '=';
}

static gboolean ascii_eq_nocase_n(const gchar *a, gsize alen, const gchar *b)
{
    gsize i, blen = strlen(b);
    if (alen != blen) return FALSE;
    for (i = 0; i < alen; i++) {
        if (g_ascii_tolower(a[i]) != g_ascii_tolower(b[i])) return FALSE;
    }
    return TRUE;
}

static gboolean qname_local_is(const gchar *qname, gsize len, const gchar *local)
{
    gsize i, start = 0;
    for (i = 0; i < len; i++) {
        if (qname[i] == ':') start = i + 1;
    }
    return ascii_eq_nocase_n(qname + start, len - start, local);
}

static gboolean attr_name_is(const gchar *name, const gchar *wanted)
{
    return name && g_ascii_strcasecmp(name, wanted) == 0;
}

static const Attr *attrs_get(const GPtrArray *attrs, const gchar *name)
{
    guint i;
    if (!attrs) return NULL;
    for (i = attrs->len; i > 0; i--) {
        const Attr *a = (const Attr *)g_ptr_array_index(attrs, i - 1);
        if (attr_name_is(a->name, name)) return a;
    }
    return NULL;
}

static void append_xml_escaped(GString *s, const gchar *v)
{
    gchar *e = g_markup_escape_text(v ? v : "", -1);
    g_string_append(s, e);
    g_free(e);
}

static void append_attr(GString *s, const gchar *name, const gchar *value)
{
    g_string_append_c(s, ' ');
    g_string_append(s, name);
    g_string_append(s, "=\"");
    append_xml_escaped(s, value ? value : "");
    g_string_append_c(s, '"');
}

static gchar *xml_attr_unescape(const gchar *src, gsize len)
{
    GString *out = g_string_sized_new(len);
    gsize i = 0;

    while (i < len) {
        if (src[i] != '&') {
            g_string_append_c(out, src[i++]);
            continue;
        }

        if (i + 5 <= len && strncmp(src + i, "&amp;", 5) == 0) {
            g_string_append_c(out, '&'); i += 5;
        } else if (i + 6 <= len && strncmp(src + i, "&quot;", 6) == 0) {
            g_string_append_c(out, '"'); i += 6;
        } else if (i + 6 <= len && strncmp(src + i, "&apos;", 6) == 0) {
            g_string_append_c(out, '\''); i += 6;
        } else if (i + 4 <= len && strncmp(src + i, "&lt;", 4) == 0) {
            g_string_append_c(out, '<'); i += 4;
        } else if (i + 4 <= len && strncmp(src + i, "&gt;", 4) == 0) {
            g_string_append_c(out, '>'); i += 4;
        } else if (i + 2 < len && src[i + 1] == '#') {
            gsize j = i + 2;
            gboolean hex = FALSE;
            gunichar uc = 0;
            if (j < len && (src[j] == 'x' || src[j] == 'X')) { hex = TRUE; j++; }
            while (j < len && src[j] != ';') {
                int d = -1;
                if (hex) {
                    if (src[j] >= '0' && src[j] <= '9') d = src[j] - '0';
                    else if (src[j] >= 'a' && src[j] <= 'f') d = 10 + src[j] - 'a';
                    else if (src[j] >= 'A' && src[j] <= 'F') d = 10 + src[j] - 'A';
                    else break;
                    uc = (uc << 4) + (gunichar)d;
                } else {
                    if (src[j] < '0' || src[j] > '9') break;
                    uc = uc * 10 + (gunichar)(src[j] - '0');
                }
                j++;
            }
            if (j < len && src[j] == ';' && uc != 0 && g_unichar_validate(uc)) {
                gchar buf[8];
                gint n = g_unichar_to_utf8(uc, buf);
                g_string_append_len(out, buf, n);
                i = j + 1;
            } else {
                g_string_append_c(out, src[i++]);
            }
        } else {
            g_string_append_c(out, src[i++]);
        }
    }

    return g_string_free(out, FALSE);
}

static gboolean parse_svg_number_or_px(const gchar *value, double fallback,
                                       double *out)
{
    gchar *end = NULL;
    double v;

    if (!out) return FALSE;
    if (!value || !*value) {
        *out = fallback;
        return TRUE;
    }

    v = g_ascii_strtod(value, &end);
    if (end == value || !isfinite(v)) return FALSE;
    while (*end && is_xml_space(*end)) end++;
    if (g_ascii_strncasecmp(end, "px", 2) == 0) {
        end += 2;
        while (*end && is_xml_space(*end)) end++;
    }
    if (*end) return FALSE;
    *out = v;
    return TRUE;
}

static gsize find_plain(const gchar *s, gsize len, gsize from, const gchar *needle)
{
    gsize nlen = strlen(needle);
    gsize i;
    if (nlen == 0 || from >= len || nlen > len) return G_MAXSIZE;
    for (i = from; i + nlen <= len; i++) {
        if (memcmp(s + i, needle, nlen) == 0) return i;
    }
    return G_MAXSIZE;
}

static gsize find_tag_end(const gchar *s, gsize len, gsize lt)
{
    gchar quote = 0;
    gsize i;
    for (i = lt + 1; i < len; i++) {
        if (quote) {
            if (s[i] == quote) quote = 0;
        } else {
            if (s[i] == '"' || s[i] == '\'') quote = s[i];
            else if (s[i] == '>') return i;
        }
    }
    return G_MAXSIZE;
}

static GPtrArray *parse_attrs(const gchar *s, gsize from, gsize to)
{
    GPtrArray *attrs = g_ptr_array_new_with_free_func(attr_free);
    gsize i = from;

    while (i < to) {
        gsize ns, ne;
        Attr *a;

        while (i < to && is_xml_space(s[i])) i++;
        if (i >= to) break;
        if (s[i] == '/') { i++; continue; }

        ns = i;
        while (i < to && !is_name_break(s[i])) i++;
        ne = i;
        if (ne == ns) { i++; continue; }

        a = g_new0(Attr, 1);
        a->name = g_strndup(s + ns, ne - ns);
        a->value = g_strdup("");
        a->has_value = FALSE;

        while (i < to && is_xml_space(s[i])) i++;
        if (i < to && s[i] == '=') {
            gsize vs, ve;
            gchar q;
            i++;
            while (i < to && is_xml_space(s[i])) i++;
            if (i < to && (s[i] == '"' || s[i] == '\'')) {
                q = s[i++];
                vs = i;
                while (i < to && s[i] != q) i++;
                ve = i;
                if (i < to && s[i] == q) i++;
            } else {
                vs = i;
                while (i < to && !is_xml_space(s[i]) && s[i] != '/' && s[i] != '>') i++;
                ve = i;
            }
            g_free(a->value);
            a->value = xml_attr_unescape(s + vs, ve - vs);
            a->has_value = TRUE;
        }

        g_ptr_array_add(attrs, a);
    }

    return attrs;
}

static gboolean tag_self_closing(const gchar *s, gsize lt, gsize gt)
{
    gsize i = gt;
    while (i > lt && is_xml_space(s[i - 1])) i--;
    return i > lt && s[i - 1] == '/';
}

static gsize find_image_close(const gchar *s, gsize len, gsize from, const gchar *qname)
{
    gsize qlen = strlen(qname);
    gsize i = from;

    while (i < len) {
        gsize lt = find_plain(s, len, i, "<");
        gsize j, ns, ne, gt;
        if (lt == G_MAXSIZE || lt + 2 >= len) return G_MAXSIZE;
        if (s[lt + 1] != '/') { i = lt + 1; continue; }
        j = lt + 2;
        while (j < len && is_xml_space(s[j])) j++;
        ns = j;
        while (j < len && !is_name_break(s[j])) j++;
        ne = j;
        if (ne > ns && ne - ns == qlen && g_ascii_strncasecmp(s + ns, qname, qlen) == 0) {
            gt = find_tag_end(s, len, lt);
            return gt == G_MAXSIZE ? G_MAXSIZE : gt + 1;
        }
        i = lt + 1;
    }

    return G_MAXSIZE;
}

static gboolean next_image_tag(const gchar *s, gsize len, gsize from, ImageTag *out, GError **error)
{
    gsize i = from;
    memset(out, 0, sizeof(*out));

    while (i < len) {
        gsize lt = find_plain(s, len, i, "<");
        gsize j, ns, ne, gt, attr_to;
        gboolean selfclose;
        if (lt == G_MAXSIZE) return FALSE;

        if (lt + 4 <= len && memcmp(s + lt, "<!--", 4) == 0) {
            gsize end = find_plain(s, len, lt + 4, "-->");
            i = (end == G_MAXSIZE) ? len : end + 3;
            continue;
        }
        if (lt + 9 <= len && memcmp(s + lt, "<![CDATA[", 9) == 0) {
            gsize end = find_plain(s, len, lt + 9, "]]>");
            i = (end == G_MAXSIZE) ? len : end + 3;
            continue;
        }
        if (lt + 2 <= len && s[lt + 1] == '?') {
            gsize end = find_plain(s, len, lt + 2, "?>");
            i = (end == G_MAXSIZE) ? len : end + 2;
            continue;
        }
        if (lt + 2 <= len && s[lt + 1] == '!') {
            gt = find_tag_end(s, len, lt);
            i = (gt == G_MAXSIZE) ? len : gt + 1;
            continue;
        }
        if (lt + 2 <= len && s[lt + 1] == '/') {
            gt = find_tag_end(s, len, lt);
            i = (gt == G_MAXSIZE) ? len : gt + 1;
            continue;
        }

        j = lt + 1;
        while (j < len && is_xml_space(s[j])) j++;
        ns = j;
        while (j < len && !is_name_break(s[j])) j++;
        ne = j;
        if (ne == ns) { i = lt + 1; continue; }

        gt = find_tag_end(s, len, lt);
        if (gt == G_MAXSIZE) {
            g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_PARSE,
                        "Unterminated SVG tag near byte %" G_GSIZE_FORMAT, lt);
            return FALSE;
        }

        if (!qname_local_is(s + ns, ne - ns, "image")) {
            i = gt + 1;
            continue;
        }

        selfclose = tag_self_closing(s, lt, gt);
        attr_to = gt;
        if (selfclose) {
            while (attr_to > lt && is_xml_space(s[attr_to - 1])) attr_to--;
            if (attr_to > lt && s[attr_to - 1] == '/') attr_to--;
        }

        out->start = lt;
        out->open_end = gt + 1;
        out->qname = g_strndup(s + ns, ne - ns);
        out->attrs = parse_attrs(s, ne, attr_to);
        if (selfclose) {
            out->replace_end = gt + 1;
        } else {
            out->replace_end = find_image_close(s, len, gt + 1, out->qname);
            if (out->replace_end == G_MAXSIZE) {
                image_tag_clear(out);
                g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_PARSE,
                            "Found non-self-closing <image> near byte %" G_GSIZE_FORMAT
                            " but no matching closing tag", lt);
                return FALSE;
            }
        }
        return TRUE;
    }

    return FALSE;
}

static gboolean meta_has_base64(const gchar *meta)
{
    gchar **parts = g_strsplit(meta, ";", -1);
    gboolean ok = FALSE;
    guint i;
    for (i = 0; parts && parts[i]; i++) {
        if (g_ascii_strcasecmp(parts[i], "base64") == 0) { ok = TRUE; break; }
    }
    g_strfreev(parts);
    return ok;
}

static gboolean meta_is_png_or_empty(const gchar *meta)
{
    const gchar *semi;
    gchar *mime;
    gboolean ok;
    if (!meta || !*meta || meta[0] == ';') return TRUE;
    semi = strchr(meta, ';');
    mime = semi ? g_strndup(meta, semi - meta) : g_strdup(meta);
    ok = (*mime == '\0') || (g_ascii_strcasecmp(mime, "image/png") == 0);
    g_free(mime);
    return ok;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static gchar *percent_unescape_ascii(const gchar *s)
{
    GString *out = g_string_sized_new(strlen(s));
    gsize i;
    for (i = 0; s[i]; i++) {
        if (s[i] == '%' && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            int hi = hexval(s[i + 1]);
            int lo = hexval(s[i + 2]);
            g_string_append_c(out, (gchar)((hi << 4) | lo));
            i += 2;
        } else {
            g_string_append_c(out, s[i]);
        }
    }
    return g_string_free(out, FALSE);
}

static gchar *strip_ascii_space(const gchar *s)
{
    GString *out = g_string_sized_new(strlen(s));
    gsize i;
    for (i = 0; s[i]; i++) {
        if (!g_ascii_isspace(s[i])) g_string_append_c(out, s[i]);
    }
    return g_string_free(out, FALSE);
}


static gboolean href_looks_like_data_png_base64(const gchar *href)
{
    const gchar *comma;
    gchar *meta;
    gboolean ok;

    if (!href || g_ascii_strncasecmp(href, "data:", 5) != 0) return FALSE;
    comma = strchr(href, ',');
    if (!comma) return FALSE;
    meta = g_strndup(href + 5, comma - (href + 5));
    ok = meta_has_base64(meta) && meta_is_png_or_empty(meta);
    g_free(meta);
    return ok;
}

static gboolean parse_png_data_uri(const gchar *href, guchar **png, gsize *png_len,
                                   gboolean *is_data_png, GError **error)
{
    const gchar *comma;
    gchar *meta = NULL, *payload_unescaped = NULL, *payload = NULL;
    static const guchar sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

    *png = NULL;
    *png_len = 0;
    *is_data_png = FALSE;

    if (!href || g_ascii_strncasecmp(href, "data:", 5) != 0) return TRUE;

    comma = strchr(href, ',');
    if (!comma) {
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_DATA_URI,
                    "data: URI has no comma separator");
        return FALSE;
    }

    meta = g_strndup(href + 5, comma - (href + 5));
    if (!meta_has_base64(meta) || !meta_is_png_or_empty(meta)) {
        g_free(meta);
        return TRUE;
    }

    *is_data_png = TRUE;
    payload_unescaped = percent_unescape_ascii(comma + 1);
    payload = strip_ascii_space(payload_unescaped);
    g_free(payload_unescaped);

    *png = g_base64_decode(payload, png_len);
    g_free(payload);
    g_free(meta);

    if (!*png || *png_len < 8 || memcmp(*png, sig, 8) != 0) {
        g_clear_pointer(png, g_free);
        *png_len = 0;
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_DATA_URI,
                    "Embedded data URI is marked image/png;base64 but decoded bytes are not a PNG");
        return FALSE;
    }

    return TRUE;
}

static cairo_status_t read_png_cb(void *closure, unsigned char *data, unsigned int length)
{
    MemRead *r = (MemRead *)closure;
    if (r->pos + length > r->len) return CAIRO_STATUS_READ_ERROR;
    memcpy(data, r->data + r->pos, length);
    r->pos += length;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t write_png_cb(void *closure, const unsigned char *data, unsigned int length)
{
    MemWrite *w = (MemWrite *)closure;
    g_byte_array_append(w->bytes, data, length);
    return CAIRO_STATUS_SUCCESS;
}

static cairo_surface_t *load_png_surface(const guchar *png, gsize png_len, GError **error)
{
    MemRead r;
    cairo_surface_t *s;
    cairo_status_t st;

    r.data = png;
    r.len = png_len;
    r.pos = 0;

    s = cairo_image_surface_create_from_png_stream(read_png_cb, &r);
    st = cairo_surface_status(s);
    if (st != CAIRO_STATUS_SUCCESS) {
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_CAIRO,
                    "cairo failed to decode embedded PNG: %s", cairo_status_to_string(st));
        cairo_surface_destroy(s);
        return NULL;
    }
    return s;
}

static gchar *encode_png_tile_data_uri(cairo_surface_t *src,
                                       int sx, int sy, int sw, int sh,
                                       GError **error)
{
    cairo_surface_t *dst;
    cairo_t *cr;
    cairo_status_t st;
    MemWrite w;
    gchar *b64, *uri;

    if (sw <= 0 || sh <= 0) {
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_INVALID_ARGUMENT,
                    "Invalid tile size %dx%d", sw, sh);
        return NULL;
    }

    dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    st = cairo_surface_status(dst);
    if (st != CAIRO_STATUS_SUCCESS) {
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_CAIRO,
                    "cairo failed to create tile surface: %s", cairo_status_to_string(st));
        cairo_surface_destroy(dst);
        return NULL;
    }

    cr = cairo_create(dst);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, src, -sx, -sy);
    cairo_paint(cr);
    st = cairo_status(cr);
    cairo_destroy(cr);
    if (st != CAIRO_STATUS_SUCCESS) {
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_CAIRO,
                    "cairo failed while copying tile pixels: %s", cairo_status_to_string(st));
        cairo_surface_destroy(dst);
        return NULL;
    }

    w.bytes = g_byte_array_new();
    st = cairo_surface_write_to_png_stream(dst, write_png_cb, &w);
    cairo_surface_destroy(dst);
    if (st != CAIRO_STATUS_SUCCESS) {
        g_byte_array_unref(w.bytes);
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_CAIRO,
                    "cairo failed to encode tile PNG: %s", cairo_status_to_string(st));
        return NULL;
    }

    b64 = g_base64_encode(w.bytes->data, w.bytes->len);
    g_byte_array_unref(w.bytes);
    uri = g_strconcat("data:image/png;base64,", b64, NULL);
    g_free(b64);
    return uri;
}

static gboolean append_one_tile(TileCtx *ctx, int sx, int sy, int sw, int sh,
                                GError **error)
{
    gchar *uri;
    guint idnum;

    uri = encode_png_tile_data_uri(ctx->src, sx, sy, sw, sh, error);
    if (!uri) return FALSE;

    idnum = (*ctx->tile_counter)++;
    g_string_append(ctx->out, "    <image");
    if (ctx->id_prefix && *ctx->id_prefix) {
        gchar *id = g_strdup_printf("%s__tile_%u", ctx->id_prefix, idnum);
        append_attr(ctx->out, "id", id);
        g_free(id);
    }
    g_string_append_printf(ctx->out,
                           " x=\"%.12g\" y=\"%.12g\" width=\"%.12g\" height=\"%.12g\"",
                           ctx->svg_x + ctx->svg_w * (double)sx / (double)ctx->img_w,
                           ctx->svg_y + ctx->svg_h * (double)sy / (double)ctx->img_h,
                           ctx->svg_w * (double)sw / (double)ctx->img_w,
                           ctx->svg_h * (double)sh / (double)ctx->img_h);
    append_attr(ctx->out, "preserveAspectRatio", "none");
    if (ctx->opt->inherit_image_rendering) {
            append_attr(ctx->out, "style",
                        "image-rendering:pixelated;image-rendering:crisp-edges");
    }
    append_attr(ctx->out, ctx->href_attr_name, uri);
    g_string_append(ctx->out, "/>\n");

    g_free(uri);
    return TRUE;
}

static gboolean append_tile_region(TileCtx *ctx,
                                   int lx, int ly, int lw, int lh,
                                   GError **error)
{
    int bleed = (int)ctx->opt->bleed_px;
    int sx = MAX(0, lx - bleed);
    int sy = MAX(0, ly - bleed);
    int ex = MIN(ctx->img_w, lx + lw + bleed);
    int ey = MIN(ctx->img_h, ly + lh + bleed);
    int sw = ex - sx;
    int sh = ey - sy;

    if (ctx->opt->max_data_uri_chars > 0) {
        gchar *uri = encode_png_tile_data_uri(ctx->src, sx, sy, sw, sh, error);
        if (!uri) return FALSE;
        if (strlen(uri) > ctx->opt->max_data_uri_chars &&
            ((guint)lw > ctx->opt->min_tile_px || (guint)lh > ctx->opt->min_tile_px)) {
            gboolean ok;
            g_free(uri);
            if (lw >= lh && (guint)lw > ctx->opt->min_tile_px) {
                int a = lw / 2;
                ok = append_tile_region(ctx, lx, ly, a, lh, error) &&
                     append_tile_region(ctx, lx + a, ly, lw - a, lh, error);
            } else {
                int a = lh / 2;
                ok = append_tile_region(ctx, lx, ly, lw, a, error) &&
                     append_tile_region(ctx, lx, ly + a, lw, lh - a, error);
            }
            return ok;
        }

        /* Avoid a second encode: append using the already-created uri. */
        {
            guint idnum = (*ctx->tile_counter)++;
            g_string_append(ctx->out, "    <image");
            if (ctx->id_prefix && *ctx->id_prefix) {
                gchar *id = g_strdup_printf("%s__tile_%u", ctx->id_prefix, idnum);
                append_attr(ctx->out, "id", id);
                g_free(id);
            }
            g_string_append_printf(ctx->out,
                                   " x=\"%.12g\" y=\"%.12g\" width=\"%.12g\" height=\"%.12g\"",
                                   ctx->svg_x + ctx->svg_w * (double)sx / (double)ctx->img_w,
                                   ctx->svg_y + ctx->svg_h * (double)sy / (double)ctx->img_h,
                                   ctx->svg_w * (double)sw / (double)ctx->img_w,
                                   ctx->svg_h * (double)sh / (double)ctx->img_h);
            append_attr(ctx->out, "preserveAspectRatio", "none");
            if (ctx->opt->inherit_image_rendering) {
                append_attr(ctx->out, "style",
                            "image-rendering:pixelated;image-rendering:crisp-edges");
            }
            append_attr(ctx->out, ctx->href_attr_name, uri);
            g_string_append(ctx->out, "/>\n");
            g_free(uri);
            return TRUE;
        }
    }

    return append_one_tile(ctx, sx, sy, sw, sh, error);
}

static gboolean is_geometry_or_href_attr(const gchar *name)
{
    return attr_name_is(name, "x") ||
           attr_name_is(name, "y") ||
           attr_name_is(name, "width") ||
           attr_name_is(name, "height") ||
           attr_name_is(name, "preserveAspectRatio") ||
           attr_name_is(name, "href") ||
           attr_name_is(name, "xlink:href");
}

static gboolean append_original_image_tag(const gchar *svg_utf8,
                                          const ImageTag *tag,
                                          GString *out)
{
    g_string_append_len(out, svg_utf8 + tag->start, tag->replace_end - tag->start);
    return TRUE;
}

static gboolean append_replacement_for_image(const gchar *svg_utf8,
                                             const ImageTag *tag,
                                             const SvgPngTilerOptions *opt,
                                             GString *out,
                                             gboolean *out_replaced,
                                             GError **error)
{
    const Attr *href_a = attrs_get(tag->attrs, "href");
    const Attr *xhref_a = attrs_get(tag->attrs, "xlink:href");
    const Attr *id_a = attrs_get(tag->attrs, "id");
    const Attr *x_a = attrs_get(tag->attrs, "x");
    const Attr *y_a = attrs_get(tag->attrs, "y");
    const Attr *w_a = attrs_get(tag->attrs, "width");
    const Attr *h_a = attrs_get(tag->attrs, "height");
    const Attr *par_a = attrs_get(tag->attrs, "preserveAspectRatio");
    const gchar *href_attr_name;
    const gchar *href;
    gsize href_len;
    guchar *png = NULL;
    gsize png_len = 0;
    gboolean is_data_png = FALSE;
    cairo_surface_t *src = NULL;
    int img_w, img_h;
    guint i, tile_counter = 0;
    TileCtx ctx;
    const gchar *xv, *yv, *wv, *hv;
    double svg_x, svg_y, svg_w, svg_h;
    gchar *clip_id;

    if (out_replaced) *out_replaced = FALSE;
    if (!href_a && xhref_a) href_a = xhref_a;
    if (!href_a || !href_a->value) return TRUE;

    href = href_a->value;
    href_len = strlen(href);
    href_attr_name = opt->force_href ? "href" : href_a->name;

    if (opt->disable) return append_original_image_tag(svg_utf8, tag, out);

    if (!parse_png_data_uri(href, &png, &png_len, &is_data_png, error)) return FALSE;
    if (!is_data_png) return TRUE;

    src = load_png_surface(png, png_len, error);
    g_free(png);
    if (!src) return FALSE;

    img_w = cairo_image_surface_get_width(src);
    img_h = cairo_image_surface_get_height(src);
    if (img_w <= 0 || img_h <= 0) {
        cairo_surface_destroy(src);
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_PNG,
                    "Embedded PNG has invalid dimensions %dx%d", img_w, img_h);
        return FALSE;
    }
    if (!opt->force) {
        gboolean uri_ok = opt->max_data_uri_chars == 0 ||
            href_len <= opt->max_data_uri_chars;
        gboolean pixels_ok = opt->max_image_pixels == 0 ||
            (gsize)img_w * (gsize)img_h <= opt->max_image_pixels;
        if (uri_ok && pixels_ok) {
            cairo_surface_destroy(src);
            return append_original_image_tag(svg_utf8, tag, out);
        }
    }
    if (out_replaced) *out_replaced = TRUE;

    xv = (x_a && x_a->has_value) ? x_a->value : "0";
    yv = (y_a && y_a->has_value) ? y_a->value : "0";
    /* Fallback to intrinsic PNG size if width/height are missing. */
    wv = (w_a && w_a->has_value) ? w_a->value : NULL;
    hv = (h_a && h_a->has_value) ? h_a->value : NULL;
    if (par_a && par_a->has_value &&
        g_ascii_strcasecmp(par_a->value, "none") != 0) {
        cairo_surface_destroy(src);
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_PARSE,
                    "Cannot tile large data PNG image with preserveAspectRatio=\"%s\"",
                    par_a->value);
        return FALSE;
    }
    if (!parse_svg_number_or_px(xv, 0.0, &svg_x) ||
        !parse_svg_number_or_px(yv, 0.0, &svg_y) ||
        !parse_svg_number_or_px(wv, (double)img_w, &svg_w) ||
        !parse_svg_number_or_px(hv, (double)img_h, &svg_h) ||
        svg_w <= 0.0 || svg_h <= 0.0) {
        cairo_surface_destroy(src);
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_PARSE,
                    "Cannot tile large data PNG image with non-numeric image geometry");
        return FALSE;
    }

    if (opt->emit_comment) {
        g_string_append_printf(out,
            "<!-- svg_png_tiler: split embedded PNG %dx%d into tiled data URI images; bleed=%u -->\n",
            img_w, img_h, opt->bleed_px);
    }

    clip_id = g_strdup_printf("svg_png_tiler_clip_%" G_GSIZE_FORMAT, tag->start);

    g_string_append(out, "<g");
    for (i = 0; i < tag->attrs->len; i++) {
        const Attr *a = (const Attr *)g_ptr_array_index(tag->attrs, i);
        if (is_geometry_or_href_attr(a->name)) continue;
        if (!opt->keep_original_id_on_group && attr_name_is(a->name, "id")) continue;
        if (a->has_value) append_attr(out, a->name, a->value);
        else g_string_append_printf(out, " %s", a->name);
    }
    g_string_append(out, ">\n");

    g_string_append(out, "  <defs><clipPath");
    append_attr(out, "id", clip_id);
    g_string_append(out, "><rect");
    g_string_append_printf(out,
                           " x=\"%.12g\" y=\"%.12g\" width=\"%.12g\" height=\"%.12g\"",
                           svg_x, svg_y, svg_w, svg_h);
    g_string_append(out, "/></clipPath></defs>\n");
    g_string_append(out, "  <g");
    {
        gchar *clip_url = g_strdup_printf("url(#%s)", clip_id);
        append_attr(out, "clip-path", clip_url);
        g_free(clip_url);
    }
    append_attr(out, "image-rendering", "pixelated");
    g_string_append(out, ">\n");

    memset(&ctx, 0, sizeof(ctx));
    ctx.src = src;
    ctx.img_w = img_w;
    ctx.img_h = img_h;
    ctx.svg_x = svg_x;
    ctx.svg_y = svg_y;
    ctx.svg_w = svg_w;
    ctx.svg_h = svg_h;
    ctx.opt = opt;
    ctx.href_attr_name = href_attr_name;
    ctx.id_prefix = (id_a && id_a->value && *id_a->value) ? id_a->value : NULL;
    ctx.tile_counter = &tile_counter;
    ctx.out = out;

    for (int y = 0; y < img_h; y += (int)opt->tile_px) {
        int lh = MIN((int)opt->tile_px, img_h - y);
        for (int x = 0; x < img_w; x += (int)opt->tile_px) {
            int lw = MIN((int)opt->tile_px, img_w - x);
            if (!append_tile_region(&ctx, x, y, lw, lh, error)) {
                cairo_surface_destroy(src);
                g_free(clip_id);
                return FALSE;
            }
        }
    }

    g_string_append(out, "  </g>\n</g>");
    g_free(clip_id);
    cairo_surface_destroy(src);
    return TRUE;
}

static gchar *rewrite_internal(const gchar *svg_utf8,
                               const gchar *image_id_or_null,
                               const SvgPngTilerOptions *user_opt,
                               gboolean *out_changed,
                               GError **error)
{
    SvgPngTilerOptions opt = normalized_options(user_opt);
    GString *out;
    gsize len, pos = 0, last = 0;
    guint replaced = 0;

    if (out_changed) *out_changed = FALSE;
    if (!svg_utf8) {
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_INVALID_ARGUMENT,
                    "svg_utf8 is NULL");
        return NULL;
    }

    len = strlen(svg_utf8);
    out = g_string_sized_new(len + 1024);

    while (pos < len) {
        ImageTag tag;
        GError *local_error = NULL;
        gboolean found = next_image_tag(svg_utf8, len, pos, &tag, &local_error);
        const Attr *id_a;
        const Attr *href_a;
        const Attr *xhref_a;
        gboolean should_try = FALSE;

        if (!found) {
            if (local_error) {
                g_propagate_error(error, local_error);
                g_string_free(out, TRUE);
                return NULL;
            }
            break;
        }

        id_a = attrs_get(tag.attrs, "id");
        href_a = attrs_get(tag.attrs, "href");
        xhref_a = attrs_get(tag.attrs, "xlink:href");
        if (!href_a && xhref_a) href_a = xhref_a;

        if (!image_id_or_null || (id_a && id_a->value && strcmp(id_a->value, image_id_or_null) == 0)) {
            if (href_a && href_a->value && href_looks_like_data_png_base64(href_a->value)) {
                should_try = TRUE;
            } else if (image_id_or_null) {
                image_tag_clear(&tag);
                g_string_free(out, TRUE);
                g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_DATA_URI,
                            "image id='%s' does not contain an embedded data: PNG href",
                            image_id_or_null);
                return NULL;
            }
        }

        if (should_try) {
            g_string_append_len(out, svg_utf8 + last, tag.start - last);
            gboolean replaced_this = FALSE;
            if (!append_replacement_for_image(svg_utf8, &tag, &opt, out,
                                              &replaced_this, error)) {
                image_tag_clear(&tag);
                g_string_free(out, TRUE);
                return NULL;
            }
            if (replaced_this) replaced++;
            last = tag.replace_end;
        }

        pos = tag.replace_end;
        image_tag_clear(&tag);
    }

    g_string_append_len(out, svg_utf8 + last, len - last);

    if (image_id_or_null && replaced == 0) {
        g_string_free(out, TRUE);
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_NOT_FOUND,
                    "No embedded PNG <image> with id='%s' was found", image_id_or_null);
        return NULL;
    }

    if (out_changed) *out_changed = replaced > 0;
    return g_string_free(out, FALSE);
}

gchar *svg_png_tiler_rewrite_all_full(const gchar *svg_utf8,
                                      const SvgPngTilerOptions *opt,
                                      gboolean *out_changed,
                                      GError **error)
{
    return rewrite_internal(svg_utf8, NULL, opt, out_changed, error);
}

gchar *svg_png_tiler_rewrite_all(const gchar *svg_utf8,
                                 const SvgPngTilerOptions *opt,
                                 GError **error)
{
    return rewrite_internal(svg_utf8, NULL, opt, NULL, error);
}

gchar *svg_png_tiler_rewrite_one_by_id(const gchar *svg_utf8,
                                        const gchar *image_id,
                                        const SvgPngTilerOptions *opt,
                                        GError **error)
{
    if (!image_id || !*image_id) {
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_INVALID_ARGUMENT,
                    "image_id is NULL or empty");
        return NULL;
    }
    return rewrite_internal(svg_utf8, image_id, opt, NULL, error);
}

gboolean svg_png_tiler_rewrite_file(const gchar *input_svg_path,
                                    const gchar *output_svg_path,
                                    const gchar *image_id_or_null,
                                    const SvgPngTilerOptions *opt,
                                    GError **error)
{
    gchar *in = NULL;
    gchar *out = NULL;
    gsize len = 0;
    gboolean ok;

    if (!input_svg_path || !output_svg_path) {
        g_set_error(error, SVG_PNG_TILER_ERROR, SVG_PNG_TILER_ERROR_INVALID_ARGUMENT,
                    "input_svg_path/output_svg_path must not be NULL");
        return FALSE;
    }

    if (!g_file_get_contents(input_svg_path, &in, &len, error)) {
        return FALSE;
    }

    if (image_id_or_null) out = svg_png_tiler_rewrite_one_by_id(in, image_id_or_null, opt, error);
    else out = svg_png_tiler_rewrite_all(in, opt, error);
    g_free(in);
    if (!out) return FALSE;

    ok = g_file_set_contents(output_svg_path, out, -1, error);
    g_free(out);
    return ok;
}
