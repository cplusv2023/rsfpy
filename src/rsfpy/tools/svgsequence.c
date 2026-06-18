#include "svgsequence.h"

#include <errno.h>
#include <gio/gio.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static GPtrArray *png_paths = NULL;
static GPtrArray *tmp_svg_paths = NULL;
static int argc_global = 0;
static char **argv_global = NULL;

void sf_init(int argc, char **argv)
{
    argc_global = argc;
    argv_global = argv;
}

static void ensure_temp_arrays(void)
{
    if (!png_paths) png_paths = g_ptr_array_new_with_free_func(g_free);
    if (!tmp_svg_paths) tmp_svg_paths = g_ptr_array_new_with_free_func(g_free);
}

static void remember_temp_path(GPtrArray **arr, const char *path)
{
    ensure_temp_arrays();
    if (*arr) g_ptr_array_add(*arr, g_strdup(path));
}

static char *sf_getstring(const char *key)
/* key=value */
{
    if (!key) return NULL;

    size_t keylen = strlen(key);

    for (int i = 1; i < argc_global; i++) {
        const char *arg = argv_global[i];
        if (strncmp(arg, key, keylen) == 0 && arg[keylen] == '=') {
            const char *val = arg + keylen + 1;
            return g_strdup(val);
        }
    }
    return NULL;
}

static gboolean readpathfile(const char *filename, char *datapath, size_t size)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) return FALSE;

    if (fscanf(fp, "datapath=%4095s", datapath) <= 0) {
        fclose(fp);
        return FALSE;
    }
    fclose(fp);
    datapath[size - 1] = '\0';
    return TRUE;
}

static char *getdatapath(void)
{
    char *path = sf_getstring("datapath");
    if (path) return path;

    path = g_malloc0(PATH_MAX + 1);

    if (readpathfile(".datapath", path, PATH_MAX + 1)) return path;

    const char *penv = getenv("DATAPATH");
    if (penv) {
        g_strlcpy(path, penv, PATH_MAX + 1);
        return path;
    }

    const char *home = getenv("HOME");
    if (home) {
        char file[PATH_MAX + 1];
        g_snprintf(file, sizeof(file), "%s/.datapath", home);
        if (readpathfile(file, path, PATH_MAX + 1)) return path;
    }

    g_strlcpy(path, "./", PATH_MAX + 1);
    return path;
}

static char *make_temp_path(const char *prefix, const char *suffix)
{
    char *datapath = getdatapath();
    char *name = g_strdup_printf(".%s_%d_%08x%s", prefix, (int)getpid(), g_random_int(), suffix);
    char *path = g_build_filename(datapath, name, NULL);
    g_free(name);
    g_free(datapath);
    return path;
}

static const char *find_mem(const char *haystack, gsize hay_len,
                            const char *needle, gsize needle_len)
{
    if (!haystack || !needle || needle_len == 0 || hay_len < needle_len) return NULL;

    const char first = needle[0];
    const char *p = haystack;
    const char *end = haystack + hay_len - needle_len + 1;

    while (p < end) {
        p = memchr(p, first, (size_t)(end - p));
        if (!p) return NULL;
        if (memcmp(p, needle, needle_len) == 0) return p;
        p++;
    }
    return NULL;
}

static void trim_range(const char *base, const char **start, const char **end)
{
    while (*start < *end && g_ascii_isspace((guchar)**start)) (*start)++;
    while (*end > *start && g_ascii_isspace((guchar)*((*end) - 1))) (*end)--;
    (void)base;
}

static char *extract_frame_label(const char *comment_start, const char *comment_end)
{
    static const char key[] = "framelabel=\"";
    const char *p = find_mem(comment_start,
                             (gsize)(comment_end - comment_start),
                             key, strlen(key));
    if (!p) return NULL;

    p += strlen(key);
    const char *q = memchr(p, '"', (size_t)(comment_end - p));
    if (!q || q <= p) return NULL;

    return g_strndup(p, (gsize)(q - p));
}

static void svg_sequence_init_if_needed(SvgSequence *seq)
{
    if (!seq) return;

    if (!seq->frames && seq->capacity <= 0) {
        seq->capacity = 0;
        seq->count = 0;
        seq->current_index = 0;
        seq->fps = 12;
        seq->playing = FALSE;
        seq->handle_cache_radius = SVG_SEQUENCE_DEFAULT_HANDLE_CACHE_RADIUS;
    }
}

static gboolean svg_sequence_reserve(SvgSequence *seq, int need)
{
    svg_sequence_init_if_needed(seq);
    if (need <= seq->capacity) return TRUE;

    int newcap = seq->capacity > 0 ? seq->capacity : 16;
    while (newcap < need) newcap *= 2;

    SvgFrame *newframes = g_realloc_n(seq->frames, (gsize)newcap, sizeof(SvgFrame));
    if (!newframes) return FALSE;

    memset(newframes + seq->capacity, 0, (gsize)(newcap - seq->capacity) * sizeof(SvgFrame));
    seq->frames = newframes;
    seq->capacity = newcap;
    return TRUE;
}

static SvgFrame *svg_sequence_append_frame(SvgSequence *seq)
{
    if (!svg_sequence_reserve(seq, seq->count + 1)) return NULL;
    SvgFrame *f = &seq->frames[seq->count++];
    memset(f, 0, sizeof(*f));
    return f;
}

static void svg_frame_clear_loaded(SvgFrame *f)
{
    if (!f) return;
    if (f->handle) {
        g_object_unref(f->handle);
        f->handle = NULL;
    }
    f->dimensions_valid = FALSE;
    f->width = 0.0;
    f->height = 0.0;
}

static void svg_frame_free(SvgFrame *f)
{
    if (!f) return;
    svg_frame_clear_loaded(f);

    if (f->remove_source_on_free && f->source_path) {
        remove(f->source_path);
        fprintf(stderr, "Removed temporary SVG file %s\n", f->source_path);
    }

    g_free(f->path);
    g_free(f->framelabel);
    g_free(f->source_path);
    memset(f, 0, sizeof(*f));
}

static void add_file_frame(SvgSequence *seq, const char *path, const char *label)
{
    SvgFrame *f = svg_sequence_append_frame(seq);
    if (!f) return;

    f->path = g_strdup(path);
    f->framelabel = g_strdup(label ? label : "Single file");
    f->source_path = g_strdup(path);
    f->use_range = FALSE;
    f->data_offset = 0;
    f->data_len = 0;
}

static void add_range_frame(SvgSequence *seq,
                            const char *source_path,
                            const char *display_path,
                            const char *label,
                            goffset offset,
                            gsize len,
                            gboolean remove_source_on_free)
{
    SvgFrame *f = svg_sequence_append_frame(seq);
    if (!f) return;

    f->path = g_strdup(display_path ? display_path : source_path);
    f->framelabel = label ? g_strdup(label) : g_strdup_printf("Frame %d", seq->count - 1);
    f->source_path = g_strdup(source_path);
    f->data_offset = offset;
    f->data_len = len;
    f->use_range = TRUE;
    f->remove_source_on_free = remove_source_on_free;
}

static gboolean read_frame_range(const SvgFrame *f, char **out, gsize *out_len)
{
    *out = NULL;
    *out_len = 0;

    FILE *fp = fopen(f->source_path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s: %s\n", f->source_path, g_strerror(errno));
        return FALSE;
    }

#if defined(_WIN32)
    if (fseek(fp, (long)f->data_offset, SEEK_SET) != 0) {
#else
    if (fseeko(fp, (off_t)f->data_offset, SEEK_SET) != 0) {
#endif
        fprintf(stderr, "Failed to seek %s: %s\n", f->source_path, g_strerror(errno));
        fclose(fp);
        return FALSE;
    }

    char *buf = g_malloc(f->data_len + 1);
    size_t nr = fread(buf, 1, f->data_len, fp);
    fclose(fp);

    if (nr != f->data_len) {
        fprintf(stderr, "Short read from %s: expected %zu bytes, got %zu bytes\n",
                f->source_path, (size_t)f->data_len, nr);
        g_free(buf);
        return FALSE;
    }

    buf[f->data_len] = '\0';
    *out = buf;
    *out_len = f->data_len;
    return TRUE;
}

static RsvgHandle *extract_base64(const guint8 *svg_content,
                                  RsvgHandleFlags flags,
                                  GCancellable *cancellable,
                                  GError **err)
{
    const char *p = (const char *)svg_content;
    GString *out = g_string_new(NULL);
    gboolean found = FALSE;

    while (1) {
        const char *tag = strstr(p, "data:image/png;base64,");
        if (!tag) {
            g_string_append(out, p);
            break;
        }

        g_string_append_len(out, p, tag - p);

        tag += strlen("data:image/png;base64,");
        const char *end = strpbrk(tag, "\"'>");
        if (!end) {
            g_set_error(err, g_quark_from_static_string("extract-base64"),
                        1, "Malformed SVG: unterminated base64 image");
            g_string_free(out, TRUE);
            return NULL;
        }

        size_t b64len = (size_t)(end - tag);
        char *b64data = g_strndup(tag, b64len);

        gsize out_len = 0;
        guchar *decoded = g_base64_decode(b64data, &out_len);
        g_free(b64data);

        if (!decoded) {
            g_set_error(err, g_quark_from_static_string("extract-base64"),
                        2, "Base64 decode failed");
            g_string_free(out, TRUE);
            return NULL;
        }

        char *filename = make_temp_path("pngtmp", ".png");
        FILE *fp = fopen(filename, "wb");
        if (!fp) {
            g_free(decoded);
            g_set_error(err, g_quark_from_static_string("extract-base64"),
                        3, "Failed to open temp file %s", filename);
            g_free(filename);
            g_string_free(out, TRUE);
            return NULL;
        }
        fwrite(decoded, 1, out_len, fp);
        fclose(fp);
        g_free(decoded);

        remember_temp_path(&png_paths, filename);
        fprintf(stderr, "Extracted embedded PNG to %s\n", filename);

        g_string_append(out, filename);
        g_free(filename);

        found = TRUE;
        p = end;
    }

    if (!found) {
        g_set_error(err, g_quark_from_static_string("extract-base64"),
                    4, "No embedded PNG images found");
        g_string_free(out, TRUE);
        return NULL;
    }

    char *svg_filename = make_temp_path("svgtmp", ".svg");
    FILE *svg_fp = fopen(svg_filename, "wb");
    if (!svg_fp) {
        g_set_error(err, g_quark_from_static_string("extract-base64"),
                    6, "Failed to open temp SVG file %s", svg_filename);
        g_free(svg_filename);
        g_string_free(out, TRUE);
        return NULL;
    }
    fwrite(out->str, 1, out->len, svg_fp);
    fclose(svg_fp);
    g_string_free(out, TRUE);

    remember_temp_path(&tmp_svg_paths, svg_filename);
    fprintf(stderr, "Wrote temporary SVG to %s\n", svg_filename);

    GFile *gfile = g_file_new_for_path(svg_filename);
    RsvgHandle *handle = rsvg_handle_new_from_gfile_sync(gfile, flags, cancellable, err);
    g_object_unref(gfile);
    g_free(svg_filename);

    return handle;
}

static gboolean set_frame_dimensions_from_handle(SvgFrame *f)
{
    if (!f || !f->handle) return FALSE;

#if LIBRSVG_CHECK_VERSION(2,46,0)
    double w = 0.0, h = 0.0;
    if (!rsvg_handle_get_intrinsic_size_in_pixels(f->handle, &w, &h) ||
        w <= 0.0 || h <= 0.0) {
        w = 800.0;
        h = 600.0;
    }
    f->width = w;
    f->height = h;
#else
    RsvgDimensionData dim;
    rsvg_handle_get_dimensions(f->handle, &dim);
    f->width = dim.width > 0 ? dim.width : 800;
    f->height = dim.height > 0 ? dim.height : 600;
#endif

    f->dimensions_valid = TRUE;
    return TRUE;
}

static gboolean load_handle_from_file(SvgFrame *f)
{
    GError *err = NULL;
    GFile *gfile = g_file_new_for_path(f->source_path);
    f->handle = rsvg_handle_new_from_gfile_sync(gfile, RSVG_HANDLE_FLAGS_NONE, NULL, &err);
    g_object_unref(gfile);

    if (!f->handle) {
        fprintf(stderr, "Warning: failed to load SVG file %s: %s\nTry extracting embedded PNGs instead.\n",
                f->source_path, err ? err->message : "unknown");
        g_clear_error(&err);

        gchar *content = NULL;
        gsize len = 0;
        if (!g_file_get_contents(f->source_path, &content, &len, &err)) {
            fprintf(stderr, "Failed to read %s for base64 fallback: %s\n",
                    f->source_path, err ? err->message : "unknown");
            g_clear_error(&err);
            return FALSE;
        }
        f->handle = extract_base64((const guint8 *)content, RSVG_HANDLE_FLAGS_NONE, NULL, &err);
        g_free(content);

        if (!f->handle) {
            fprintf(stderr, "Failed to load SVG file %s: %s\n",
                    f->source_path, err ? err->message : "unknown");
            g_clear_error(&err);
            return FALSE;
        }
    }

    return set_frame_dimensions_from_handle(f);
}

static gboolean load_handle_from_range(SvgFrame *f)
{
    char *content = NULL;
    gsize len = 0;
    GError *err = NULL;

    if (!read_frame_range(f, &content, &len)) return FALSE;

    f->handle = rsvg_handle_new_from_data((const guint8 *)content, len, &err);
    if (!f->handle) {
        fprintf(stderr, "Warning: failed to load SVG segment %s: %s\nTry extracting embedded PNGs instead.\n",
                f->path ? f->path : "(unknown)", err ? err->message : "unknown");
        g_clear_error(&err);

        f->handle = extract_base64((const guint8 *)content, RSVG_HANDLE_FLAGS_NONE, NULL, &err);
        if (!f->handle) {
            fprintf(stderr, "Failed to load SVG segment %s: %s\n",
                    f->path ? f->path : "(unknown)", err ? err->message : "unknown");
            g_clear_error(&err);
            g_free(content);
            return FALSE;
        }
    }

    g_free(content);
    return set_frame_dimensions_from_handle(f);
}

static gboolean load_frame_handle(SvgFrame *f)
{
    if (!f) return FALSE;
    if (f->handle && f->dimensions_valid) return TRUE;

    svg_frame_clear_loaded(f);
    if (f->use_range) return load_handle_from_range(f);
    return load_handle_from_file(f);
}

static int frame_distance_wrapped(int a, int b, int count)
{
    if (count <= 0) return 0;
    int d = abs(a - b);
    int rd = count - d;
    return d < rd ? d : rd;
}

static void evict_distant_handles(SvgSequence *seq)
{
    if (!seq || !seq->frames || seq->count <= 1) return;

    int radius = seq->handle_cache_radius;
    if (radius < 0) radius = 0;

    for (int i = 0; i < seq->count; i++) {
        if (i == seq->current_index) continue;
        if (frame_distance_wrapped(i, seq->current_index, seq->count) <= radius) continue;
        svg_frame_clear_loaded(&seq->frames[i]);
    }
}

static gboolean scan_split_file(SvgSequence *seq,
                                const char *path,
                                gboolean remove_source_on_free)
{
    GError *err = NULL;
    GMappedFile *mapped = g_mapped_file_new(path, FALSE, &err);
    if (!mapped) {
        fprintf(stderr, "Failed to map file %s: %s\n", path, err ? err->message : "unknown");
        g_clear_error(&err);
        return FALSE;
    }

    const char *data = g_mapped_file_get_contents(mapped);
    gsize len = g_mapped_file_get_length(mapped);
    const char marker[] = "<!-- RSFPY_SPLIT";
    const gsize marker_len = strlen(marker);
    const char end_marker[] = "-->";
    const gsize end_marker_len = strlen(end_marker);

    const char *base = data;
    const char *file_end = data + len;
    const char *m = find_mem(data, len, marker, marker_len);

    if (!m) {
        add_file_frame(seq, path, "Single file");
        if (remove_source_on_free && seq->count > 0)
            seq->frames[seq->count - 1].remove_source_on_free = TRUE;
        g_mapped_file_unref(mapped);
        return TRUE;
    }

    const char *pre_s = base;
    const char *pre_e = m;
    trim_range(base, &pre_s, &pre_e);
    if (pre_e > pre_s) {
        char *display = g_strdup_printf("%s.frame[%d]", path, seq->count);
        add_range_frame(seq, path, display, NULL,
                        (goffset)(pre_s - base), (gsize)(pre_e - pre_s),
                        remove_source_on_free);
        g_free(display);
    }

    while (m && m < file_end) {
        const char *comment_end = find_mem(m, (gsize)(file_end - m), end_marker, end_marker_len);
        if (!comment_end) {
            fprintf(stderr, "Warning: malformed split marker in %s\n", path);
            break;
        }
        comment_end += end_marker_len;

        char *label = extract_frame_label(m, comment_end);
        const char *content_s = comment_end;
        const char *next_m = find_mem(content_s, (gsize)(file_end - content_s), marker, marker_len);
        const char *content_e = next_m ? next_m : file_end;
        trim_range(base, &content_s, &content_e);

        if (content_e > content_s) {
            char *display = g_strdup_printf("%s.frame[%d]", path, seq->count);
            add_range_frame(seq, path, display, label,
                            (goffset)(content_s - base), (gsize)(content_e - content_s),
                            remove_source_on_free);
            g_free(display);
        }

        g_free(label);
        m = next_m;
    }

    g_mapped_file_unref(mapped);
    return TRUE;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

void cairo_set_source_rgba_string(cairo_t *cr, const char *color_str)
{
    double r = 0.0, g = 0.0, b = 0.0, a = 1.0;

    if (color_str && color_str[0] == '#') {
        int len = strlen(color_str);
        if (len == 4) {
            r = hexval(color_str[1]) / 15.0;
            g = hexval(color_str[2]) / 15.0;
            b = hexval(color_str[3]) / 15.0;
        } else if (len == 7) {
            r = (hexval(color_str[1]) * 16 + hexval(color_str[2])) / 255.0;
            g = (hexval(color_str[3]) * 16 + hexval(color_str[4])) / 255.0;
            b = (hexval(color_str[5]) * 16 + hexval(color_str[6])) / 255.0;
        } else if (len == 9) {
            r = (hexval(color_str[1]) * 16 + hexval(color_str[2])) / 255.0;
            g = (hexval(color_str[3]) * 16 + hexval(color_str[4])) / 255.0;
            b = (hexval(color_str[5]) * 16 + hexval(color_str[6])) / 255.0;
            a = (hexval(color_str[7]) * 16 + hexval(color_str[8])) / 255.0;
        }
    }

    cairo_set_source_rgba(cr, r, g, b, a);
}

gboolean svg_sequence_load_files(SvgSequence *seq, char **paths, int num)
{
    if (!seq || !paths || num <= 0) return FALSE;

    svg_sequence_init_if_needed(seq);
    seq->current_index = seq->count;
    if (seq->fps <= 0) seq->fps = 12;
    if (seq->handle_cache_radius < 0) seq->handle_cache_radius = SVG_SEQUENCE_DEFAULT_HANDLE_CACHE_RADIUS;

    int before = seq->count;
    for (int i = 0; i < num; i++) {
        if (!paths[i]) continue;
        scan_split_file(seq, paths[i], FALSE);
    }

    seq->playing = (seq->count > 1);
    return seq->count > before;
}

void svg_sequence_render_frame(SvgSequence *seq, cairo_t *cr,
                               int win_w, int win_h,
                               double pan_x, double pan_y,
                               double zoom_scale,
                               gboolean stretch_mode,
                               int toolbar_h, int hintbar_h)
{
    if (!seq || seq->count <= 0 || !seq->frames || !cr) return;

    if (seq->current_index < 0) seq->current_index = 0;
    if (seq->current_index >= seq->count) seq->current_index = seq->count - 1;

    SvgFrame *f = &seq->frames[seq->current_index];
    if (!load_frame_handle(f)) return;
    evict_distant_handles(seq);

    int content_h = win_h - toolbar_h - hintbar_h;
    if (content_h <= 0 || win_w <= 0 || f->width <= 0.0 || f->height <= 0.0) return;

    if (zoom_scale < MIN_ZOOM_SCALE) zoom_scale = MIN_ZOOM_SCALE;
    if (zoom_scale > MAX_ZOOM_SCALE) zoom_scale = MAX_ZOOM_SCALE;

    double sx, sy, s, dst_w, dst_h, ox, oy;

    if (stretch_mode) {
        dst_w = win_w * zoom_scale;
        dst_h = content_h * zoom_scale;
        sx = dst_w / f->width;
        sy = dst_h / f->height;
        s = 1.0;
    } else {
        sx = (double)win_w / f->width;
        sy = (double)content_h / f->height;
        s = (sx < sy ? sx : sy) * zoom_scale;
        dst_w = f->width * s;
        dst_h = f->height * s;
    }

    ox = (win_w - dst_w) / 2.0;
    oy = (content_h - dst_h) / 2.0;

    cairo_save(cr);
    cairo_set_source_rgba_string(cr, BACKGROUND_COLOR);
    cairo_rectangle(cr, 0, toolbar_h, win_w, content_h);
    cairo_fill(cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_rectangle(cr, 0, toolbar_h, win_w, content_h);
    cairo_clip(cr);

    cairo_translate(cr, ox + pan_x, toolbar_h + oy + pan_y);
    if (stretch_mode) {
        cairo_scale(cr, sx, sy);
    } else {
        cairo_scale(cr, s, s);
    }

#if LIBRSVG_CHECK_VERSION(2,52,0)
    RsvgRectangle viewport = {0.0, 0.0, f->width, f->height};
    GError *err = NULL;
    if (!rsvg_handle_render_document(f->handle, cr, &viewport, &err)) {
        fprintf(stderr, "Render error: %s\n", err ? err->message : "unknown");
        g_clear_error(&err);
    }
#else
    rsvg_handle_render_cairo(f->handle, cr);
#endif

    cairo_restore(cr);
}

void svg_sequence_advance(SvgSequence *seq)
{
    if (!seq || seq->count == 0) return;
    seq->current_index = (seq->current_index + 1) % seq->count;
}

gboolean svg_sequence_load_from_stream(SvgSequence *seq, const char *data, size_t len)
{
    if (!seq || !data || len == 0) return FALSE;

    svg_sequence_init_if_needed(seq);
    if (seq->fps <= 0) seq->fps = 12;

    char *tmp = make_temp_path("svgstream", ".svg");
    GError *err = NULL;
    if (!g_file_set_contents(tmp, data, (gssize)len, &err)) {
        fprintf(stderr, "Failed to write temporary stream SVG %s: %s\n",
                tmp, err ? err->message : "unknown");
        g_clear_error(&err);
        g_free(tmp);
        return FALSE;
    }

    /* The frames refer to this temporary source path.  Keep the path alive and
     * remove it once from the global temporary list during svg_sequence_free(). */
    remember_temp_path(&tmp_svg_paths, tmp);

    int before = seq->count;
    scan_split_file(seq, tmp, FALSE);
    g_free(tmp);

    seq->current_index = 0;
    seq->playing = (seq->count > 1);
    return seq->count > before;
}

void svg_sequence_set_handle_cache_radius(SvgSequence *seq, int radius)
{
    if (!seq) return;
    if (radius < 0) radius = 0;
    seq->handle_cache_radius = radius;
    evict_distant_handles(seq);
}

void svg_sequence_free(SvgSequence *seq)
{
    if (!seq) return;

    for (int i = 0; i < seq->count; i++) {
        svg_frame_free(&seq->frames[i]);
    }
    g_free(seq->frames);
    seq->frames = NULL;
    seq->count = 0;
    seq->capacity = 0;
    seq->current_index = 0;
    seq->playing = FALSE;
    seq->handle_cache_radius = SVG_SEQUENCE_DEFAULT_HANDLE_CACHE_RADIUS;

    if (png_paths) {
        for (guint i = 0; i < png_paths->len; i++) {
            char *p = g_ptr_array_index(png_paths, i);
            if (p) {
                remove(p);
                fprintf(stderr, "Removed temporary PNG file %s\n", p);
            }
        }
        g_ptr_array_free(png_paths, TRUE);
        png_paths = NULL;
    }

    if (tmp_svg_paths) {
        for (guint i = 0; i < tmp_svg_paths->len; i++) {
            char *p = g_ptr_array_index(tmp_svg_paths, i);
            if (p) {
                remove(p);
                fprintf(stderr, "Removed temporary SVG file %s\n", p);
            }
        }
        g_ptr_array_free(tmp_svg_paths, TRUE);
        tmp_svg_paths = NULL;
    }
}
