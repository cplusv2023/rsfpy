/*
 * Standalone VPL to SVG converter for rsfpy.
 *
 * This intentionally does not include rsf.h/rsfplot.h and does not link to
 * Madagascar.  It reads the common vplot command stream directly and emits a
 * cropped SVG.  Text is preserved as SVG <text> in this first native pass;
 * vplot-font-to-path conversion can be layered on top of the same parser.
 */

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RPERIN 600.0
#define TXPERIN 33.0
#define TEXTVECSCALE 10.0
#define PPI 72.0
#define VPL_STROKE_BIAS_PX 0.6

#define VP_SETSTYLE         'S'
#define VP_MOVE             'm'
#define VP_DRAW             'd'
#define VP_PLINE            'L'
#define VP_PMARK            'M'
#define VP_TEXT             'T'
#define VP_GTEXT            'G'
#define VP_AREA             'A'
#define VP_OLDAREA          'a'
#define VP_BYTE_RASTER      'R'
#define VP_BIT_RASTER       'r'
#define VP_ERASE            'e'
#define VP_BREAK            'b'
#define VP_PURGE            'p'
#define VP_NOOP             'n'
#define VP_ORIGIN           'o'
#define VP_WINDOW           'w'
#define VP_FAT              'f'
#define VP_SETDASH          's'
#define VP_COLOR            'c'
#define VP_SET_COLOR_TABLE  'C'
#define VP_TXALIGN          'J'
#define VP_TXFONTPREC       'F'
#define VP_PATLOAD          'l'
#define VP_OVERLAY          'v'
#define VP_MESSAGE          'z'
#define VP_BEGIN_GROUP      '['
#define VP_END_GROUP        ']'
#define VP_OLDTEXT          't'
#define VP_BACKGROUND       'E'

#define TH_NORMAL 0
#define TH_LEFT   1
#define TH_CENTER 2
#define TH_RIGHT  3
#define TH_SYMBOL 4

#define TV_NORMAL 0
#define TV_BOTTOM 1
#define TV_BASE   2
#define TV_HALF   3
#define TV_CAP    4
#define TV_TOP    5
#define TV_SYMBOL 6

typedef struct {
    unsigned char *data;
    size_t len;
    size_t pos;
} Reader;

typedef struct {
    double xmin, xmax, ymin, ymax;
    bool empty;
} Bounds;

typedef struct {
    int r, g, b;
    int a;
    bool set;
} Color;

typedef struct {
    Color color;
    bool color_set;
    bool color_default;
    bool size_set;
    double size;
    bool fat_set;
    int fat;
    bool weight_set;
    int weight;
    bool width_set;
    double width;
    bool family_set;
    char family[128];
} Style;

typedef struct {
    Color bgcolor;
    Style frame_style;
    Style axis_style;
    Style grid_style;
    Style font_style;
    Style label_style;
    Style title_style;
    Style barlabel_style;
    Style scalebar_style;
} ConvertOptions;

typedef struct {
    int current_color;
    int fat;
    int tx_font;
    int tx_prec;
    int tx_overlay;
    int tx_halign;
    int tx_valign;
    double x;
    double y;
    bool dash_on;
    int dash_count;
    double dashes[20];
    Color colors[32768];
    int group_depth;
    char groups[32][64];
    bool clip_active;
    double clip_xmin, clip_xmax, clip_ymin, clip_ymax;
    int clip_id;
} VplState;

typedef struct {
    Bounds bounds;
    FILE *out;
    double scale;
    double pad;
    int clip_id;
    bool in_defs;
    bool warned_raster;
    bool warned_pat;
    ConvertOptions opt;
} SvgCtx;

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} ByteBuf;

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "vpl2svg: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static void warn_once(bool *flag, const char *fmt, ...)
{
    va_list ap;
    if (*flag) return;
    *flag = true;
    va_start(ap, fmt);
    fprintf(stderr, "vpl2svg: warning: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static void bb_reserve(ByteBuf *b, size_t extra)
{
    if (b->len + extra <= b->cap) return;
    size_t new_cap = b->cap ? b->cap * 2 : 1024;
    unsigned char *p;
    while (new_cap < b->len + extra) new_cap *= 2;
    p = (unsigned char *)realloc(b->data, new_cap);
    if (!p) die("out of memory");
    b->data = p;
    b->cap = new_cap;
}

static void bb_put(ByteBuf *b, unsigned char v)
{
    bb_reserve(b, 1);
    b->data[b->len++] = v;
}

static void bb_write(ByteBuf *b, const void *data, size_t n)
{
    bb_reserve(b, n);
    memcpy(b->data + b->len, data, n);
    b->len += n;
}

static void bb_free(ByteBuf *b)
{
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void bb_u32be(ByteBuf *b, uint32_t v)
{
    bb_put(b, (unsigned char)((v >> 24) & 255));
    bb_put(b, (unsigned char)((v >> 16) & 255));
    bb_put(b, (unsigned char)((v >> 8) & 255));
    bb_put(b, (unsigned char)(v & 255));
}

static uint32_t crc32_step(uint32_t crc, const unsigned char *data, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int k;
        crc ^= data[i];
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int)(crc & 1));
        }
    }
    return crc;
}

static uint32_t adler32_bytes(const unsigned char *data, size_t n)
{
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void png_chunk(ByteBuf *png, const char type[4], const unsigned char *data, size_t n)
{
    uint32_t crc = 0xffffffffu;
    bb_u32be(png, (uint32_t)n);
    bb_write(png, type, 4);
    if (n) bb_write(png, data, n);
    crc = crc32_step(crc, (const unsigned char *)type, 4);
    if (n) crc = crc32_step(crc, data, n);
    bb_u32be(png, ~crc);
}

static void write_png_rgba(ByteBuf *png, const unsigned char *rgba, int width, int height)
{
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    ByteBuf raw = {0}, z = {0}, ihdr = {0};
    size_t rowbytes = (size_t)width * 4 + 1;
    size_t y, pos = 0;
    uint32_t adler;

    bb_write(png, sig, sizeof(sig));

    bb_u32be(&ihdr, (uint32_t)width);
    bb_u32be(&ihdr, (uint32_t)height);
    bb_put(&ihdr, 8);
    bb_put(&ihdr, 6);
    bb_put(&ihdr, 0);
    bb_put(&ihdr, 0);
    bb_put(&ihdr, 0);
    png_chunk(png, "IHDR", ihdr.data, ihdr.len);
    free(ihdr.data);

    for (y = 0; y < (size_t)height; y++) {
        bb_put(&raw, 0);
        bb_write(&raw, rgba + y * (size_t)width * 4, (size_t)width * 4);
    }

    bb_put(&z, 0x78);
    bb_put(&z, 0x01);
    while (pos < raw.len) {
        size_t block = raw.len - pos;
        unsigned int len, nlen;
        if (block > 65535) block = 65535;
        len = (unsigned int)block;
        nlen = (~len) & 0xffffu;
        bb_put(&z, (pos + block == raw.len) ? 1 : 0);
        bb_put(&z, (unsigned char)(len & 255));
        bb_put(&z, (unsigned char)((len >> 8) & 255));
        bb_put(&z, (unsigned char)(nlen & 255));
        bb_put(&z, (unsigned char)((nlen >> 8) & 255));
        bb_write(&z, raw.data + pos, block);
        pos += block;
    }
    adler = adler32_bytes(raw.data, raw.len);
    bb_u32be(&z, adler);
    png_chunk(png, "IDAT", z.data, z.len);
    png_chunk(png, "IEND", NULL, 0);

    free(raw.data);
    free(z.data);
}

static void write_base64(FILE *out, const unsigned char *data, size_t n)
{
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i;
    for (i = 0; i < n; i += 3) {
        unsigned int a = data[i];
        unsigned int b = (i + 1 < n) ? data[i + 1] : 0;
        unsigned int c = (i + 2 < n) ? data[i + 2] : 0;
        fputc(tab[(a >> 2) & 63], out);
        fputc(tab[((a & 3) << 4) | ((b >> 4) & 15)], out);
        fputc((i + 1 < n) ? tab[((b & 15) << 2) | ((c >> 6) & 3)] : '=', out);
        fputc((i + 2 < n) ? tab[c & 63] : '=', out);
    }
}

static unsigned char *read_all_file(const char *path, size_t *len)
{
    FILE *fp = path ? fopen(path, "rb") : stdin;
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0;

    if (!fp) die("cannot open %s: %s", path, strerror(errno));
    for (;;) {
        size_t got;
        if (n + 8192 > cap) {
            size_t new_cap = cap ? cap * 2 : 65536;
            unsigned char *p;
            while (new_cap < n + 8192) new_cap *= 2;
            p = (unsigned char *)realloc(buf, new_cap);
            if (!p) die("out of memory");
            buf = p;
            cap = new_cap;
        }
        got = fread(buf + n, 1, cap - n, fp);
        n += got;
        if (got == 0) {
            if (ferror(fp)) die("read failed");
            break;
        }
    }
    if (path) fclose(fp);
    *len = n;
    return buf;
}

static bool stream_u8(FILE *fp, ByteBuf *out, unsigned char *v)
{
    int c = fgetc(fp);
    if (c == EOF) {
        if (ferror(fp)) die("read failed");
        return false;
    }
    *v = (unsigned char)c;
    if (out) bb_put(out, *v);
    return true;
}

static bool stream_i16(FILE *fp, ByteBuf *out, int *v)
{
    unsigned char lo, hi;
    uint16_t u;
    if (!stream_u8(fp, out, &lo) || !stream_u8(fp, out, &hi)) return false;
    u = (uint16_t)lo | ((uint16_t)hi << 8);
    *v = (int)(int16_t)u;
    return true;
}

static bool stream_cstring(FILE *fp, ByteBuf *out)
{
    unsigned char c;
    do {
        if (!stream_u8(fp, out, &c)) return false;
    } while (c != '\0');
    return true;
}

static void stream_i16_many(FILE *fp, ByteBuf *out, int n)
{
    int i, tmp;
    for (i = 0; i < n; i++) {
        if (!stream_i16(fp, out, &tmp)) die("truncated VPL command");
    }
}

static void stream_bytes(FILE *fp, ByteBuf *out, int n)
{
    int i;
    unsigned char tmp;
    for (i = 0; i < n; i++) {
        if (!stream_u8(fp, out, &tmp)) die("truncated VPL command");
    }
}

static void stream_raster_payload(FILE *fp, ByteBuf *out, bool bit,
                                  int xpix, int ypix)
{
    int yrast = 0;
    while (yrast < ypix) {
        int rep, pos = 0;
        if (!stream_i16(fp, out, &rep) || rep <= 0) die("truncated raster payload");
        while (pos < xpix) {
            int num_pat, num_byte, bytes;
            if (!stream_i16(fp, out, &num_pat) ||
                !stream_i16(fp, out, &num_byte)) die("truncated raster payload");
            if (num_pat <= 0 || num_byte <= 0) die("invalid raster run");
            bytes = bit ? ((num_byte + 7) / 8) : num_byte;
            stream_bytes(fp, out, bytes);
            pos += num_pat * num_byte;
        }
        yrast += rep;
    }
}

static bool stream_next_command(FILE *fp, ByteBuf *out, unsigned char *cmd_out)
{
    unsigned char cmd, style;
    int a, b, c, d, n;
    size_t cmd_offset = out ? out->len : 0;

    if (!stream_u8(fp, out, &cmd)) return false;
    *cmd_out = cmd;

    switch (cmd) {
        case VP_SETSTYLE:
            if (!stream_u8(fp, out, &style)) die("truncated VPL command");
            break;
        case VP_MOVE:
        case VP_DRAW:
        case VP_ORIGIN:
        case VP_TXALIGN:
            stream_i16_many(fp, out, 2);
            break;
        case VP_WINDOW:
        case VP_SET_COLOR_TABLE:
        case VP_PATLOAD:
            if (!stream_i16(fp, out, &a) || !stream_i16(fp, out, &b) ||
                !stream_i16(fp, out, &c) || !stream_i16(fp, out, &d)) {
                die("truncated VPL command");
            }
            if (cmd == VP_PATLOAD) {
                if (b >= 0) {
                    stream_i16_many(fp, out, b * c);
                } else {
                    stream_i16_many(fp, out, c * 2 * 4);
                }
            }
            break;
        case VP_PLINE:
        case VP_AREA:
            if (!stream_i16(fp, out, &n)) die("truncated VPL command");
            stream_i16_many(fp, out, n * 2);
            break;
        case VP_PMARK:
            if (!stream_i16(fp, out, &n)) die("truncated VPL command");
            stream_i16_many(fp, out, 2 + n * 2);
            break;
        case VP_OLDAREA:
            if (!stream_i16(fp, out, &n)) die("truncated VPL command");
            stream_i16_many(fp, out, 3 + n * 2);
            break;
        case VP_TEXT:
            stream_i16_many(fp, out, 2);
            if (!stream_cstring(fp, out)) die("truncated VPL command");
            break;
        case VP_GTEXT:
            stream_i16_many(fp, out, 4);
            if (!stream_cstring(fp, out)) die("truncated VPL command");
            break;
        case VP_BYTE_RASTER:
        case VP_BIT_RASTER: {
            int orient, offset, x0, y0, x1, y1, xpix, ypix;
            (void)orient; (void)offset; (void)x0; (void)y0; (void)x1; (void)y1;
            if (!stream_i16(fp, out, &orient) || !stream_i16(fp, out, &offset) ||
                !stream_i16(fp, out, &x0) || !stream_i16(fp, out, &y0) ||
                !stream_i16(fp, out, &x1) || !stream_i16(fp, out, &y1) ||
                !stream_i16(fp, out, &xpix) || !stream_i16(fp, out, &ypix)) {
                die("truncated VPL raster header");
            }
            stream_raster_payload(fp, out, cmd == VP_BIT_RASTER, xpix, ypix);
            break;
        }
        case VP_FAT:
        case VP_COLOR:
        case VP_OVERLAY:
        case VP_OLDTEXT:
            stream_i16_many(fp, out, 1);
            if (cmd == VP_OLDTEXT && !stream_cstring(fp, out)) die("truncated VPL command");
            break;
        case VP_SETDASH:
            if (!stream_i16(fp, out, &n)) die("truncated VPL command");
            stream_i16_many(fp, out, n * 2);
            break;
        case VP_TXFONTPREC:
            stream_i16_many(fp, out, 3);
            break;
        case VP_MESSAGE:
        case VP_BEGIN_GROUP:
            if (!stream_cstring(fp, out)) die("truncated VPL command");
            break;
        case VP_BREAK:
        case VP_ERASE:
        case VP_PURGE:
        case VP_NOOP:
        case VP_BACKGROUND:
        case VP_END_GROUP:
            break;
        default:
            die("invalid VPL command 0x%02x in stream at byte %zu", cmd, cmd_offset);
    }
    return true;
}

static bool r_u8(Reader *r, unsigned char *v)
{
    if (r->pos >= r->len) return false;
    *v = r->data[r->pos++];
    return true;
}

static bool r_i16(Reader *r, int *v)
{
    uint16_t u;
    if (r->pos + 2 > r->len) return false;
    u = (uint16_t)r->data[r->pos] | ((uint16_t)r->data[r->pos + 1] << 8);
    r->pos += 2;
    *v = (int)(int16_t)u;
    return true;
}

static bool r_cstring(Reader *r, char **out)
{
    size_t start = r->pos;
    char *s;
    while (r->pos < r->len && r->data[r->pos] != '\0') r->pos++;
    if (r->pos >= r->len) return false;
    s = (char *)malloc(r->pos - start + 1);
    if (!s) die("out of memory");
    memcpy(s, r->data + start, r->pos - start);
    s[r->pos - start] = '\0';
    r->pos++;
    *out = s;
    return true;
}

static void bounds_add(Bounds *b, double x, double y)
{
    if (b->empty) {
        b->xmin = b->xmax = x;
        b->ymin = b->ymax = y;
        b->empty = false;
        return;
    }
    if (x < b->xmin) b->xmin = x;
    if (x > b->xmax) b->xmax = x;
    if (y < b->ymin) b->ymin = y;
    if (y > b->ymax) b->ymax = y;
}

static void bounds_add_fat(Bounds *b, double x, double y, int fat)
{
    double pad = (fat > 1 ? fat : 1) / 2.0;
    bounds_add(b, x - pad, y - pad);
    bounds_add(b, x + pad, y + pad);
}

static double u_to_px(double v)
{
    return v * (PPI / RPERIN);
}

static double px_to_u(double v)
{
    return v * (RPERIN / PPI);
}

static double sx(SvgCtx *ctx, double x)
{
    return u_to_px(x - ctx->bounds.xmin + ctx->pad);
}

static double sy(SvgCtx *ctx, double y)
{
    return u_to_px(ctx->bounds.ymax - y + ctx->pad);
}

static bool point_in_clip(VplState *st, double x, double y)
{
    if (!st->clip_active) return true;
    return x >= st->clip_xmin && x <= st->clip_xmax &&
           y >= st->clip_ymin && y <= st->clip_ymax;
}

static void bounds_add_clipped(Bounds *b, VplState *st, double x, double y)
{
    if (!point_in_clip(st, x, y)) return;
    bounds_add(b, x, y);
}

static int clip_outcode(VplState *st, double x, double y)
{
    int code = 0;
    if (x < st->clip_xmin) code |= 1;
    else if (x > st->clip_xmax) code |= 2;
    if (y < st->clip_ymin) code |= 4;
    else if (y > st->clip_ymax) code |= 8;
    return code;
}

static bool clip_line_to_window(VplState *st,
                                double *x0, double *y0,
                                double *x1, double *y1)
{
    int c0, c1;

    if (!st->clip_active) return true;

    c0 = clip_outcode(st, *x0, *y0);
    c1 = clip_outcode(st, *x1, *y1);

    while (true) {
        double x = 0.0, y = 0.0;
        int c;

        if (!(c0 | c1)) return true;
        if (c0 & c1) return false;

        c = c0 ? c0 : c1;
        if (c & 8) {
            if (*y1 == *y0) return false;
            x = *x0 + (*x1 - *x0) * (st->clip_ymax - *y0) / (*y1 - *y0);
            y = st->clip_ymax;
        } else if (c & 4) {
            if (*y1 == *y0) return false;
            x = *x0 + (*x1 - *x0) * (st->clip_ymin - *y0) / (*y1 - *y0);
            y = st->clip_ymin;
        } else if (c & 2) {
            if (*x1 == *x0) return false;
            y = *y0 + (*y1 - *y0) * (st->clip_xmax - *x0) / (*x1 - *x0);
            x = st->clip_xmax;
        } else if (c & 1) {
            if (*x1 == *x0) return false;
            y = *y0 + (*y1 - *y0) * (st->clip_xmin - *x0) / (*x1 - *x0);
            x = st->clip_xmin;
        }

        if (c == c0) {
            *x0 = x;
            *y0 = y;
            c0 = clip_outcode(st, *x0, *y0);
        } else {
            *x1 = x;
            *y1 = y;
            c1 = clip_outcode(st, *x1, *y1);
        }
    }
}

static void emit_clip_attr(SvgCtx *ctx, VplState *st)
{
    (void)ctx;
    if (st->clip_active && st->clip_id > 0) {
        fprintf(ctx->out, " clip-path=\"url(#vplclip%d)\"", st->clip_id);
    }
}

static void emit_clip_def(SvgCtx *ctx, VplState *st)
{
    double x, y, w, h;
    if (!ctx || !ctx->out || !st->clip_active || st->clip_id <= 0) return;
    x = sx(ctx, st->clip_xmin);
    y = sy(ctx, st->clip_ymax);
    w = u_to_px(st->clip_xmax - st->clip_xmin);
    h = u_to_px(st->clip_ymax - st->clip_ymin);
    if (w <= 0.0 || h <= 0.0) return;
    fprintf(ctx->out,
            "<defs><clipPath id=\"vplclip%d\"><rect x=\"%.6g\" y=\"%.6g\" width=\"%.6g\" height=\"%.6g\"/></clipPath></defs>\n",
            st->clip_id, x, y, w, h);
}

static void svg_escape(FILE *out, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        switch (*p) {
            case '&': fputs("&amp;", out); break;
            case '<': fputs("&lt;", out); break;
            case '>': fputs("&gt;", out); break;
            case '"': fputs("&quot;", out); break;
            default:
                if (*p >= 32 || *p == '\t' || *p == '\n') fputc(*p, out);
                break;
        }
        p++;
    }
}

static const char *greek_letter_ascii(int ch)
{
    switch (ch) {
        case 'a': return "\xce\xb1";
        case 'b': return "\xce\xb2";
        case 'g': return "\xce\xb3";
        case 'd': return "\xce\xb4";
        case 'e': return "\xce\xb5";
        case 'z': return "\xce\xb6";
        case 'h': return "\xce\xb7";
        case 'q': return "\xce\xb8";
        case 'i': return "\xce\xb9";
        case 'k': return "\xce\xba";
        case 'l': return "\xce\xbb";
        case 'm': return "\xce\xbc";
        case 'n': return "\xce\xbd";
        case 'x': return "\xce\xbe";
        case 'o': return "\xce\xbf";
        case 'p': return "\xcf\x80";
        case 'r': return "\xcf\x81";
        case 's': return "\xcf\x83";
        case 't': return "\xcf\x84";
        case 'u': return "\xcf\x85";
        case 'f': return "\xcf\x86";
        case 'c': return "\xcf\x87";
        case 'y': return "\xcf\x88";
        case 'w': return "\xcf\x89";
        case 'A': return "\xce\x91";
        case 'B': return "\xce\x92";
        case 'G': return "\xce\x93";
        case 'D': return "\xce\x94";
        case 'E': return "\xce\x95";
        case 'Z': return "\xce\x96";
        case 'H': return "\xce\x97";
        case 'Q': return "\xce\x98";
        case 'I': return "\xce\x99";
        case 'K': return "\xce\x9a";
        case 'L': return "\xce\x9b";
        case 'M': return "\xce\x9c";
        case 'N': return "\xce\x9d";
        case 'X': return "\xce\x9e";
        case 'O': return "\xce\x9f";
        case 'P': return "\xce\xa0";
        case 'R': return "\xce\xa1";
        case 'S': return "\xce\xa3";
        case 'T': return "\xce\xa4";
        case 'U': return "\xce\xa5";
        case 'F': return "\xce\xa6";
        case 'C': return "\xce\xa7";
        case 'Y': return "\xce\xa8";
        case 'W': return "\xce\xa9";
        default: return NULL;
    }
}

static char *plain_vplot_text(const char *s)
{
    size_t n = strlen(s);
    char *out = (char *)malloc(n * 4 + 1);
    size_t i = 0, j = 0;
    bool greek = false;
    if (!out) die("out of memory");
    while (i < n) {
        if (s[i] == '\\' && s[i + 1]) {
            char c = s[++i];
            if (c == 'F') {
                char *end = NULL;
                long font;
                i++;
                font = strtol(s + i, &end, 10);
                if (end != s + i) {
                    greek = (font == 9);
                    i = (size_t)(end - s);
                    if (i < n && s[i] == ' ') i++;
                }
                continue;
            }
            if (c == 'C' || c == 's' || c == 'v' || c == '_' || c == '^') {
                i++;
                while (i < n && (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+')) i++;
                continue;
            }
            if (c == 'n') {
                out[j++] = '\n';
                i++;
                continue;
            }
            out[j++] = c;
            i++;
            continue;
        }
        if (greek) {
            const char *mapped = greek_letter_ascii((unsigned char)s[i]);
            if (mapped) {
                size_t m = strlen(mapped);
                memcpy(out + j, mapped, m);
                j += m;
                i++;
                continue;
            }
        }
        out[j++] = s[i++];
    }
    out[j] = '\0';
    return out;
}

static Color color_rgba(int r, int g, int b, int a)
{
    Color c;
    c.r = r < 0 ? 0 : (r > 255 ? 255 : r);
    c.g = g < 0 ? 0 : (g > 255 ? 255 : g);
    c.b = b < 0 ? 0 : (b > 255 ? 255 : b);
    c.a = a < 0 ? 0 : (a > 255 ? 255 : a);
    c.set = true;
    return c;
}

static Color invert_color(Color c)
{
    return color_rgba(255 - c.r, 255 - c.g, 255 - c.b, 255);
}

static int hex_value(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool parse_hex_color(const char *s, Color *out)
{
    size_t n = strlen(s);
    int vals[8], i;
    if (n != 4 && n != 5 && n != 7 && n != 9) return false;
    if (s[0] != '#') return false;
    for (i = 1; s[i]; i++) {
        vals[i - 1] = hex_value((unsigned char)s[i]);
        if (vals[i - 1] < 0) return false;
    }
    if (n == 4 || n == 5) {
        int r = vals[0] * 17;
        int g = vals[1] * 17;
        int b = vals[2] * 17;
        int a = (n == 5) ? vals[3] * 17 : 255;
        *out = color_rgba(r, g, b, a);
        return true;
    }
    *out = color_rgba(vals[0] * 16 + vals[1],
                      vals[2] * 16 + vals[3],
                      vals[4] * 16 + vals[5],
                      n == 9 ? vals[6] * 16 + vals[7] : 255);
    return true;
}

static bool streq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool starts_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (!*s) return false;
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
        s++;
        prefix++;
    }
    return true;
}

static bool ends_ci(const char *s, const char *suffix)
{
    size_t ns = strlen(s), nf = strlen(suffix);
    if (nf > ns) return false;
    return streq_ci(s + ns - nf, suffix);
}

static bool parse_named_color(const char *s, Color *out)
{
    if (streq_ci(s, "k") || streq_ci(s, "black")) {
        *out = color_rgba(0, 0, 0, 255);
    } else if (streq_ci(s, "w") || streq_ci(s, "white")) {
        *out = color_rgba(255, 255, 255, 255);
    } else if (streq_ci(s, "r") || streq_ci(s, "red")) {
        *out = color_rgba(255, 0, 0, 255);
    } else if (streq_ci(s, "g") || streq_ci(s, "green")) {
        *out = color_rgba(0, 128, 0, 255);
    } else if (streq_ci(s, "b") || streq_ci(s, "blue")) {
        *out = color_rgba(0, 0, 255, 255);
    } else if (streq_ci(s, "c") || streq_ci(s, "cyan")) {
        *out = color_rgba(0, 255, 255, 255);
    } else if (streq_ci(s, "m") || streq_ci(s, "magenta")) {
        *out = color_rgba(255, 0, 255, 255);
    } else if (streq_ci(s, "y") || streq_ci(s, "yellow")) {
        *out = color_rgba(255, 255, 0, 255);
    } else if (streq_ci(s, "gray") || streq_ci(s, "grey")) {
        *out = color_rgba(128, 128, 128, 255);
    } else if (streq_ci(s, "none") || streq_ci(s, "transparent")) {
        *out = color_rgba(255, 255, 255, 0);
    } else {
        return false;
    }
    return true;
}

static bool parse_color_spec(const char *s, Color *out)
{
    char *end = NULL;
    double v;
    if (!s || !*s) return false;
    if (parse_hex_color(s, out) || parse_named_color(s, out)) return true;
    v = strtod(s, &end);
    if (end && *end == '\0' && v >= 0.0 && v <= 1.0) {
        int g = (int)lrint(v * 255.0);
        *out = color_rgba(g, g, g, 255);
        return true;
    }
    return false;
}

static void state_init(VplState *st)
{
    int i;
    const int red[]   = {255, 255,   0,   0, 255, 255,   0, 0};
    const int green[] = {255, 255, 255, 255,   0,   0,   0, 0};
    const int blue[]  = {255,   0, 255,   0, 255,   0, 255, 0};
    memset(st, 0, sizeof(*st));
    st->current_color = 7;
    st->fat = 1;
    st->tx_font = 3;
    st->tx_prec = 2;
    st->tx_overlay = 0;
    for (i = 0; i < 32768; i++) {
        st->colors[i].r = st->colors[i].g = st->colors[i].b = 0;
        st->colors[i].a = 255;
        st->colors[i].set = false;
    }
    for (i = 0; i < 8; i++) {
        st->colors[i].r = red[i];
        st->colors[i].g = green[i];
        st->colors[i].b = blue[i];
        st->colors[i].a = 255;
        st->colors[i].set = true;
    }
}

static Color state_color(VplState *st)
{
    Color c;
    if (st->current_color >= 0 && st->current_color < 32768 &&
        st->colors[st->current_color].set) {
        return st->colors[st->current_color];
    }
    c.r = c.g = c.b = 0;
    c.a = 255;
    c.set = true;
    return c;
}

static void svg_color(FILE *out, Color c)
{
    fprintf(out, "#%02x%02x%02x", c.r & 255, c.g & 255, c.b & 255);
}

static void svg_opacity(FILE *out, Color c, const char *name)
{
    if (c.a >= 0 && c.a < 255) {
        fprintf(out, " %s=\"%.6g\"", name, c.a / 255.0);
    }
}

static bool contains_ci(const char *s, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0) return true;
    while (*s) {
        size_t i;
        for (i = 0; i < n; i++) {
            if (!s[i]) return false;
            if (tolower((unsigned char)s[i]) !=
                tolower((unsigned char)needle[i])) break;
        }
        if (i == n) return true;
        s++;
    }
    return false;
}

static void group_push(VplState *st, const char *name)
{
    if (st->group_depth >= 32) return;
    snprintf(st->groups[st->group_depth], sizeof(st->groups[st->group_depth]),
             "%s", name ? name : "");
    st->group_depth++;
}

static void group_pop(VplState *st)
{
    if (st->group_depth > 0) st->group_depth--;
}

static bool group_has(VplState *st, const char *needle)
{
    int i;
    for (i = st->group_depth - 1; i >= 0; i--) {
        if (contains_ci(st->groups[i], needle)) return true;
    }
    return false;
}

static bool in_axis_group(VplState *st)
{
    return group_has(st, "axis") ||
           group_has(st, "tic marks");
}

static bool in_grid_group(VplState *st)
{
    return group_has(st, "grid");
}

static bool in_frame_group(VplState *st)
{
    return group_has(st, "frame") ||
           group_has(st, "corner") ||
           group_has(st, "plot frame") ||
           group_has(st, "scalebar frame");
}

static bool in_title_group(VplState *st)
{
    return group_has(st, "title");
}

static bool in_scalebar_group(VplState *st)
{
    return group_has(st, "scalebar");
}

static bool in_frame_number_group(VplState *st)
{
    return group_has(st, "frame number");
}

static bool in_cube_indicator_group(VplState *st)
{
    return group_has(st, "colored lines");
}

static bool in_label_group(VplState *st)
{
    return group_has(st, "axis") ||
           group_has(st, "axis label") ||
           group_has(st, "tic marks");
}

static bool style_color(const Style *style, Color *out)
{
    if (!style->color_set) return false;
    *out = style->color;
    return true;
}

static bool style_explicit_color(const Style *style, Color *out)
{
    if (!style->color_set || style->color_default) return false;
    *out = style->color;
    return true;
}

static bool style_stroke_width_px(const Style *style, double *out)
{
    if (style->width_set) {
        *out = style->width;
        return true;
    }
    if (style->fat_set) {
        *out = (double)style->fat;
        return true;
    }
    return false;
}

static Color effective_stroke_color(SvgCtx *ctx, VplState *st)
{
    Color c;
    if (in_cube_indicator_group(st)) {
        if (style_explicit_color(&ctx->opt.axis_style, &c) ||
            style_explicit_color(&ctx->opt.frame_style, &c)) return c;
    } else if (in_grid_group(st)) {
        if (style_color(&ctx->opt.grid_style, &c) ||
            style_color(&ctx->opt.frame_style, &c)) return c;
    } else if (in_axis_group(st)) {
        if (style_color(&ctx->opt.axis_style, &c) ||
            style_color(&ctx->opt.frame_style, &c)) return c;
    } else if (in_frame_group(st)) {
        if (style_color(&ctx->opt.axis_style, &c) ||
            style_color(&ctx->opt.frame_style, &c)) return c;
    }
    return state_color(st);
}

static Color effective_text_color(SvgCtx *ctx, VplState *st)
{
    Color c;
    if (in_title_group(st)) {
        if (style_color(&ctx->opt.title_style, &c) ||
            style_color(&ctx->opt.font_style, &c)) return c;
    } else if (in_scalebar_group(st)) {
        if (style_color(&ctx->opt.scalebar_style, &c) ||
            style_color(&ctx->opt.barlabel_style, &c) ||
            style_color(&ctx->opt.font_style, &c)) return c;
    } else if (in_label_group(st)) {
        if (style_color(&ctx->opt.label_style, &c) ||
            style_color(&ctx->opt.font_style, &c)) return c;
    }
    if (style_color(&ctx->opt.font_style, &c)) return c;
    return state_color(st);
}

static double effective_stroke_width(SvgCtx *ctx, VplState *st)
{
    int fat = st->fat > 1 ? st->fat : 1;
    const Style *specific = NULL;
    double width;
    if (in_grid_group(st)) {
        specific = &ctx->opt.grid_style;
    } else if (in_axis_group(st) || in_frame_group(st)) {
        specific = &ctx->opt.axis_style;
    }
    if (specific) {
        if (style_stroke_width_px(specific, &width)) return width;
    }
    if (in_grid_group(st) || in_axis_group(st) || in_frame_group(st)) {
        if (style_stroke_width_px(&ctx->opt.frame_style, &width)) return width;
    }
    if (fat < 1) fat = 1;
    /* VPL's 600-units-per-inch pen widths are much finer than a practical
     * screen/SVG stroke.  Preserve their relative scale while adding a
     * readable baseline: 0.12 * fat + 0.6 px. */
    return u_to_px(fat) + VPL_STROKE_BIAS_PX;
}

static const Style *specific_text_style(SvgCtx *ctx, VplState *st)
{
    if (in_title_group(st)) return &ctx->opt.title_style;
    if (in_scalebar_group(st)) {
        if (ctx->opt.scalebar_style.color_set ||
            ctx->opt.scalebar_style.size_set ||
            ctx->opt.scalebar_style.fat_set ||
            ctx->opt.scalebar_style.weight_set ||
            ctx->opt.scalebar_style.family_set) {
            return &ctx->opt.scalebar_style;
        }
        return &ctx->opt.barlabel_style;
    }
    if (in_label_group(st)) return &ctx->opt.label_style;
    return NULL;
}

static int effective_text_weight(SvgCtx *ctx, VplState *st)
{
    int fat = st->fat > 1 ? st->fat : 1;
    const Style *specific = specific_text_style(ctx, st);
    if (specific) {
        if (specific->weight_set) return specific->weight;
        if (specific->fat_set) fat = specific->fat + 1;
    }
    if (ctx->opt.font_style.weight_set) return ctx->opt.font_style.weight;
    if (ctx->opt.font_style.fat_set) fat = ctx->opt.font_style.fat + 1;
    if (fat < 1) fat = 1;
    if (fat > 6) fat = 6;
    return 400 + (fat - 1) * 100;
}

static double effective_text_size(SvgCtx *ctx, VplState *st)
{
    const Style *specific = specific_text_style(ctx, st);
    if (specific && specific->size_set && specific->size > 0.0) return specific->size;
    if (ctx->opt.font_style.size_set && ctx->opt.font_style.size > 0.0) return ctx->opt.font_style.size;
    return 0.0;
}

static const char *effective_font_family(SvgCtx *ctx, VplState *st)
{
    const Style *specific = specific_text_style(ctx, st);
    if (specific && specific->family_set) return specific->family;
    if (ctx->opt.font_style.family_set) return ctx->opt.font_style.family;
    return "serif";
}

static void svg_dash_attrs(FILE *out, VplState *st)
{
    int i;
    if (!st->dash_on || st->dash_count <= 0) return;
    fputs(" stroke-dasharray=\"", out);
    for (i = 0; i < st->dash_count * 2; i++) {
        if (i) fputc(' ', out);
        fprintf(out, "%.3g", u_to_px(st->dashes[i]));
    }
    fputc('"', out);
}

static void emit_line(SvgCtx *ctx, VplState *st, double x1, double y1,
                      double x2, double y2)
{
    Color c = effective_stroke_color(ctx, st);
    if (st->fat < 0) return;
    fprintf(ctx->out, "<path d=\"M %.6g %.6g L %.6g %.6g\" fill=\"none\" stroke=\"",
            sx(ctx, x1), sy(ctx, y1), sx(ctx, x2), sy(ctx, y2));
    svg_color(ctx->out, c);
    fprintf(ctx->out, "\" stroke-width=\"%.6g\" stroke-linecap=\"butt\" stroke-linejoin=\"round\"",
            effective_stroke_width(ctx, st));
    svg_opacity(ctx->out, c, "stroke-opacity");
    svg_dash_attrs(ctx->out, st);
    emit_clip_attr(ctx, st);
    fputs("/>\n", ctx->out);
}

static void bbox_line(SvgCtx *ctx, VplState *st, double x1, double y1,
                      double x2, double y2)
{
    double pad = px_to_u(effective_stroke_width(ctx, st));
    if (!clip_line_to_window(st, &x1, &y1, &x2, &y2)) return;
    bounds_add_fat(&ctx->bounds, x1, y1, (int)ceil(pad));
    bounds_add_fat(&ctx->bounds, x2, y2, (int)ceil(pad));
}

static bool is_corner_cleanup_fill(VplState *st, int npts, bool fill, bool stroke)
{
    return fill && !stroke &&
           (npts == 3 || npts == 4) &&
           st->current_color == 0 &&
           group_has(st, "corner");
}

static void emit_polygon(SvgCtx *ctx, VplState *st, double *pts, int npts,
                         bool fill, bool stroke)
{
    int i;
    Color c = effective_stroke_color(ctx, st);
    if (npts <= 0) return;
    if (is_corner_cleanup_fill(st, npts, fill, stroke)) c = ctx->opt.bgcolor;
    fputs("<path d=\"", ctx->out);
    fprintf(ctx->out, "M %.6g %.6g", sx(ctx, pts[0]), sy(ctx, pts[1]));
    for (i = 1; i < npts; i++) {
        fprintf(ctx->out, " L %.6g %.6g", sx(ctx, pts[2 * i]), sy(ctx, pts[2 * i + 1]));
    }
    fputs(" Z\" ", ctx->out);
    if (fill) {
        fputs("fill=\"", ctx->out);
        svg_color(ctx->out, c);
        fputs("\"", ctx->out);
        svg_opacity(ctx->out, c, "fill-opacity");
        fputc(' ', ctx->out);
    } else {
        fputs("fill=\"none\" ", ctx->out);
    }
    if (stroke && st->fat >= 0) {
        fputs("stroke=\"", ctx->out);
        svg_color(ctx->out, c);
        fprintf(ctx->out, "\" stroke-width=\"%.6g\" stroke-linejoin=\"round\"",
                effective_stroke_width(ctx, st));
        svg_opacity(ctx->out, c, "stroke-opacity");
    } else {
        fputs("stroke=\"none\"", ctx->out);
    }
    emit_clip_attr(ctx, st);
    fputs("/>\n", ctx->out);
}

static void bbox_polygon(SvgCtx *ctx, VplState *st, double *pts, int npts)
{
    int i;
    for (i = 0; i < npts; i++) {
        bounds_add_clipped(&ctx->bounds, st, pts[2 * i], pts[2 * i + 1]);
    }
    if (st->clip_active && npts > 0) {
        bounds_add(&ctx->bounds, st->clip_xmin, st->clip_ymin);
        bounds_add(&ctx->bounds, st->clip_xmax, st->clip_ymax);
    }
}

static int max_text_line_len(const char *plain)
{
    int max_len = 0, len = 0;
    const char *p;
    for (p = plain; *p; p++) {
        if (*p == '\n') {
            if (len > max_len) max_len = len;
            len = 0;
        } else {
            len++;
        }
    }
    if (len > max_len) max_len = len;
    return max_len;
}

static double text_align_dy_units(VplState *st, double h)
{
    switch (st->tx_valign) {
        case TV_TOP:
            return 0.81 * h;
        case TV_CAP:
            return 0.654 * h;
        case TV_SYMBOL:
        case TV_HALF:
            return 0.327 * h;
        case TV_BOTTOM:
            return -0.1666666667 * h;
        case TV_NORMAL:
        case TV_BASE:
        default:
            return 0.0;
    }
}

static const char *text_anchor(VplState *st)
{
    switch (st->tx_halign) {
        case TH_CENTER:
        case TH_SYMBOL:
            return "middle";
        case TH_RIGHT:
            return "end";
        case TH_NORMAL:
        case TH_LEFT:
        default:
            return "start";
    }
}

static void normalize_text_vectors(SvgCtx *ctx, VplState *st,
                                   double *pathx, double *pathy,
                                   double *upx, double *upy)
{
    double plen = hypot(*pathx, *pathy);
    double ulen = hypot(*upx, *upy);
    double target;
    if (plen <= 0.0 && ulen <= 0.0) {
        *pathx = RPERIN / TXPERIN;
        *pathy = 0.0;
        *upx = 0.0;
        *upy = RPERIN / TXPERIN;
        plen = ulen = RPERIN / TXPERIN;
    } else if (plen <= 0.0) {
        *pathx = *upy;
        *pathy = -*upx;
        plen = hypot(*pathx, *pathy);
    } else if (ulen <= 0.0) {
        *upx = -*pathy;
        *upy = *pathx;
        ulen = hypot(*upx, *upy);
    }
    target = effective_text_size(ctx, st);
    if (target > 0.0) {
        target *= RPERIN / TXPERIN;
        if (plen > 0.0) {
            *pathx *= target / plen;
            *pathy *= target / plen;
        }
        if (ulen > 0.0) {
            *upx *= target / ulen;
            *upy *= target / ulen;
        }
    }
}

static void text_bbox_generic(SvgCtx *ctx, VplState *st, const char *raw,
                              double x, double y,
                              double pathx, double pathy,
                              double upx, double upy)
{
    char *plain = plain_vplot_text(raw);
    int chars = max_text_line_len(plain);
    double plen, ulen, px, py, ux, uy, width, low, high, hx;
    double x0, y0, corners[8];
    int i;
    normalize_text_vectors(ctx, st, &pathx, &pathy, &upx, &upy);
    plen = hypot(pathx, pathy);
    ulen = hypot(upx, upy);
    px = plen > 0.0 ? pathx / plen : 1.0;
    py = plen > 0.0 ? pathy / plen : 0.0;
    ux = ulen > 0.0 ? upx / ulen : 0.0;
    uy = ulen > 0.0 ? upy / ulen : 1.0;
    width = (double)(chars > 0 ? chars : 1) * plen * 0.72;
    low = -text_align_dy_units(st, ulen) - 0.22 * ulen;
    high = -text_align_dy_units(st, ulen) + 0.92 * ulen;
    if (st->tx_valign == TV_BOTTOM) {
        low = -text_align_dy_units(st, ulen) - 0.04 * ulen;
        high = -text_align_dy_units(st, ulen) + 1.04 * ulen;
    }
    if (st->tx_halign == TH_CENTER || st->tx_halign == TH_SYMBOL) {
        hx = -0.5 * width;
    } else if (st->tx_halign == TH_RIGHT) {
        hx = -width;
    } else {
        hx = 0.0;
    }

    x0 = x + px * hx;
    y0 = y + py * hx;
    corners[0] = x0 + ux * low;
    corners[1] = y0 + uy * low;
    corners[2] = x0 + ux * high;
    corners[3] = y0 + uy * high;
    corners[4] = x0 + px * width + ux * low;
    corners[5] = y0 + py * width + uy * low;
    corners[6] = x0 + px * width + ux * high;
    corners[7] = y0 + py * width + uy * high;
    for (i = 0; i < 4; i++) bounds_add(&ctx->bounds, corners[2 * i], corners[2 * i + 1]);
    free(plain);
}

static void emit_text_generic(SvgCtx *ctx, VplState *st, const char *raw,
                              double x, double y,
                              double pathx, double pathy,
                              double upx, double upy)
{
    char *plain = plain_vplot_text(raw);
    double px = sx(ctx, x);
    double py = sy(ctx, y);
    Color c = effective_text_color(ctx, st);
    const char *family = effective_font_family(ctx, st);
    double font_units, font_px, angle, dy;
    int weight = effective_text_weight(ctx, st);
    normalize_text_vectors(ctx, st, &pathx, &pathy, &upx, &upy);
    font_units = hypot(upx, upy);
    if (font_units <= 0.0) font_units = RPERIN / TXPERIN;
    font_px = u_to_px(font_units);
    dy = u_to_px(text_align_dy_units(st, font_units));
    angle = atan2(pathy, pathx) * 180.0 / M_PI;

    fputs("<text x=\"", ctx->out);
    fprintf(ctx->out, "%.6g\" y=\"%.6g\" fill=\"", px, py + dy);
    svg_color(ctx->out, c);
    fputs("\" font-family=\"", ctx->out);
    svg_escape(ctx->out, family);
    fprintf(ctx->out, "\" font-size=\"%.6g\" text-anchor=\"%s\" font-weight=\"%d\"",
            font_px, text_anchor(st), weight);
    svg_opacity(ctx->out, c, "fill-opacity");
    emit_clip_attr(ctx, st);
    if (fabs(angle) > 0.0001) {
        fprintf(ctx->out, " transform=\"rotate(%.6g %.6g %.6g)\"", -angle, px, py);
    }
    fputs(">", ctx->out);
    svg_escape(ctx->out, plain);
    fputs("</text>\n", ctx->out);
    free(plain);
}

static bool is_symbol_text(VplState *st, const char *raw)
{
    return raw && raw[0] && raw[1] == '\0' &&
           (st->tx_halign == TH_SYMBOL || st->tx_valign == TV_SYMBOL);
}

static double symbol_size_units(double pathx, double pathy,
                                  double upx, double upy)
{
    double h = hypot(upx, upy);
    if (h <= 0.0) h = hypot(pathx, pathy);
    if (h <= 0.0) h = RPERIN / TXPERIN;
    h *= 0.42;
    if (h < px_to_u(1.0)) h = px_to_u(1.0);
    return h;
}

static void emit_symbol_marker(SvgCtx *ctx, VplState *st, const char *raw,
                               double x, double y,
                               double pathx, double pathy,
                               double upx, double upy)
{
    Color c = effective_stroke_color(ctx, st);
    double s = symbol_size_units(pathx, pathy, upx, upy);
    double r = s * 0.5;
    double px = sx(ctx, x);
    double py = sy(ctx, y);
    int ch = raw && raw[0] ? (unsigned char)raw[0] : '.';
    if (!point_in_clip(st, x, y)) return;

    switch (ch) {
        case '.':
        case ',':
            fprintf(ctx->out, "<circle cx=\"%.6g\" cy=\"%.6g\" r=\"%.6g\" fill=\"",
                    px, py, u_to_px(r * 0.65));
            svg_color(ctx->out, c);
            fputc('"', ctx->out);
            svg_opacity(ctx->out, c, "fill-opacity");
            emit_clip_attr(ctx, st);
            fputs("/>\n", ctx->out);
            break;
        case 'o':
        case 'O':
        case '0':
            fprintf(ctx->out, "<circle cx=\"%.6g\" cy=\"%.6g\" r=\"%.6g\" fill=\"none\" stroke=\"",
                    px, py, u_to_px(r));
            svg_color(ctx->out, c);
            fprintf(ctx->out, "\" stroke-width=\"%.6g\"", effective_stroke_width(ctx, st));
            svg_opacity(ctx->out, c, "stroke-opacity");
            emit_clip_attr(ctx, st);
            fputs("/>\n", ctx->out);
            break;
        case '+':
            fprintf(ctx->out, "<path d=\"M %.6g %.6g L %.6g %.6g M %.6g %.6g L %.6g %.6g\" fill=\"none\" stroke=\"",
                    sx(ctx, x - r), py, sx(ctx, x + r), py,
                    px, sy(ctx, y - r), px, sy(ctx, y + r));
            svg_color(ctx->out, c);
            fprintf(ctx->out, "\" stroke-width=\"%.6g\" stroke-linecap=\"round\"", effective_stroke_width(ctx, st));
            svg_opacity(ctx->out, c, "stroke-opacity");
            emit_clip_attr(ctx, st);
            fputs("/>\n", ctx->out);
            break;
        case 'x':
        case 'X':
            fprintf(ctx->out, "<path d=\"M %.6g %.6g L %.6g %.6g M %.6g %.6g L %.6g %.6g\" fill=\"none\" stroke=\"",
                    sx(ctx, x - r), sy(ctx, y - r), sx(ctx, x + r), sy(ctx, y + r),
                    sx(ctx, x - r), sy(ctx, y + r), sx(ctx, x + r), sy(ctx, y - r));
            svg_color(ctx->out, c);
            fprintf(ctx->out, "\" stroke-width=\"%.6g\" stroke-linecap=\"round\"", effective_stroke_width(ctx, st));
            svg_opacity(ctx->out, c, "stroke-opacity");
            emit_clip_attr(ctx, st);
            fputs("/>\n", ctx->out);
            break;
        case '*':
            fprintf(ctx->out, "<path d=\"M %.6g %.6g L %.6g %.6g M %.6g %.6g L %.6g %.6g M %.6g %.6g L %.6g %.6g M %.6g %.6g L %.6g %.6g\" fill=\"none\" stroke=\"",
                    sx(ctx, x - r), py, sx(ctx, x + r), py,
                    px, sy(ctx, y - r), px, sy(ctx, y + r),
                    sx(ctx, x - 0.707 * r), sy(ctx, y - 0.707 * r),
                    sx(ctx, x + 0.707 * r), sy(ctx, y + 0.707 * r),
                    sx(ctx, x - 0.707 * r), sy(ctx, y + 0.707 * r),
                    sx(ctx, x + 0.707 * r), sy(ctx, y - 0.707 * r));
            svg_color(ctx->out, c);
            fprintf(ctx->out, "\" stroke-width=\"%.6g\" stroke-linecap=\"round\"", effective_stroke_width(ctx, st));
            svg_opacity(ctx->out, c, "stroke-opacity");
            emit_clip_attr(ctx, st);
            fputs("/>\n", ctx->out);
            break;
        case 's':
        case 'S':
            fprintf(ctx->out, "<rect x=\"%.6g\" y=\"%.6g\" width=\"%.6g\" height=\"%.6g\" fill=\"none\" stroke=\"",
                    sx(ctx, x - r), sy(ctx, y + r), u_to_px(2.0 * r), u_to_px(2.0 * r));
            svg_color(ctx->out, c);
            fprintf(ctx->out, "\" stroke-width=\"%.6g\"", effective_stroke_width(ctx, st));
            svg_opacity(ctx->out, c, "stroke-opacity");
            emit_clip_attr(ctx, st);
            fputs("/>\n", ctx->out);
            break;
        case '^':
        case 'v':
        case 'V':
            if (ch == '^') {
                fprintf(ctx->out, "<path d=\"M %.6g %.6g L %.6g %.6g L %.6g %.6g Z\" fill=\"none\" stroke=\"",
                        px, sy(ctx, y + r), sx(ctx, x - r), sy(ctx, y - r), sx(ctx, x + r), sy(ctx, y - r));
            } else {
                fprintf(ctx->out, "<path d=\"M %.6g %.6g L %.6g %.6g L %.6g %.6g Z\" fill=\"none\" stroke=\"",
                        px, sy(ctx, y - r), sx(ctx, x - r), sy(ctx, y + r), sx(ctx, x + r), sy(ctx, y + r));
            }
            svg_color(ctx->out, c);
            fprintf(ctx->out, "\" stroke-width=\"%.6g\" stroke-linejoin=\"round\"", effective_stroke_width(ctx, st));
            svg_opacity(ctx->out, c, "stroke-opacity");
            emit_clip_attr(ctx, st);
            fputs("/>\n", ctx->out);
            break;
        case 'd':
        case 'D':
            fprintf(ctx->out, "<path d=\"M %.6g %.6g L %.6g %.6g L %.6g %.6g L %.6g %.6g Z\" fill=\"none\" stroke=\"",
                    px, sy(ctx, y + r), sx(ctx, x + r), py, px, sy(ctx, y - r), sx(ctx, x - r), py);
            svg_color(ctx->out, c);
            fprintf(ctx->out, "\" stroke-width=\"%.6g\" stroke-linejoin=\"round\"", effective_stroke_width(ctx, st));
            svg_opacity(ctx->out, c, "stroke-opacity");
            emit_clip_attr(ctx, st);
            fputs("/>\n", ctx->out);
            break;
        default:
            fprintf(ctx->out, "<circle cx=\"%.6g\" cy=\"%.6g\" r=\"%.6g\" fill=\"",
                    px, py, u_to_px(r * 0.65));
            svg_color(ctx->out, c);
            fputc('"', ctx->out);
            svg_opacity(ctx->out, c, "fill-opacity");
            emit_clip_attr(ctx, st);
            fputs("/>\n", ctx->out);
            break;
    }
}

static void bbox_symbol_marker(SvgCtx *ctx, VplState *st, double x, double y,
                               double pathx, double pathy,
                               double upx, double upy)
{
    double r = symbol_size_units(pathx, pathy, upx, upy) * 0.5;
    if (!point_in_clip(st, x, y)) return;
    bounds_add(&ctx->bounds, x - r, y - r);
    bounds_add(&ctx->bounds, x + r, y + r);
}

static void text_vectors_from_size(int size, int orient,
                                   double *pathx, double *pathy,
                                   double *upx, double *upy)
{
    double h = size * RPERIN / TXPERIN;
    double a = (double)orient * M_PI / 180.0;
    *pathx = h * cos(a);
    *pathy = h * sin(a);
    *upx = -h * sin(a);
    *upy = h * cos(a);
}

static Color color_for_index(VplState *st, int idx)
{
    Color c;
    if (idx >= 0 && idx < 32768 && st->colors[idx].set) return st->colors[idx];
    if (idx < 0) idx = 0;
    if (idx > 255) idx = 255;
    c.r = c.g = c.b = idx;
    c.a = 255;
    c.set = true;
    return c;
}

static Color default_palette_color(int idx)
{
    VplState st;
    state_init(&st);
    return color_for_index(&st, idx);
}

static void options_init(ConvertOptions *opt)
{
    memset(opt, 0, sizeof(*opt));
    opt->bgcolor = color_rgba(255, 255, 255, 255);
}

static bool decode_raster_rgba(Reader *r, VplState *st, bool bit, int offset,
                               int xpix, int ypix, unsigned char **rgba_out)
{
    int yrast;
    unsigned char *rgba;
    int *line;

    if (xpix <= 0 || ypix <= 0) return false;
    rgba = (unsigned char *)calloc((size_t)xpix * (size_t)ypix * 4, 1);
    line = (int *)calloc((size_t)xpix, sizeof(int));
    if (!rgba || !line) die("out of memory");

    for (yrast = 0; yrast < ypix; yrast++) {
        int rep, pos = 0, rr;
        if (!r_i16(r, &rep) || rep <= 0) {
            free(rgba);
            free(line);
            return false;
        }
        while (pos < xpix) {
            int num_pat, num_byte, bytes, j, p;
            unsigned char *pat;
            if (!r_i16(r, &num_pat) || !r_i16(r, &num_byte)) {
                free(rgba);
                free(line);
                return false;
            }
            if (num_pat <= 0 || num_byte <= 0) {
                free(rgba);
                free(line);
                return false;
            }
            bytes = bit ? ((num_byte + 7) / 8) : num_byte;
            if (r->pos + (size_t)bytes > r->len || pos + num_pat * num_byte > xpix) {
                free(rgba);
                free(line);
                return false;
            }
            pat = r->data + r->pos;
            r->pos += (size_t)bytes;
            for (p = 0; p < num_pat; p++) {
                if (bit) {
                    for (j = 0; j < num_byte; j++) {
                        int byte = pat[j / 8];
                        int on = (byte & (1 << (7 - (j % 8)))) != 0;
                        line[pos++] = offset * on;
                    }
                } else {
                    for (j = 0; j < num_byte; j++) {
                        line[pos++] = offset + pat[j];
                    }
                }
            }
        }

        for (rr = 0; rr < rep && yrast + rr < ypix; rr++) {
            int x;
            for (x = 0; x < xpix; x++) {
                Color c = color_for_index(st, line[x]);
                size_t off = ((size_t)(yrast + rr) * (size_t)xpix + (size_t)x) * 4;
                rgba[off] = (unsigned char)c.r;
                rgba[off + 1] = (unsigned char)c.g;
                rgba[off + 2] = (unsigned char)c.b;
                rgba[off + 3] = (unsigned char)c.a;
            }
        }
        yrast += rep - 1;
    }

    free(line);
    *rgba_out = rgba;
    return true;
}

static unsigned char *orient_raster_rgba(const unsigned char *src,
                                         int src_w, int src_h, int orient,
                                         int *dst_w, int *dst_h)
{
    unsigned char *dst;
    int x, y;

    orient %= 4;
    if (orient < 0) orient += 4;
    *dst_w = (orient == 1 || orient == 3) ? src_h : src_w;
    *dst_h = (orient == 1 || orient == 3) ? src_w : src_h;

    dst = (unsigned char *)calloc((size_t)(*dst_w) * (size_t)(*dst_h) * 4, 1);
    if (!dst) die("out of memory");

    for (y = 0; y < src_h; y++) {
        for (x = 0; x < src_w; x++) {
            int dx = x, dy = y;
            const unsigned char *sp = src + ((size_t)y * (size_t)src_w + (size_t)x) * 4;
            unsigned char *dp;

            switch (orient) {
            case 1:
                dx = src_h - 1 - y;
                dy = x;
                break;
            case 2:
                dx = src_w - 1 - x;
                dy = src_h - 1 - y;
                break;
            case 3:
                dx = y;
                dy = src_w - 1 - x;
                break;
            default:
                break;
            }

            dp = dst + ((size_t)dy * (size_t)(*dst_w) + (size_t)dx) * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }

    return dst;
}

static void emit_raster_image(SvgCtx *ctx, VplState *st, Reader *r, bool bit,
                              int offset, int x0, int y0, int x1, int y1,
                              int xpix, int ypix, int orient)
{
    unsigned char *rgba = NULL;
    unsigned char *display_rgba = NULL;
    ByteBuf png = {0};
    int display_w = xpix;
    int display_h = ypix;
    double x = sx(ctx, x0);
    double y = sy(ctx, y1);
    double w = fabs(u_to_px(x1 - x0));
    double h = fabs(u_to_px(y1 - y0));

    if (!decode_raster_rgba(r, st, bit, offset, xpix, ypix, &rgba)) {
        die("failed to decode raster payload");
    }
    display_rgba = orient_raster_rgba(rgba, xpix, ypix, orient, &display_w, &display_h);
    free(rgba);
    write_png_rgba(&png, display_rgba, display_w, display_h);
    free(display_rgba);

    /* VPL rasters are indexed data samples, not photographs.  Keep the
     * source cells discrete when SVG viewers scale the embedded PNG. */
    fprintf(ctx->out, "<image x=\"%.6g\" y=\"%.6g\" width=\"%.6g\" height=\"%.6g\" preserveAspectRatio=\"none\" image-rendering=\"pixelated\" href=\"data:image/png;base64,",
            x, y, w, h);
    write_base64(ctx->out, png.data, png.len);
    fputc('"', ctx->out);
    emit_clip_attr(ctx, st);
    fputs("/>\n", ctx->out);
    free(png.data);
}

static bool skip_raster_payload(Reader *r, bool bit, int xpix, int ypix)
{
    int yrast;
    for (yrast = 0; yrast < ypix; yrast++) {
        int rep, pos = 0;
        if (!r_i16(r, &rep) || rep <= 0) return false;
        while (pos < xpix) {
            int num_pat, num_byte, bytes;
            if (!r_i16(r, &num_pat) || !r_i16(r, &num_byte)) return false;
            if (num_pat <= 0 || num_byte <= 0) return false;
            bytes = bit ? ((num_byte + 7) / 8) : num_byte;
            if (r->pos + (size_t)bytes > r->len) return false;
            r->pos += (size_t)bytes;
            pos += num_pat * num_byte;
        }
        yrast += rep - 1;
    }
    return true;
}

static bool parse_vpl(Reader *r, SvgCtx *ctx, bool emit, int target_frame,
                      int *frame_count_out, bool *saw_content)
{
    VplState st;
    unsigned char cmd;
    int current_frame = 0;
    bool frame_has_content = false;
    bool any_content = false;
    state_init(&st);
    if (saw_content) *saw_content = false;

    while (r_u8(r, &cmd)) {
        int a, b, c, d, n, i;
        char *s = NULL;
        bool frame_active = target_frame < 0 || current_frame == target_frame;
        bool drawable = !in_frame_number_group(&st);
        bool active = frame_active && drawable;
        switch (cmd) {
            case VP_SETSTYLE:
                if (!r_u8(r, &cmd)) return false;
                break;
            case VP_MOVE:
                if (!r_i16(r, &a) || !r_i16(r, &b)) return false;
                st.x = a;
                st.y = b;
                break;
            case VP_DRAW:
                if (!r_i16(r, &a) || !r_i16(r, &b)) return false;
                if (drawable) {
                    frame_has_content = true;
                    any_content = true;
                }
                if (active) {
                    if (saw_content) *saw_content = true;
                    if (emit) emit_line(ctx, &st, st.x, st.y, a, b);
                    else bbox_line(ctx, &st, st.x, st.y, a, b);
                }
                st.x = a;
                st.y = b;
                break;
            case VP_PLINE: {
                double *pts;
                if (!r_i16(r, &n)) return false;
                if (n <= 0) break;
                pts = (double *)calloc((size_t)n * 2, sizeof(double));
                if (!pts) die("out of memory");
                for (i = 0; i < n; i++) {
                    if (!r_i16(r, &a) || !r_i16(r, &b)) {
                        free(pts);
                        return false;
                    }
                    pts[2 * i] = a;
                    pts[2 * i + 1] = b;
                }
                for (i = 1; i < n; i++) {
                    if (drawable) {
                        frame_has_content = true;
                        any_content = true;
                    }
                    if (active) {
                        if (saw_content) *saw_content = true;
                        if (emit) emit_line(ctx, &st, pts[2 * (i - 1)], pts[2 * (i - 1) + 1], pts[2 * i], pts[2 * i + 1]);
                        else bbox_line(ctx, &st, pts[2 * (i - 1)], pts[2 * (i - 1) + 1], pts[2 * i], pts[2 * i + 1]);
                    }
                }
                st.x = pts[2 * (n - 1)];
                st.y = pts[2 * (n - 1) + 1];
                free(pts);
                break;
            }
            case VP_PMARK:
                if (!r_i16(r, &n) || !r_i16(r, &a) || !r_i16(r, &b)) return false;
                for (i = 0; i < n; i++) {
                    int x, y;
                    if (!r_i16(r, &x) || !r_i16(r, &y)) return false;
                    if (drawable) {
                        frame_has_content = true;
                        any_content = true;
                    }
                    if (active) {
                        if (saw_content) *saw_content = true;
                        if (emit) {
                        if (!point_in_clip(&st, x, y)) continue;
                        Color col = effective_stroke_color(ctx, &st);
                        fprintf(ctx->out, "<circle cx=\"%.6g\" cy=\"%.6g\" r=\"%.6g\" fill=\"",
                                sx(ctx, x), sy(ctx, y), u_to_px(b > 0 ? b : 4));
                        svg_color(ctx->out, col);
                        fputc('"', ctx->out);
                        svg_opacity(ctx->out, col, "fill-opacity");
                        emit_clip_attr(ctx, &st);
                        fputs("/>\n", ctx->out);
                        } else {
                        if (!point_in_clip(&st, x, y)) continue;
                        bounds_add_fat(&ctx->bounds, x, y, b > 0 ? b : 4);
                        }
                    }
                }
                break;
            case VP_TEXT:
                if (!r_i16(r, &a) || !r_i16(r, &b) || !r_cstring(r, &s)) return false;
                {
                    double pathx, pathy, upx, upy;
                    text_vectors_from_size(a, b, &pathx, &pathy, &upx, &upy);
                    if (drawable) {
                        frame_has_content = true;
                        any_content = true;
                    }
                    if (active) {
                        if (saw_content) *saw_content = true;
                        if (is_symbol_text(&st, s)) {
                            if (emit) emit_symbol_marker(ctx, &st, s, st.x, st.y, pathx, pathy, upx, upy);
                            else bbox_symbol_marker(ctx, &st, st.x, st.y, pathx, pathy, upx, upy);
                        } else {
                            if (emit) emit_text_generic(ctx, &st, s, st.x, st.y, pathx, pathy, upx, upy);
                            else text_bbox_generic(ctx, &st, s, st.x, st.y, pathx, pathy, upx, upy);
                        }
                    }
                }
                free(s);
                break;
            case VP_GTEXT: {
                int pathx, pathy, upx, upy;
                if (!r_i16(r, &pathx) || !r_i16(r, &pathy) ||
                    !r_i16(r, &upx) || !r_i16(r, &upy) || !r_cstring(r, &s)) return false;
                if (drawable) {
                    frame_has_content = true;
                    any_content = true;
                }
                if (active) {
                    if (saw_content) *saw_content = true;
                    if (is_symbol_text(&st, s)) {
                    if (emit) emit_symbol_marker(ctx, &st, s, st.x, st.y,
                                                 (double)pathx / TEXTVECSCALE,
                                                 (double)pathy / TEXTVECSCALE,
                                                 (double)upx / TEXTVECSCALE,
                                                 (double)upy / TEXTVECSCALE);
                    else bbox_symbol_marker(ctx, &st, st.x, st.y,
                                            (double)pathx / TEXTVECSCALE,
                                            (double)pathy / TEXTVECSCALE,
                                            (double)upx / TEXTVECSCALE,
                                            (double)upy / TEXTVECSCALE);
                    } else {
                    if (emit) {
                    emit_text_generic(ctx, &st, s, st.x, st.y,
                                      (double)pathx / TEXTVECSCALE,
                                      (double)pathy / TEXTVECSCALE,
                                      (double)upx / TEXTVECSCALE,
                                      (double)upy / TEXTVECSCALE);
                    } else {
                    text_bbox_generic(ctx, &st, s, st.x, st.y,
                                      (double)pathx / TEXTVECSCALE,
                                      (double)pathy / TEXTVECSCALE,
                                      (double)upx / TEXTVECSCALE,
                                      (double)upy / TEXTVECSCALE);
                    }
                    }
                }
                free(s);
                break;
            }
            case VP_AREA: {
                double *pts;
                if (!r_i16(r, &n)) return false;
                pts = (double *)calloc((size_t)n * 2, sizeof(double));
                if (!pts) die("out of memory");
                for (i = 0; i < n; i++) {
                    if (!r_i16(r, &a) || !r_i16(r, &b)) {
                        free(pts);
                        return false;
                    }
                    pts[2 * i] = a;
                    pts[2 * i + 1] = b;
                }
                if (drawable) {
                    frame_has_content = true;
                    any_content = true;
                }
                if (active) {
                    if (saw_content) *saw_content = true;
                    if (emit) emit_polygon(ctx, &st, pts, n, true, false);
                    else bbox_polygon(ctx, &st, pts, n);
                }
                free(pts);
                break;
            }
            case VP_OLDAREA: {
                double *pts;
                int fat, xmask, ymask;
                if (!r_i16(r, &n) || !r_i16(r, &fat) || !r_i16(r, &xmask) || !r_i16(r, &ymask)) return false;
                pts = (double *)calloc((size_t)n * 2, sizeof(double));
                if (!pts) die("out of memory");
                for (i = 0; i < n; i++) {
                    if (!r_i16(r, &a) || !r_i16(r, &b)) {
                        free(pts);
                        return false;
                    }
                    pts[2 * i] = a;
                    pts[2 * i + 1] = b;
                }
                if (drawable) {
                    frame_has_content = true;
                    any_content = true;
                }
                if (active) {
                    if (saw_content) *saw_content = true;
                    if (emit) {
                    int save_fat = st.fat;
                    st.fat = fat;
                    emit_polygon(ctx, &st, pts, n, xmask != 0 && ymask != 0, fat >= 0);
                    st.fat = save_fat;
                    } else {
                    bbox_polygon(ctx, &st, pts, n);
                    }
                }
                free(pts);
                break;
            }
            case VP_BYTE_RASTER:
            case VP_BIT_RASTER: {
                int orient, offset, x0, y0, x1, y1, xpix, ypix;
                bool bit = cmd == VP_BIT_RASTER;
                if (!r_i16(r, &orient) || !r_i16(r, &offset) ||
                    !r_i16(r, &x0) || !r_i16(r, &y0) ||
                    !r_i16(r, &x1) || !r_i16(r, &y1) ||
                    !r_i16(r, &xpix) || !r_i16(r, &ypix)) return false;
                if (drawable) {
                    frame_has_content = true;
                    any_content = true;
                }
                if (active) {
                    if (saw_content) *saw_content = true;
                    if (emit) {
                    emit_raster_image(ctx, &st, r, bit, offset, x0, y0, x1, y1, xpix, ypix, orient);
                    } else {
                    if (!skip_raster_payload(r, bit, xpix, ypix)) return false;
                    if (st.clip_active) {
                        double xmin = x0 < x1 ? x0 : x1;
                        double xmax = x0 < x1 ? x1 : x0;
                        double ymin = y0 < y1 ? y0 : y1;
                        double ymax = y0 < y1 ? y1 : y0;
                        if (xmin < st.clip_xmin) xmin = st.clip_xmin;
                        if (xmax > st.clip_xmax) xmax = st.clip_xmax;
                        if (ymin < st.clip_ymin) ymin = st.clip_ymin;
                        if (ymax > st.clip_ymax) ymax = st.clip_ymax;
                        if (xmax >= xmin && ymax >= ymin) {
                            bounds_add(&ctx->bounds, xmin, ymin);
                            bounds_add(&ctx->bounds, xmax, ymax);
                        }
                    } else {
                        bounds_add(&ctx->bounds, x0, y0);
                        bounds_add(&ctx->bounds, x1, y1);
                    }
                    }
                } else {
                    if (!skip_raster_payload(r, bit, xpix, ypix)) return false;
                }
                (void)offset;
                break;
            }
            case VP_BREAK:
                st.group_depth = 0;
                break;
            case VP_ERASE:
                if (frame_has_content) {
                    current_frame++;
                    frame_has_content = false;
                }
                st.group_depth = 0;
                st.clip_active = false;
                st.clip_id = 0;
                break;
            case VP_PURGE:
            case VP_NOOP:
            case VP_BACKGROUND:
                break;
            case VP_ORIGIN:
                if (!r_i16(r, &a) || !r_i16(r, &b)) return false;
                break;
            case VP_WINDOW:
                if (!r_i16(r, &a) || !r_i16(r, &b) || !r_i16(r, &c) || !r_i16(r, &d)) return false;
                st.clip_xmin = a < c ? a : c;
                st.clip_xmax = a < c ? c : a;
                st.clip_ymin = b < d ? b : d;
                st.clip_ymax = b < d ? d : b;
                st.clip_active = st.clip_xmax > st.clip_xmin && st.clip_ymax > st.clip_ymin;
                if (st.clip_active && emit && frame_active) {
                    st.clip_id = ++ctx->clip_id;
                    emit_clip_def(ctx, &st);
                } else {
                    st.clip_id = 0;
                }
                break;
            case VP_FAT:
                if (!r_i16(r, &a)) return false;
                st.fat = a >= 0 ? a + 1 : -1;
                break;
            case VP_SETDASH:
                if (!r_i16(r, &n)) return false;
                if (n < 0 || n > 10) return false;
                st.dash_count = n;
                st.dash_on = false;
                for (i = 0; i < n * 2; i++) {
                    if (!r_i16(r, &a)) return false;
                    st.dashes[i] = a;
                    if (a != 0) st.dash_on = true;
                }
                break;
            case VP_COLOR:
                if (!r_i16(r, &a)) return false;
                if (a >= 0 && a < 32768) st.current_color = a;
                break;
            case VP_SET_COLOR_TABLE:
                if (!r_i16(r, &a) || !r_i16(r, &b) || !r_i16(r, &c) || !r_i16(r, &d)) return false;
                if (a >= 0 && a < 32768) {
                    st.colors[a].r = b < 0 ? 0 : (b > 255 ? 255 : b);
                    st.colors[a].g = c < 0 ? 0 : (c > 255 ? 255 : c);
                    st.colors[a].b = d < 0 ? 0 : (d > 255 ? 255 : d);
                    st.colors[a].a = 255;
                    st.colors[a].set = true;
                }
                break;
            case VP_TXALIGN:
                if (!r_i16(r, &st.tx_halign) || !r_i16(r, &st.tx_valign)) return false;
                break;
            case VP_TXFONTPREC:
                if (!r_i16(r, &st.tx_font) || !r_i16(r, &st.tx_prec) || !r_i16(r, &st.tx_overlay)) return false;
                break;
            case VP_PATLOAD:
                if (!r_i16(r, &a) || !r_i16(r, &b) || !r_i16(r, &c) || !r_i16(r, &d)) return false;
                if (b >= 0) {
                    int count = b * c;
                    for (i = 0; i < count; i++) {
                        if (!r_i16(r, &a)) return false;
                    }
                } else {
                    int count = c * 2 * 4;
                    for (i = 0; i < count; i++) {
                        if (!r_i16(r, &a)) return false;
                    }
                }
                if (emit) warn_once(&ctx->warned_pat, "pattern/hatch fills simplified");
                break;
            case VP_OVERLAY:
                if (!r_i16(r, &a)) return false;
                break;
            case VP_MESSAGE:
            case VP_BEGIN_GROUP:
                if (!r_cstring(r, &s)) return false;
                if (cmd == VP_BEGIN_GROUP) {
                    group_push(&st, s);
                } else if (emit && s && *s) {
                    fprintf(stderr, "%s\n", s);
                }
                free(s);
                break;
            case VP_END_GROUP:
                group_pop(&st);
                break;
            case VP_OLDTEXT:
                if (!r_i16(r, &a) || !r_cstring(r, &s)) return false;
                b = (int)(((a & 0140) >> 5) * 90);
                a = a & 037;
                {
                    double pathx, pathy, upx, upy;
                    text_vectors_from_size(a, b, &pathx, &pathy, &upx, &upy);
                    if (drawable) {
                        frame_has_content = true;
                        any_content = true;
                    }
                    if (active) {
                        if (saw_content) *saw_content = true;
                        if (is_symbol_text(&st, s)) {
                            if (emit) emit_symbol_marker(ctx, &st, s, st.x, st.y, pathx, pathy, upx, upy);
                            else bbox_symbol_marker(ctx, &st, st.x, st.y, pathx, pathy, upx, upy);
                        } else {
                            if (emit) emit_text_generic(ctx, &st, s, st.x, st.y, pathx, pathy, upx, upy);
                            else text_bbox_generic(ctx, &st, s, st.x, st.y, pathx, pathy, upx, upy);
                        }
                    }
                }
                free(s);
                break;
            default:
                die("invalid VPL command 0x%02x at byte %zu", cmd, r->pos - 1);
        }
    }
    if (frame_count_out) {
        *frame_count_out = any_content ? current_frame + (frame_has_content ? 1 : 0) : 1;
    }
    return true;
}

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: vpl2svg [options] [input.vpl] > output.svg\n"
            "\n"
            "Converts a VPL command stream to SVG without Madagascar libraries.\n"
            "Default output is cropped to the drawn bounding box.\n"
            "\n"
            "Options:\n"
            "  --stream              Emit each completed frame as soon as VP_ERASE is read\n"
            "  --border INCHES\n"
            "  --bgcolor COLOR\n"
            "  --fontcolor COLOR | --font FAMILY | --fontfamily FAMILY | --fontsz SIZE\n"
            "  --framecolor COLOR | --framefat N | --framewidth PX\n"
            "  --axiscolor COLOR | --axisfat N | --axiswidth PX\n"
            "  --gridcolor COLOR | --gridfat N | --gridwidth PX\n"
            "  --labelcolor COLOR | --labelsz SIZE | --labelfat N\n"
            "  --titlecolor COLOR | --titlesz SIZE | --titlefat N\n"
            "  --barlabelcolor COLOR | --scalebarcolor COLOR\n");
}

static int parse_int_arg(const char *name, const char *value)
{
    char *end = NULL;
    long v = strtol(value, &end, 10);
    if (!end || *end != '\0') die("%s needs an integer", name);
    if (v < -32768 || v > 32767) die("%s is out of range", name);
    return (int)v;
}

static double parse_double_arg(const char *name, const char *value)
{
    char *end = NULL;
    double v = strtod(value, &end);
    if (!end || *end != '\0' || !isfinite(v)) die("%s needs a number", name);
    return v;
}

static void style_set_family(Style *style, const char *value)
{
    snprintf(style->family, sizeof(style->family), "%s", value);
    style->family_set = true;
}

static void style_set_color_arg(Style *style, const char *name, const char *value)
{
    Color c;
    if (parse_color_spec(value, &c)) {
        style->color = c;
        style->color_set = true;
        style->color_default = false;
        return;
    }
    style->color = default_palette_color(parse_int_arg(name, value));
    style->color_set = true;
    style->color_default = false;
}

static Style *style_for_option(ConvertOptions *opt, const char *name, const char **prefix_out)
{
    struct StyleName {
        const char *prefix;
        Style *style;
    } styles[] = {
        {"barlabel", &opt->barlabel_style},
        {"scalebar", &opt->scalebar_style},
        {"label", &opt->label_style},
        {"title", &opt->title_style},
        {"frame", &opt->frame_style},
        {"axis", &opt->axis_style},
        {"grid", &opt->grid_style},
        {"font", &opt->font_style},
        {NULL, NULL}
    };
    int i;
    for (i = 0; styles[i].prefix; i++) {
        if (starts_ci(name, styles[i].prefix)) {
            if (prefix_out) *prefix_out = styles[i].prefix;
            return styles[i].style;
        }
    }
    return NULL;
}

static bool is_text_style(ConvertOptions *opt, Style *style)
{
    return style == &opt->font_style ||
           style == &opt->label_style ||
           style == &opt->title_style ||
           style == &opt->barlabel_style ||
           style == &opt->scalebar_style;
}

static bool parse_style_option(ConvertOptions *opt, const char *name,
                               const char *value)
{
    const char *prefix = NULL;
    Style *style = style_for_option(opt, name, &prefix);
    const char *suffix;
    if (streq_ci(name, "font") || streq_ci(name, "fontfamily")) {
        style_set_family(&opt->font_style, value);
        return true;
    }
    if (!style || !prefix) return false;
    suffix = name + strlen(prefix);

    if (streq_ci(suffix, "sz") || streq_ci(suffix, "size")) {
        style->size = parse_double_arg(name, value);
        style->size_set = style->size > 0.0;
    } else if (streq_ci(suffix, "fat")) {
        if (is_text_style(opt, style)) {
            style->fat = parse_int_arg(name, value);
            style->fat_set = true;
        } else {
            style->width = parse_double_arg(name, value);
            style->width_set = style->width > 0.0;
        }
    } else if (streq_ci(suffix, "width")) {
        if (is_text_style(opt, style)) return false;
        style->width = parse_double_arg(name, value);
        style->width_set = style->width > 0.0;
    } else if (streq_ci(suffix, "weight")) {
        style->weight = parse_int_arg(name, value);
        style->weight_set = true;
    } else if (streq_ci(suffix, "color") || streq_ci(suffix, "col")) {
        style_set_color_arg(style, name, value);
    } else if (streq_ci(suffix, "family")) {
        style_set_family(style, value);
    } else {
        return false;
    }
    return true;
}

static void apply_default_styles(ConvertOptions *opt)
{
    Color inv = invert_color(opt->bgcolor);
    if (!opt->frame_style.color_set) {
        opt->frame_style.color = inv;
        opt->frame_style.color_set = true;
        opt->frame_style.color_default = true;
    }
    if (!opt->font_style.color_set) {
        opt->font_style.color = inv;
        opt->font_style.color_set = true;
        opt->font_style.color_default = true;
    }
}

static const char *basename_c(const char *path)
{
    const char *p;
    if (!path || !*path) return "stdin";
    p = strrchr(path, '/');
    if (p && p[1]) return p + 1;
    return path;
}

static void write_split_marker(FILE *out, const char *input, int frame_index)
{
    fputs("<!-- RSFPY_SPLIT filename=\"", out);
    svg_escape(out, input ? input : "stdin");
    fputs("\" framelabel=\"", out);
    svg_escape(out, basename_c(input));
    fprintf(out, " #%d\" -->\n", frame_index + 1);
}

static bool emit_svg_frame(const unsigned char *data, size_t len,
                           const char *input, ConvertOptions opt,
                           int frame_index, int total_frames,
                           double border_in)
{
    Reader r;
    SvgCtx ctx;
    double width, height;
    bool saw_content = false;

    memset(&ctx, 0, sizeof(ctx));
    ctx.opt = opt;
    ctx.bounds.empty = true;
    ctx.out = stdout;
    ctx.scale = PPI / RPERIN;
    ctx.pad = border_in * RPERIN;

    r.data = (unsigned char *)data;
    r.len = len;
    r.pos = 0;
    if (!parse_vpl(&r, &ctx, false, frame_index, NULL, &saw_content)) {
        die("failed to parse VPL while scanning bounds");
    }
    if (!saw_content) return false;

    if (ctx.bounds.empty) {
        ctx.bounds.xmin = ctx.bounds.ymin = 0;
        ctx.bounds.xmax = 100 * RPERIN / PPI;
        ctx.bounds.ymax = 100 * RPERIN / PPI;
    }

    width = u_to_px(ctx.bounds.xmax - ctx.bounds.xmin + 2 * ctx.pad);
    height = u_to_px(ctx.bounds.ymax - ctx.bounds.ymin + 2 * ctx.pad);
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    if (total_frames != 1) write_split_marker(stdout, input, frame_index);
    fprintf(stdout, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(stdout, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.6g\" height=\"%.6g\" viewBox=\"0 0 %.6g %.6g\">\n",
            width, height, width, height);
    fputs("<metadata source=\"vpl2svg\" input=\"", stdout);
    svg_escape(stdout, input ? input : "stdin");
    fprintf(stdout, "\" frame=\"%d\" frames=\"%d\" bbox_vplot=\"%.6g %.6g %.6g %.6g\"></metadata>\n",
            frame_index + 1, total_frames > 0 ? total_frames : 0,
            ctx.bounds.xmin, ctx.bounds.ymin, ctx.bounds.xmax, ctx.bounds.ymax);
    if (ctx.opt.bgcolor.a > 0) {
        fputs("<rect x=\"0\" y=\"0\" width=\"100%\" height=\"100%\" fill=\"", stdout);
        svg_color(stdout, ctx.opt.bgcolor);
        fputc('"', stdout);
        svg_opacity(stdout, ctx.opt.bgcolor, "fill-opacity");
        fputs("/>\n", stdout);
    }

    r.pos = 0;
    if (!parse_vpl(&r, &ctx, true, frame_index, NULL, NULL)) {
        die("failed to parse VPL while emitting SVG");
    }
    fputs("</svg>\n", stdout);
    fflush(stdout);
    return true;
}

static int stream_emit_available(ByteBuf *prefix, const char *input,
                                 ConvertOptions opt, double border_in,
                                 int *emitted)
{
    Reader r;
    SvgCtx ctx;
    int frame_count = 0;
    int made = 0;

    if (!prefix || prefix->len == 0 || !emitted) return 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.opt = opt;
    r.data = prefix->data;
    r.len = prefix->len;
    r.pos = 0;

    if (!parse_vpl(&r, &ctx, false, 0, &frame_count, NULL)) {
        die("failed to parse streamed VPL prefix");
    }

    while (*emitted < frame_count) {
        if (emit_svg_frame(prefix->data, prefix->len, input, opt,
                           *emitted, 0, border_in)) {
            made++;
        }
        (*emitted)++;
    }

    return made;
}

static int run_streaming(const char *input, ConvertOptions opt, double border_in)
{
    FILE *fp = input ? fopen(input, "rb") : stdin;
    ByteBuf prefix = {0};
    unsigned char cmd;
    int emitted = 0;
    int made = 0;

    if (!fp) die("cannot open %s: %s", input, strerror(errno));

    while (stream_next_command(fp, &prefix, &cmd)) {
        if (cmd == VP_ERASE) {
            made += stream_emit_available(&prefix, input, opt, border_in, &emitted);
        }
    }

    if (ferror(fp)) die("read failed");
    made += stream_emit_available(&prefix, input, opt, border_in, &emitted);

    if (input) fclose(fp);
    bb_free(&prefix);
    return made;
}

int main(int argc, char **argv)
{
    const char *input = NULL;
    double border_in = 0.1;
    bool stream = false;
    unsigned char *data;
    size_t len;
    Reader r;
    SvgCtx ctx;
    ConvertOptions opt;
    int i, frame_count = 1, emitted = 0;

    memset(&ctx, 0, sizeof(ctx));
    options_init(&ctx.opt);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--stream") == 0) {
            stream = true;
        } else if (strcmp(argv[i], "--border") == 0) {
            if (++i >= argc) die("--border needs a value");
            border_in = atof(argv[i]);
            if (border_in < 0) border_in = 0;
        } else if (strcmp(argv[i], "--bgcolor") == 0) {
            if (++i >= argc) die("--bgcolor needs a value");
            if (!parse_color_spec(argv[i], &ctx.opt.bgcolor)) die("invalid color: %s", argv[i]);
        } else if (argv[i][0] == '-') {
            const char *name = argv[i] + 2;
            if (argv[i][1] != '-') die("unknown option: %s", argv[i]);
            if (++i >= argc) die("%s needs a value", argv[i - 1]);
            if (!parse_style_option(&ctx.opt, name, argv[i])) die("unknown option: %s", argv[i - 1]);
        } else {
            input = argv[i];
        }
    }

    apply_default_styles(&ctx.opt);
    opt = ctx.opt;

    if (stream) {
        run_streaming(input, opt, border_in);
        return 0;
    }

    data = read_all_file(input, &len);
    memset(&ctx, 0, sizeof(ctx));
    ctx.opt = opt;
    r.data = data;
    r.len = len;
    r.pos = 0;
    if (!parse_vpl(&r, &ctx, false, 0, &frame_count, NULL)) {
        die("failed to parse VPL while counting frames");
    }
    if (frame_count < 1) frame_count = 1;

    for (i = 0; i < frame_count; i++) {
        if (emit_svg_frame(data, len, input, opt, i, frame_count, border_in)) emitted++;
    }
    if (emitted == 0) emit_svg_frame(data, len, input, opt, 0, 1, border_in);
    free(data);
    return 0;
}
