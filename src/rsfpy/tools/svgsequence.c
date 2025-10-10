#include "svgsequence.h"


static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

void cairo_set_source_rgba_string(cairo_t *cr, const char *color_str) {
    double r=0, g=0, b=0, a=1.0;

    if (color_str[0] == '#') {
        int len = strlen(color_str);
        if (len == 4) { // #rgb
            r = hexval(color_str[1]) / 15.0;
            g = hexval(color_str[2]) / 15.0;
            b = hexval(color_str[3]) / 15.0;
        } else if (len == 7) { // #rrggbb
            r = (hexval(color_str[1]) * 16 + hexval(color_str[2])) / 255.0;
            g = (hexval(color_str[3]) * 16 + hexval(color_str[4])) / 255.0;
            b = (hexval(color_str[5]) * 16 + hexval(color_str[6])) / 255.0;
        } else if (len == 9) { // #rrggbbaa
            r = (hexval(color_str[1]) * 16 + hexval(color_str[2])) / 255.0;
            g = (hexval(color_str[3]) * 16 + hexval(color_str[4])) / 255.0;
            b = (hexval(color_str[5]) * 16 + hexval(color_str[6])) / 255.0;
            a = (hexval(color_str[7]) * 16 + hexval(color_str[8])) / 255.0;
        }
    }

    cairo_set_source_rgba(cr, r, g, b, a);
}

gboolean svg_sequence_load_files(SvgSequence *seq, char **paths, int num) {
    // Assume seq is already initialized
    // seq->count = 0;
    const char * path, *splitter = "<!-- RSFPY_SPLIT";;
    GError *err;
    gchar *content;
    gsize len;
    char *svg_start, *svg_content, *label_end, *label_start, *label,
         **segments, *segment;

    seq->current_index = seq->count;
    seq->fps = 4;
    for (int i = 0; i < num && seq->count < MAX_FRAMES; i++) {
        path = paths[i];
        err = NULL;
        content = NULL;
        len = 0;

        if (!g_file_get_contents(path, &content, &len, &err)) {
            fprintf(stderr, "Failed to read file: %s\n", path);
            g_clear_error(&err);
            continue;
        }

        if (strstr(content, splitter)) {
            segments = g_strsplit(content, splitter, MAX_FRAMES - seq->count + 1);
            for (int j = 0; segments[j] && seq->count < MAX_FRAMES; j++) {
                segment = g_strstrip(segments[j]);
                if (strlen(segment) == 0) continue;

                label = NULL;
                label_start = strstr(segment, "framelabel=\"");
                if (label_start) {
                    label_start += strlen("framelabel=\"");
                    label_end = strchr(label_start, '"');
                    if (label_end && label_end > label_start) {
                        label = g_strndup(label_start, label_end - label_start);
                    }
                }

                if (j!=0){
                    svg_start = strstr(segment, "-->");
                    if (!svg_start) continue;
                    svg_start += 3;
                    svg_content = g_strstrip(svg_start);
                    if (strlen(svg_content) == 0) continue;
                }else{
                    svg_content = segment;
                }

                SvgFrame *f = &seq->frames[seq->count];
                f->path = g_strdup_printf("%s.frame[%d]", path, j-1);
                f->framelabel = label ? label : g_strdup_printf("Frame %d", seq->count-1);
                f->handle = rsvg_handle_new_from_data((const guint8 *)svg_content, strlen(svg_content), &err);
                if (!f->handle) {
                    fprintf(stderr, "Failed to load SVG segment %d from %s: %s\n", j, path, err->message);
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
            SvgFrame *f = &seq->frames[seq->count];
            f->path = g_strdup(path);
            f->framelabel = g_strdup("Single file");
            f->handle = rsvg_handle_new_from_data((const guint8*)content, len, &err);
            if (!f->handle) {
                fprintf(stderr, "Failed to load SVG file: %s, %s\n", path, err->message);
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
                               int pan_x, int pan_y,
                               double zoom_scale,
                               int toolbar_h, int hintbar_h) {
    if (seq->count == 0) return;
    SvgFrame *f = &seq->frames[seq->current_index];
    if (!f->handle && !load_svg(f)) return;
    
    int content_h = win_h - toolbar_h - hintbar_h;
    char msg_buf[1024];
    double x, y, cx, cy, tx, ty, sx, sy, s, dst_w, dst_h, ox, oy;
    const char *msg = "Surface creation failed:";
    cairo_t *cr_surf;
    cairo_text_extents_t extents;
    #if LIBRSVG_CHECK_VERSION(2,52,0)
        RsvgRectangle viewport = {0, 0, f->width, f->height};
        GError *err = NULL;
    #endif


    sx = (double)win_w / f->width;
    sy = (double)content_h / f->height;
    s = (sx < sy ? sx : sy) * zoom_scale;

    dst_w = f->width * s;
    dst_h = f->height * s;
    ox = (win_w - dst_w) / 2;
    oy = (content_h - dst_h) / 2;

    cairo_set_source_rgba_string(cr, BACKGROUND_COLOR);
    cairo_rectangle(cr, 0, toolbar_h, win_w, content_h);
    cairo_fill(cr);

    gboolean cache_invalid = FALSE;
    if (!f->rendered || !f->surface) {
        cache_invalid = TRUE;
    } else {
        if (abs(win_w - f->cached_win_w) > 50 ||
            abs(win_h - f->cached_win_h) > 50) {
            cache_invalid = TRUE;
        }
        if (fabs(zoom_scale - f->cached_scale) / f->cached_scale > 0.5) {
            cache_invalid = TRUE;
        }
    }

    if (cache_invalid) {
        if (f->surface) {
            cairo_surface_destroy(f->surface);
            f->surface = NULL;
        }
        f->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                (int)f->width * s,
                                                (int)f->height * s);
        if (cairo_surface_status(f->surface) != CAIRO_STATUS_SUCCESS) {
            snprintf(msg_buf, sizeof(msg_buf), "%s",
                     cairo_status_to_string(cairo_surface_status(f->surface)));

            cx = win_w / 2.0;
            cy = win_h / 2.0;
            x = cx - win_w / 2.0;
            y = cy - content_h / 2.0;

            cairo_save(cr);
            cairo_set_source_rgba_string(cr, BACKGROUND_COLOR);
            cairo_rectangle(cr, x, y, win_w, content_h);
            cairo_fill(cr);

            cairo_set_source_rgba_string(cr, WARNING_COLOR);
            cairo_set_line_width(cr, 2.0);
            cairo_rectangle(cr, x, y, win_w, content_h);
            cairo_stroke(cr);

            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 20);
            
            cairo_text_extents(cr, msg_buf, &extents);

            tx = cx - extents.width / 2 - extents.x_bearing;
            ty = cy - extents.height / 2 - extents.y_bearing - 5;

            cairo_move_to(cr, tx, ty);
            cairo_show_text(cr, msg);

            ty = ty + extents.height + 5;


            cairo_move_to(cr, tx, ty);
            cairo_show_text(cr, msg_buf);

            cairo_restore(cr);

            cairo_surface_destroy(f->surface);
            f->surface = NULL;
            f->rendered = FALSE;
            return;
        }
        cr_surf = cairo_create(f->surface);
        cairo_scale(cr_surf, s, s);

    #if LIBRSVG_CHECK_VERSION(2,52,0)
        
        rsvg_handle_render_document(f->handle, cr_surf, &viewport, &err);
        if (err) {
            fprintf(stderr, "Render error: %s\n", err->message);
            g_error_free(err);
        }
    #else
        rsvg_handle_render_cairo(f->handle, cr_surf);
    #endif

        cairo_destroy(cr_surf);
        f->rendered = TRUE;
        f->cached_scale = zoom_scale;
        f->cached_win_w = win_w;
        f->cached_win_h = win_h;
    }
    cairo_save(cr);
    cairo_rectangle(cr, 0, toolbar_h, win_w, content_h);
    cairo_translate(cr, ox + pan_x, oy + toolbar_h + pan_y);
    cairo_clip(cr);
    if (!cache_invalid) cairo_scale(cr, zoom_scale/f->cached_scale, zoom_scale/f->cached_scale);

    cairo_set_source_surface(cr, f->surface, 0, 0);
    cairo_paint(cr);
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
    char *svg_start, *svg_content, *label_end, *label_start, *label,
         **segments, *segment;
    GError *err = NULL;

    if (strstr(copy, splitter)) {
        segments = g_strsplit(copy, splitter, MAX_FRAMES + 1);
        for (int i = 0; segments[i] && seq->count < MAX_FRAMES; i++) {
            segment = g_strstrip(segments[i]);
            if (strlen(segment) == 0) continue;

            label = NULL;
            label_start = strstr(segment, "framelabel=\"");
            if (label_start) {
                label_start += strlen("framelabel=\"");
                label_end = strchr(label_start, '"');
                if (label_end && label_end > label_start) {
                    label = g_strndup(label_start, label_end - label_start);
                }
            }

            if (i!=0){
                svg_start = strstr(segment, "-->");
                if (!svg_start) continue;
                svg_start += 3;
                svg_content = g_strstrip(svg_start);
                if (strlen(svg_content) == 0) continue;
            }else{
                svg_content = segment;
            }

            SvgFrame *f = &seq->frames[seq->count];
            f->path = g_strdup_printf("stdin[%d]", i-1);
            f->framelabel = label ? label : g_strdup_printf("Frame %d", i-1);
            f->handle = rsvg_handle_new_from_data((const guint8 *)svg_content, strlen(svg_content), &err);
            if (!f->handle) {
                fprintf(stderr, "Failed to load SVG segment %d: %s\n", i, err->message);
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
        SvgFrame *f = &seq->frames[0];
        f->path = g_strdup("stdin");
        f->framelabel = g_strdup("Single file");
        f->handle = rsvg_handle_new_from_data((const guint8 *)copy, len, &err);
        if (!f->handle) {
            fprintf(stderr, "Failed to load single SVG stream: %s\n", err->message);
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
