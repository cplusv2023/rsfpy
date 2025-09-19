#include "svg_sequence.h"
#include <string.h>
#include <stdio.h>

gboolean svg_sequence_load_files(SvgSequence *seq, char **paths, int num) {
    seq->count = 0;
    seq->current_index = 0;
    seq->fps = 4;

    for (int i = 0; i < num && seq->count < MAX_FRAMES; i++) {
        const char *path = paths[i];
        GError *err = NULL;
        gchar *content = NULL;
        gsize len = 0;

        if (!g_file_get_contents(path, &content, &len, &err)) {
            fprintf(stderr, "Failed to read file: %s\n", path);
            g_clear_error(&err);
            continue;
        }

        const char *splitter = "<!-- RSFPY_SPLIT";
        if (strstr(content, splitter)) {
            // 拼接格式
            char **segments = g_strsplit(content, splitter, MAX_FRAMES - seq->count + 1);
            for (int j = 1; segments[j] && seq->count < MAX_FRAMES; j++) {
                char *segment = g_strstrip(segments[j]);
                if (strlen(segment) == 0) continue;

                // 提取 framelabel
                char *label = NULL;
                char *label_start = strstr(segment, "framelabel=\"");
                if (label_start) {
                    label_start += strlen("framelabel=\"");
                    char *label_end = strchr(label_start, '"');
                    if (label_end && label_end > label_start) {
                        label = g_strndup(label_start, label_end - label_start);
                    }
                }

                // 找到 SVG 内容起始位置
                char *svg_start = strstr(segment, "-->");
                if (!svg_start) continue;
                svg_start += 3;

                char *svg_content = g_strstrip(svg_start);
                if (strlen(svg_content) == 0) continue;

                SvgFrame *f = &seq->frames[seq->count];
                f->path = g_strdup_printf("%s.frame[%d]", path, j - 1);
                f->framelabel = label ? label : g_strdup_printf("Frame %d", seq->count);
                f->handle = rsvg_handle_new_from_data((const guint8 *)svg_content, strlen(svg_content), NULL);
                if (!f->handle) {
                    fprintf(stderr, "Failed to load SVG segment %d from %s\n", j - 1, path);
                    g_free(f->framelabel);
                    continue;
                }

#if LIBRSVG_CHECK_VERSION(2,46,0)
                double w = 0, h = 0;
                if (!rsvg_handle_get_intrinsic_size_in_pixels(f->handle, &w, &h)) {
                    w = 800; h = 600;
                }
                f->width = w;
                f->height = h;
#else
                RsvgDimensionData dim;
                rsvg_handle_get_dimensions(f->handle, &dim);
                f->width = dim.width;
                f->height = dim.height;
#endif
                f->surface = NULL;
                f->rendered = FALSE;
                seq->count++;
            }
            g_strfreev(segments);
        } else {
            // 普通 SVG 文件
            SvgFrame *f = &seq->frames[seq->count];
            f->path = g_strdup(path);
            f->framelabel = g_strdup("Single file");
            f->handle = rsvg_handle_new_from_data((const guint8 *)content, len, NULL);
            if (!f->handle) {
                fprintf(stderr, "Failed to load SVG file: %s\n", path);
                g_free(content);
                g_free(f->framelabel);
                continue;
            }

#if LIBRSVG_CHECK_VERSION(2,46,0)
            double w = 0, h = 0;
            if (!rsvg_handle_get_intrinsic_size_in_pixels(f->handle, &w, &h)) {
                w = 800; h = 600;
            }
            f->width = w;
            f->height = h;
#else
            RsvgDimensionData dim;
            rsvg_handle_get_dimensions(f->handle, &dim);
            f->width = dim.width;
            f->height = dim.height;
#endif
            f->surface = NULL;
            f->rendered = FALSE;
            seq->count++;
        }

        g_free(content);
    }

    seq->playing = (seq->count > 1);
    return seq->count > 0;
}


static gboolean load_svg(SvgFrame *f) {
    GError *err = NULL;
    f->handle = rsvg_handle_new_from_file(f->path, &err);
    if (!f->handle) {
        fprintf(stderr, "Failed to load %s: %s\n", f->path, err ? err->message : "unknown");
        if (err) g_error_free(err);
        return FALSE;
    }
#if LIBRSVG_CHECK_VERSION(2,46,0)
    double w = 0, h = 0;
    if (!rsvg_handle_get_intrinsic_size_in_pixels(f->handle, &w, &h)) {
        w = 800; h = 600;
    }
    f->width = w;
    f->height = h;
#else
    RsvgDimensionData dim;
    rsvg_handle_get_dimensions(f->handle, &dim);
    f->width = dim.width;
    f->height = dim.height;
#endif
    return TRUE;
}

void svg_sequence_render_frame(SvgSequence *seq, cairo_t *cr,
                               int win_w, int win_h,
                               int toolbar_h, int hintbar_h) {
    if (seq->count == 0) return;
    SvgFrame *f = &seq->frames[seq->current_index];
    if (!f->handle && !load_svg(f)) return;

    // 清空背景
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    // 内容区域尺寸
    int content_h = win_h - toolbar_h - hintbar_h;
    double sx = (double)win_w / (f->width * PXPT_TRANS);
    double sy = (double)content_h / (f->height * PXPT_TRANS);
    double s = sx < sy ? sx : sy;

    // 缩放后尺寸与偏移
    double dst_w = (f->width * PXPT_TRANS)* s;
    double dst_h = (f->height * PXPT_TRANS) * s;
    double ox = (win_w - dst_w) / 2;
    double oy = (content_h - dst_h) / 2;

    cairo_save(cr);
    cairo_translate(cr, ox, oy + toolbar_h); // 加上 toolbar 高度
    cairo_scale(cr, s, s);

#if LIBRSVG_CHECK_VERSION(2,52,0)
    RsvgRectangle viewport = {0, 0, f->width, f->height};
    GError *err = NULL;
    rsvg_handle_render_document(f->handle, cr, &viewport, &err);
    if (err) {
        fprintf(stderr, "Render error: %s\n", err->message);
        g_error_free(err);
    }
#else
    rsvg_handle_render_cairo(f->handle, cr);
#endif

    cairo_restore(cr);
    cairo_surface_flush(cairo_get_target(cr));
}

void svg_sequence_advance(SvgSequence *seq) {
    if (seq->count == 0) return;
    seq->current_index = (seq->current_index + 1) % seq->count;
}

void svg_sequence_free(SvgSequence *seq) {
    for (int i = 0; i < seq->count; i++) {
        SvgFrame *f = &seq->frames[i];
        if (f->handle) g_object_unref(f->handle);
        if (f->surface) cairo_surface_destroy(f->surface);
        if (f->path) g_free(f->path);
    }
    seq->count = 0;
}

gboolean svg_sequence_load_from_stream(SvgSequence *seq, const char *data, size_t len) {
    seq->count = 0;
    seq->current_index = 0;
    seq->fps = 4;
    seq->playing = TRUE;

    char *copy = g_strndup(data, len);
    const char *splitter = "<!-- RSFPY_SPLIT";

    if (strstr(copy, splitter)) {
        // 多段 SVG
        char **segments = g_strsplit(copy, splitter, MAX_FRAMES + 1);
        for (int i = 1; segments[i] && seq->count < MAX_FRAMES; i++) {
            char *segment = g_strstrip(segments[i]);
            if (strlen(segment) == 0) continue;

            // 提取 framelabel
            char *label = NULL;
            char *label_start = strstr(segment, "framelabel=\"");
            if (label_start) {
                label_start += strlen("framelabel=\"");
                char *label_end = strchr(label_start, '"');
                if (label_end && label_end > label_start) {
                    label = g_strndup(label_start, label_end - label_start);
                }
            }

            // 找到 SVG 内容起始位置（跳过注释尾部 -->）
            char *svg_start = strstr(segment, "-->");
            if (!svg_start) continue;
            svg_start += 3;

            char *svg_content = g_strstrip(svg_start);
            if (strlen(svg_content) == 0) continue;

            SvgFrame *f = &seq->frames[seq->count];
            f->path = g_strdup_printf("stream[%d]", i - 1);
            f->framelabel = label ? label : g_strdup_printf("Frame %d", i - 1);
            f->handle = rsvg_handle_new_from_data((const guint8 *)svg_content, strlen(svg_content), NULL);
            if (!f->handle) {
                fprintf(stderr, "Failed to load SVG segment %d\n", i - 1);
                g_free(f->framelabel);
                continue;
            }

#if LIBRSVG_CHECK_VERSION(2,46,0)
            double w = 0, h = 0;
            if (!rsvg_handle_get_intrinsic_size_in_pixels(f->handle, &w, &h)) {
                w = 800; h = 600;
            }
            f->width = w;
            f->height = h;
#else
            RsvgDimensionData dim;
            rsvg_handle_get_dimensions(f->handle, &dim);
            f->width = dim.width;
            f->height = dim.height;
#endif
            f->surface = NULL;
            f->rendered = FALSE;
            seq->count++;
        }
        g_strfreev(segments);
    } else {
        // 单个 SVG
        SvgFrame *f = &seq->frames[0];
        f->path = g_strdup("stream[0]");
        f->framelabel = g_strdup("Single file");
        f->handle = rsvg_handle_new_from_data((const guint8 *)copy, len, NULL);
        if (!f->handle) {
            fprintf(stderr, "Failed to load single SVG stream\n");
            g_free(copy);
            g_free(f->framelabel);
            return FALSE;
        }

#if LIBRSVG_CHECK_VERSION(2,46,0)
        double w = 0, h = 0;
        if (!rsvg_handle_get_intrinsic_size_in_pixels(f->handle, &w, &h)) {
            w = 800; h = 600;
        }
        f->width = w;
        f->height = h;
#else
        RsvgDimensionData dim;
        rsvg_handle_get_dimensions(f->handle, &dim);
        f->width = dim.width;
        f->height = dim.height;
#endif
        f->surface = NULL;
        f->rendered = FALSE;
        seq->count = 1;
    }

    g_free(copy);
    seq->playing = (seq->count > 1);
    return seq->count > 0;
}
