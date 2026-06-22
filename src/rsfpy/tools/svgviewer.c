#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "svgsequence.h"

typedef enum {
    TOOL_NONE = 0,
    TOOL_PAN,
    TOOL_ZOOM,
    TOOL_PEN
} ToolMode;

typedef enum {
    PEN_TOOL_BRUSH = 0,
    PEN_TOOL_BRUSH_DASH,
    PEN_TOOL_RECT,
    PEN_TOOL_RECT_DASH,
    PEN_TOOL_ARROW,
    PEN_TOOL_ERASER_PARTIAL,
    PEN_TOOL_ERASER_FULL
} PenTool;

typedef enum {
    COLOR_SLOT_RECENT0 = 0,
    COLOR_SLOT_RECENT1,
    COLOR_SLOT_RECENT2,
    COLOR_SLOT_CUSTOM
} ColorSlot;

typedef enum {
    ANNO_BRUSH = 0,
    ANNO_BRUSH_DASH,
    ANNO_RECT,
    ANNO_RECT_DASH,
    ANNO_ARROW
} AnnotationType;

typedef struct {
    double x;
    double y;
} DocPoint;

typedef struct {
    GdkRGBA color;
    double width_doc;
    gboolean dashed;
} AnnotationStyle;

typedef struct {
    AnnotationType type;
    AnnotationStyle style;
    GArray *points;          /* DocPoint[], used by freehand strokes. */
    double x0, y0, x1, y1;   /* Used by rectangle and arrow. */
} Annotation;

typedef struct {
    double sx;
    double sy;
    double tx;
    double ty;
    gboolean valid;
} ViewTransform;

typedef struct {
    GtkApplication *gtk_app;
    GtkWidget *window;
    GtkWidget *root_box;
    GtkWidget *main_toolbar;
    GtkWidget *pen_revealer;
    GtkWidget *pen_toolbar;
    GtkWidget *pen_tool_dropdown;
    GtkWidget *pen_width_dropdown;
    GtkWidget *pen_clear_button;
    GtkWidget *color_btn_red;
    GtkWidget *color_btn_green;
    GtkWidget *color_btn_blue;
    GtkWidget *color_btn_custom;
    GtkWidget *color_area_red;
    GtkWidget *color_area_green;
    GtkWidget *color_area_blue;
    GtkWidget *color_area_custom;
    GtkWidget *area;
    GtkWidget *status_label;

    GtkWidget *btn_prev;
    GtkWidget *btn_next;
    GtkWidget *btn_run;
    GtkWidget *btn_pause;
    GtkWidget *btn_slower;
    GtkWidget *btn_faster;
    GtkWidget *btn_reset;
    GtkWidget *btn_undo;
    GtkWidget *btn_redo;

    GtkWidget *toggle_pan;
    GtkWidget *toggle_zoom;
    GtkWidget *toggle_stretch;
    GtkWidget *toggle_pen;

    SvgSequence sequence;

    int canvas_w;
    int canvas_h;

    double pan_x;
    double pan_y;
    double drag_start_x;
    double drag_start_y;
    double drag_origin_pan_x;
    double drag_origin_pan_y;
    gboolean dragging;
    gboolean zoom_box_active;
    double zoom_box_x0;
    double zoom_box_y0;
    double zoom_box_x1;
    double zoom_box_y1;
    gboolean pointer_in_canvas;
    double pointer_x;
    double pointer_y;

    double zoom_scale;
    gboolean stretch_mode;
    ToolMode tool;
    PenTool pen_tool;
    double pen_width;
    GdkRGBA pen_color;
    GdkRGBA custom_color;
    GdkRGBA recent_colors[3];
    ColorSlot color_slot;
    gboolean updating_ui;

    GPtrArray *annotations;
    GPtrArray *undo_stack;
    GPtrArray *redo_stack;
    Annotation *active_annotation;

    gint64 last_frame_ms;
    guint tick_id;
} App;

static const char *APP_ID = "org.rsfpy.svgviewer.gtk";
#define UNDO_LIMIT 64

static const char *ICON_PREV =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='#20242a' d='M6 5h2v14H6zM18 5v14l-8-7z'/>"
    "</svg>";

static const char *ICON_NEXT =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='#20242a' d='M16 5h2v14h-2zM6 5v14l8-7z'/>"
    "</svg>";

static const char *ICON_PLAY =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='#20242a' d='M7 4v16l13-8z'/>"
    "</svg>";

static const char *ICON_PAUSE =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='#20242a' d='M7 5h4v14H7zM13 5h4v14h-4z'/>"
    "</svg>";

static const char *ICON_SLOWER =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='#20242a' d='M11 5v14l-8-7zM21 5v14l-8-7z'/>"
    "</svg>";

static const char *ICON_FASTER =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='#20242a' d='M3 5v14l8-7zM13 5v14l8-7z'/>"
    "</svg>";

static const char *ICON_PAN =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<g fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M12 3v18M3 12h18M12 3l-3 3M12 3l3 3M12 21l-3-3M12 21l3-3M3 12l3-3M3 12l3 3M21 12l-3-3M21 12l-3 3'/>"
    "</g>"
    "</svg>";

static const char *ICON_ZOOM =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<g fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round'>"
    "<circle cx='10' cy='10' r='6'/>"
    "<path d='M15 15l5 5M7 10h6M10 7v6'/>"
    "</g>"
    "</svg>";

static const char *ICON_PEN =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' d='M4 20l4-1 11-11-3-3L5 16zM14 6l3 3'/>"
    "</svg>";

static const char *ICON_RESET =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' d='M20 12a8 8 0 1 1-2.34-5.66L20 9M20 4v5h-5'/>"
    "</svg>";

static const char *ICON_STRETCH =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<g fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M8 8L3 3M3 3h6M3 3v6M16 8l5-5M21 3h-6M21 3v6M8 16l-5 5M3 21h6M3 21v-6M16 16l5 5M21 21h-6M21 21v-6'/>"
    "</g>"
    "</svg>";

static const char *ICON_UNDO =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' d='M9 7H4v5M4 7l5 5M5 12a8 8 0 1 0 2-5'/>"
    "</svg>";

static const char *ICON_REDO =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' d='M15 7h5v5M20 7l-5 5M19 12a8 8 0 1 1-2-5'/>"
    "</svg>";

static const char *ICON_CLEAR =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<g fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M4 7h16M10 11v6M14 11v6M6 7l1 14h10l1-14M9 7V4h6v3'/>"
    "</g>"
    "</svg>";

static void update_ui(App *app);
static void queue_canvas(App *app);
static gboolean pen_tool_is_eraser(App *app);
static gboolean pen_tool_is_partial_eraser(App *app);

static gint64 now_ms(void)
{
    return g_get_monotonic_time() / 1000;
}

static void annotation_free(gpointer ptr)
{
    Annotation *a = (Annotation *)ptr;
    if (!a) return;
    if (a->points) g_array_free(a->points, TRUE);
    g_free(a);
}

static Annotation *annotation_new(AnnotationType type, const AnnotationStyle *style)
{
    Annotation *a = g_new0(Annotation, 1);
    a->type = type;
    if (style) a->style = *style;
    if (type == ANNO_BRUSH || type == ANNO_BRUSH_DASH) {
        a->points = g_array_new(FALSE, FALSE, sizeof(DocPoint));
    }
    return a;
}

static void annotation_add_point(Annotation *a, double x, double y)
{
    if (!a || !a->points) return;
    DocPoint p = {x, y};
    g_array_append_val(a->points, p);
}

static double dist2(double x0, double y0, double x1, double y1)
{
    double dx = x0 - x1;
    double dy = y0 - y1;
    return dx * dx + dy * dy;
}

static double local_sqrt(double x)
{
    if (x <= 0.0) return 0.0;
    double y = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 12; i++) {
        y = 0.5 * (y + x / y);
    }
    return y;
}

static double point_segment_distance(double px, double py,
                                     double ax, double ay,
                                     double bx, double by)
{
    double vx = bx - ax;
    double vy = by - ay;
    double wx = px - ax;
    double wy = py - ay;
    double c1 = vx * wx + vy * wy;
    double c2 = vx * vx + vy * vy;

    if (c2 <= 1e-20) return local_sqrt(dist2(px, py, ax, ay));

    double t = c1 / c2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    double qx = ax + t * vx;
    double qy = ay + t * vy;
    return local_sqrt(dist2(px, py, qx, qy));
}

static double min4(double a, double b, double c, double d)
{
    double m = a < b ? a : b;
    if (c < m) m = c;
    if (d < m) m = d;
    return m;
}

static gboolean annotation_is_freehand(const Annotation *a)
{
    return a && (a->type == ANNO_BRUSH || a->type == ANNO_BRUSH_DASH);
}

static gboolean annotation_is_valid(const Annotation *a)
{
    if (!a) return FALSE;
    if (annotation_is_freehand(a)) return a->points && a->points->len >= 2;
    if (a->type == ANNO_RECT || a->type == ANNO_RECT_DASH) {
        return fabs(a->x1 - a->x0) > 1e-6 && fabs(a->y1 - a->y0) > 1e-6;
    }
    if (a->type == ANNO_ARROW) {
        return dist2(a->x0, a->y0, a->x1, a->y1) > 1e-12;
    }
    return FALSE;
}

static void annotation_layer_clear(App *app)
{
    if (!app || !app->annotations) return;
    app->active_annotation = NULL;
    g_ptr_array_set_size(app->annotations, 0);
}

static Annotation *annotation_clone(const Annotation *src)
{
    if (!src) return NULL;

    Annotation *dst = annotation_new(src->type, &src->style);
    dst->x0 = src->x0;
    dst->y0 = src->y0;
    dst->x1 = src->x1;
    dst->y1 = src->y1;

    if (src->points && dst->points) {
        g_array_append_vals(dst->points, src->points->data, src->points->len);
    }

    return dst;
}

static GPtrArray *annotation_layer_clone(GPtrArray *src)
{
    GPtrArray *dst = g_ptr_array_new_with_free_func(annotation_free);
    if (!src) return dst;

    for (guint i = 0; i < src->len; i++) {
        Annotation *a = g_ptr_array_index(src, i);
        Annotation *copy = annotation_clone(a);
        if (copy) g_ptr_array_add(dst, copy);
    }

    return dst;
}

static void annotation_layer_snapshot_free(gpointer ptr)
{
    if (ptr) g_ptr_array_free((GPtrArray *)ptr, TRUE);
}

static void annotation_layer_replace(App *app, GPtrArray *snapshot)
{
    if (!app || !snapshot) return;

    if (app->annotations) g_ptr_array_free(app->annotations, TRUE);
    app->annotations = snapshot;
    app->active_annotation = NULL;
}

static void history_clear_stack(GPtrArray *stack)
{
    if (!stack) return;
    g_ptr_array_set_size(stack, 0);
}

static void history_push_snapshot(GPtrArray *stack, GPtrArray *snapshot)
{
    if (!stack || !snapshot) return;

    g_ptr_array_add(stack, snapshot);
    while (stack->len > UNDO_LIMIT) {
        g_ptr_array_remove_index(stack, 0);
    }
}

static void history_save_before_edit(App *app)
{
    if (!app || !app->annotations || !app->undo_stack || !app->redo_stack) return;

    history_push_snapshot(app->undo_stack, annotation_layer_clone(app->annotations));
    history_clear_stack(app->redo_stack);
}

static void history_undo(App *app)
{
    if (!app || !app->undo_stack || app->undo_stack->len == 0) return;

    GPtrArray *current = annotation_layer_clone(app->annotations);
    GPtrArray *prev = g_ptr_array_steal_index(app->undo_stack, app->undo_stack->len - 1);

    history_push_snapshot(app->redo_stack, current);
    annotation_layer_replace(app, prev);
    update_ui(app);
    queue_canvas(app);
}

static void history_redo(App *app)
{
    if (!app || !app->redo_stack || app->redo_stack->len == 0) return;

    GPtrArray *current = annotation_layer_clone(app->annotations);
    GPtrArray *next = g_ptr_array_steal_index(app->redo_stack, app->redo_stack->len - 1);

    history_push_snapshot(app->undo_stack, current);
    annotation_layer_replace(app, next);
    update_ui(app);
    queue_canvas(app);
}

static double selected_width_from_index(guint idx)
{
    static const double values[] = {
        0.1, 0.25, 0.5, 1.0, 1.5, 2.0, 2.5,
        3.0, 3.5, 4.0, 4.5, 5.0
    };
    if (idx >= G_N_ELEMENTS(values)) idx = 7;
    return values[idx];
}

static guint width_index_from_value(double value)
{
    static const double values[] = {
        0.1, 0.25, 0.5, 1.0, 1.5, 2.0, 2.5,
        3.0, 3.5, 4.0, 4.5, 5.0
    };
    guint best = 7;
    double best_err = fabs(value - values[best]);
    for (guint i = 0; i < G_N_ELEMENTS(values); i++) {
        double err = fabs(value - values[i]);
        if (err < best_err) {
            best = i;
            best_err = err;
        }
    }
    return best;
}

static SvgFrame *current_frame(App *app)
{
    if (!app || app->sequence.count <= 0 || !app->sequence.frames) return NULL;
    if (app->sequence.current_index < 0) app->sequence.current_index = 0;
    if (app->sequence.current_index >= app->sequence.count) {
        app->sequence.current_index = app->sequence.count - 1;
    }
    return &app->sequence.frames[app->sequence.current_index];
}

static gboolean compute_view_transform(App *app, ViewTransform *tr)
{
    if (!app || !tr) return FALSE;
    memset(tr, 0, sizeof(*tr));

    SvgFrame *f = current_frame(app);
    if (!f || f->width <= 0.0 || f->height <= 0.0 || app->canvas_w <= 0 || app->canvas_h <= 0) {
        return FALSE;
    }

    double zoom = app->zoom_scale;
    if (zoom < MIN_ZOOM_SCALE) zoom = MIN_ZOOM_SCALE;
    if (zoom > MAX_ZOOM_SCALE) zoom = MAX_ZOOM_SCALE;

    double dst_w, dst_h;
    if (app->stretch_mode) {
        dst_w = app->canvas_w * zoom;
        dst_h = app->canvas_h * zoom;
        tr->sx = dst_w / f->width;
        tr->sy = dst_h / f->height;
    } else {
        double sx = (double)app->canvas_w / f->width;
        double sy = (double)app->canvas_h / f->height;
        double s = (sx < sy ? sx : sy) * zoom;
        tr->sx = s;
        tr->sy = s;
        dst_w = f->width * s;
        dst_h = f->height * s;
    }

    tr->tx = (app->canvas_w - dst_w) * 0.5 + app->pan_x;
    tr->ty = (app->canvas_h - dst_h) * 0.5 + app->pan_y;
    tr->valid = TRUE;
    return TRUE;
}

static void apply_view_transform(App *app, cairo_t *cr)
{
    ViewTransform tr;
    if (!compute_view_transform(app, &tr)) return;
    cairo_translate(cr, tr.tx, tr.ty);
    cairo_scale(cr, tr.sx, tr.sy);
}

static gboolean screen_to_doc(App *app, double sx, double sy, double *dx, double *dy)
{
    ViewTransform tr;
    if (!compute_view_transform(app, &tr)) return FALSE;
    if (fabs(tr.sx) < 1e-20 || fabs(tr.sy) < 1e-20) return FALSE;
    if (dx) *dx = (sx - tr.tx) / tr.sx;
    if (dy) *dy = (sy - tr.ty) / tr.sy;
    return TRUE;
}

static double current_doc_width_for_screen_width(App *app, double screen_width)
{
    ViewTransform tr;
    if (!compute_view_transform(app, &tr)) return screen_width;
    double s = (fabs(tr.sx) + fabs(tr.sy)) * 0.5;
    if (s <= 1e-20) return screen_width;
    return screen_width / s;
}

static double current_doc_eraser_radius(App *app)
{
    double screen_radius = app ? app->pen_width * 2.0 : 6.0;
    if (screen_radius < 6.0) screen_radius = 6.0;
    return current_doc_width_for_screen_width(app, screen_radius);
}

static const char *tool_name(ToolMode tool)
{
    switch (tool) {
        case TOOL_PAN:  return "Pan";
        case TOOL_ZOOM: return "Zoom";
        case TOOL_PEN:  return "Pen";
        case TOOL_NONE:
        default:        return "None";
    }
}


static const char *pen_tool_name(PenTool tool)
{
    switch (tool) {
        case PEN_TOOL_BRUSH_DASH: return "Brush-dash";
        case PEN_TOOL_RECT:       return "Rectangle";
        case PEN_TOOL_RECT_DASH:  return "Rectangle-dash";
        case PEN_TOOL_ARROW:          return "Arrow";
        case PEN_TOOL_ERASER_PARTIAL: return "Eraser-partial";
        case PEN_TOOL_ERASER_FULL:    return "Eraser-full";
        case PEN_TOOL_BRUSH:
        default:                      return "Brush";
    }
}

static void set_rgba(GdkRGBA *c, double r, double g, double b, double a)
{
    if (!c) return;
    c->red = r;
    c->green = g;
    c->blue = b;
    c->alpha = a;
}

static void queue_pen_color_areas(App *app)
{
    if (!app) return;
    if (app->color_area_red)    gtk_widget_queue_draw(app->color_area_red);
    if (app->color_area_green)  gtk_widget_queue_draw(app->color_area_green);
    if (app->color_area_blue)   gtk_widget_queue_draw(app->color_area_blue);
    if (app->color_area_custom) gtk_widget_queue_draw(app->color_area_custom);
}

static void queue_canvas(App *app)
{
    if (app && app->area) gtk_widget_queue_draw(app->area);
}

static void update_ui(App *app)
{
    if (!app) return;

    gboolean multi = (app->sequence.count > 1);

    if (app->btn_prev)   gtk_widget_set_sensitive(app->btn_prev,   multi);
    if (app->btn_next)   gtk_widget_set_sensitive(app->btn_next,   multi);
    if (app->btn_run)    gtk_widget_set_sensitive(app->btn_run,    multi && !app->sequence.playing);
    if (app->btn_pause)  gtk_widget_set_sensitive(app->btn_pause,  multi && app->sequence.playing);
    if (app->btn_slower) gtk_widget_set_sensitive(app->btn_slower, multi && app->sequence.fps > MIN_FPS);
    if (app->btn_faster) gtk_widget_set_sensitive(app->btn_faster, multi && app->sequence.fps < MAX_FPS);

    if (app->btn_reset) {
        gboolean changed = (app->zoom_scale != 1.0 || app->pan_x != 0.0 || app->pan_y != 0.0);
        gtk_widget_set_sensitive(app->btn_reset, changed);
    }
    if (app->btn_undo) gtk_widget_set_sensitive(app->btn_undo, app->undo_stack && app->undo_stack->len > 0);
    if (app->btn_redo) gtk_widget_set_sensitive(app->btn_redo, app->redo_stack && app->redo_stack->len > 0);

    app->updating_ui = TRUE;
    if (app->toggle_pan) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->toggle_pan), app->tool == TOOL_PAN);
    }
    if (app->toggle_zoom) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->toggle_zoom), app->tool == TOOL_ZOOM);
    }
    if (app->toggle_stretch) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->toggle_stretch), app->stretch_mode);
    }
    if (app->toggle_pen) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->toggle_pen), app->tool == TOOL_PEN);
    }
    if (app->pen_revealer) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(app->pen_revealer), app->tool == TOOL_PEN);
    }

    if (app->pen_tool_dropdown) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->pen_tool_dropdown), (guint)app->pen_tool);
    }
    if (app->pen_width_dropdown) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->pen_width_dropdown), width_index_from_value(app->pen_width));
    }
    app->updating_ui = FALSE;

    queue_pen_color_areas(app);

    if (app->status_label) {
        char status[1024];
        const char *path = "N/A";
        const char *frame_label = NULL;
        int iframe = 0;
        int nframe = app->sequence.count;
        char *color_text = gdk_rgba_to_string(&app->pen_color);

        if (app->sequence.count > 0) {
            iframe = app->sequence.current_index + 1;
            SvgFrame *f = &app->sequence.frames[app->sequence.current_index];
            path = f->path ? f->path : "N/A";
            frame_label = f->framelabel;
        }

        guint anno_count = app->annotations ? app->annotations->len : 0;

        if (nframe > 1) {
            g_snprintf(status, sizeof(status),
                       "%s%s%s | Frame %d/%d | FPS %d | %s | Zoom %.0f%% | Tool %s | Pen %s %.2g %s | Anno %u | %s%s",
                       frame_label ? frame_label : "",
                       frame_label ? " | " : "",
                       path,
                       iframe, nframe,
                       app->sequence.fps,
                       app->sequence.playing ? RUNMSG : PAUSEMSG,
                       app->zoom_scale * 100.0,
                       tool_name(app->tool),
                       pen_tool_name(app->pen_tool),
                       app->pen_width,
                       color_text ? color_text : "rgba",
                       anno_count,
                       app->stretch_mode ? "Stretch" : "Aspect",
                       app->stretch_mode ? "" : " locked");
        } else {
            g_snprintf(status, sizeof(status),
                       "%s | Single file | Zoom %.0f%% | Tool %s | Pen %s %.2g %s | Anno %u | %s%s",
                       path,
                       app->zoom_scale * 100.0,
                       tool_name(app->tool),
                       pen_tool_name(app->pen_tool),
                       app->pen_width,
                       color_text ? color_text : "rgba",
                       anno_count,
                       app->stretch_mode ? "Stretch" : "Aspect",
                       app->stretch_mode ? "" : " locked");
        }

        g_free(color_text);
        gtk_label_set_text(GTK_LABEL(app->status_label), status);
    }
}

static void reset_view(App *app)
{
    app->zoom_scale = 1.0;
    app->pan_x = 0.0;
    app->pan_y = 0.0;
    app->dragging = FALSE;
    app->zoom_box_active = FALSE;
    update_ui(app);
    queue_canvas(app);
}

static void set_tool(App *app, ToolMode tool)
{
    if (!app) return;

    if (app->tool == tool) {
        tool = TOOL_NONE;
    }

    app->tool = tool;
    app->dragging = FALSE;
    app->zoom_box_active = FALSE;
    update_ui(app);
    queue_canvas(app);
}

static void step_frame(App *app, int step)
{
    if (app->sequence.count <= 1) return;

    app->sequence.playing = FALSE;
    app->sequence.current_index =
        (app->sequence.current_index + step + app->sequence.count) % app->sequence.count;

    update_ui(app);
    queue_canvas(app);
}

static void set_zoom(App *app, double zoom)
{
    if (zoom < MIN_ZOOM_SCALE) zoom = MIN_ZOOM_SCALE;
    if (zoom > MAX_ZOOM_SCALE) zoom = MAX_ZOOM_SCALE;
    app->zoom_scale = zoom;
    update_ui(app);
    queue_canvas(app);
}

static void on_prev_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    step_frame((App *)user_data, -1);
}

static void on_next_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    step_frame((App *)user_data, 1);
}

static void on_run_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    App *app = user_data;
    if (app->sequence.count > 1) {
        app->sequence.playing = TRUE;
        app->last_frame_ms = now_ms();
    }
    update_ui(app);
}

static void on_pause_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    App *app = user_data;
    app->sequence.playing = FALSE;
    update_ui(app);
}

static void on_slower_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    App *app = user_data;
    if (app->sequence.fps > MIN_FPS) app->sequence.fps--;
    update_ui(app);
}

static void on_faster_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    App *app = user_data;
    if (app->sequence.fps < MAX_FPS) app->sequence.fps++;
    update_ui(app);
}

static void on_reset_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    reset_view((App *)user_data);
}

static void on_undo_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    history_undo((App *)user_data);
}

static void on_redo_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    history_redo((App *)user_data);
}

static void on_pan_toggled(GtkToggleButton *button, gpointer user_data)
{
    App *app = user_data;
    if (app->updating_ui) return;

    if (gtk_toggle_button_get_active(button)) {
        set_tool(app, TOOL_PAN);
    } else if (app->tool == TOOL_PAN) {
        set_tool(app, TOOL_NONE);
    }
}

static void on_zoom_toggled(GtkToggleButton *button, gpointer user_data)
{
    App *app = user_data;
    if (app->updating_ui) return;

    if (gtk_toggle_button_get_active(button)) {
        set_tool(app, TOOL_ZOOM);
    } else if (app->tool == TOOL_ZOOM) {
        set_tool(app, TOOL_NONE);
    }
}

static void on_pen_toggled(GtkToggleButton *button, gpointer user_data)
{
    App *app = user_data;
    if (app->updating_ui) return;

    if (gtk_toggle_button_get_active(button)) {
        set_tool(app, TOOL_PEN);
    } else if (app->tool == TOOL_PEN) {
        set_tool(app, TOOL_NONE);
    }
}

static void on_stretch_toggled(GtkToggleButton *button, gpointer user_data)
{
    App *app = user_data;
    if (app->updating_ui) return;

    app->stretch_mode = gtk_toggle_button_get_active(button);
    update_ui(app);
    queue_canvas(app);
}

static GtkWidget *make_icon_child(const char *svg, const char *fallback)
{
    GdkPixbufLoader *loader;
    GdkPixbuf *pixbuf;
    GdkTexture *texture;
    GtkWidget *image;
    GError *err = NULL;

    if (svg) {
        loader = gdk_pixbuf_loader_new_with_type("svg", &err);
        if (!loader) {
            if (err) g_error_free(err);
            loader = gdk_pixbuf_loader_new();
        }

        if (loader &&
            gdk_pixbuf_loader_write(loader, (const guchar *)svg, strlen(svg), &err) &&
            gdk_pixbuf_loader_close(loader, &err)) {
            pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
            if (pixbuf) {
                g_object_ref(pixbuf);
                texture = gdk_texture_new_for_pixbuf(pixbuf);
                image = gtk_image_new_from_paintable(GDK_PAINTABLE(texture));
                gtk_image_set_pixel_size(GTK_IMAGE(image), 18);
                g_object_unref(texture);
                g_object_unref(pixbuf);
                g_object_unref(loader);
                return image;
            }
        }

        if (err) g_error_free(err);
        if (loader) g_object_unref(loader);
    }

    return gtk_label_new(fallback ? fallback : "");
}

static GtkWidget *make_icon_button(const char *svg,
                                   const char *tooltip,
                                   const char *fallback,
                                   GCallback callback,
                                   App *app)
{
    GtkWidget *button = gtk_button_new();
    gtk_widget_set_focus_on_click(button, FALSE);
    gtk_widget_set_tooltip_text(button, tooltip ? tooltip : fallback);
    gtk_button_set_child(GTK_BUTTON(button), make_icon_child(svg, fallback));
    g_signal_connect(button, "clicked", callback, app);
    return button;
}

static GtkWidget *make_icon_toggle(const char *svg,
                                   const char *tooltip,
                                   const char *fallback,
                                   GCallback callback,
                                   App *app)
{
    GtkWidget *button = gtk_toggle_button_new();
    gtk_widget_set_focus_on_click(button, FALSE);
    gtk_widget_set_tooltip_text(button, tooltip ? tooltip : fallback);
    gtk_button_set_child(GTK_BUTTON(button), make_icon_child(svg, fallback));
    g_signal_connect(button, "toggled", callback, app);
    return button;
}

static void append_sep(GtkWidget *box)
{
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start(sep, 4);
    gtk_widget_set_margin_end(sep, 4);
    gtk_box_append(GTK_BOX(box), sep);
}


typedef struct {
    App *app;
    ColorSlot slot;
} ColorSwatchData;

typedef struct {
    App *app;
    GtkWidget *dialog;
    GtkWidget *chooser;
    GtkWidget *spin_r;
    GtkWidget *spin_g;
    GtkWidget *spin_b;
    GtkWidget *spin_a;
    gboolean updating;
} ColorDialogState;

static void get_slot_color(App *app, ColorSlot slot, GdkRGBA *color)
{
    if (!app || !color) return;

    switch (slot) {
        case COLOR_SLOT_RECENT1:
            *color = app->recent_colors[1];
            break;
        case COLOR_SLOT_RECENT2:
            *color = app->recent_colors[2];
            break;
        case COLOR_SLOT_CUSTOM:
            *color = app->custom_color;
            break;
        case COLOR_SLOT_RECENT0:
        default:
            *color = app->recent_colors[0];
            break;
    }
}

static void draw_rainbow_circle(cairo_t *cr, double cx, double cy, double radius)
{
    const double colors[][3] = {
        {1.0, 0.0, 0.0}, {1.0, 0.6, 0.0}, {1.0, 1.0, 0.0},
        {0.0, 0.7, 0.0}, {0.0, 0.3, 1.0}, {0.5, 0.0, 1.0}
    };
    const int n = 6;
    for (int i = 0; i < n; i++) {
        double a0 = (2.0 * G_PI * i) / n;
        double a1 = (2.0 * G_PI * (i + 1)) / n;
        cairo_move_to(cr, cx, cy);
        cairo_arc(cr, cx, cy, radius, a0, a1);
        cairo_close_path(cr);
        cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
        cairo_fill(cr);
    }
}

static void draw_color_swatch(GtkDrawingArea *area,
                              cairo_t *cr,
                              int width,
                              int height,
                              gpointer user_data)
{
    (void)area;
    ColorSwatchData *data = user_data;
    App *app = data->app;
    ColorSlot slot = data->slot;

    double cx = width * 0.5;
    double cy = height * 0.5;
    double radius = MIN(width, height) * 0.34;

    cairo_save(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    if (slot == COLOR_SLOT_CUSTOM) {
        draw_rainbow_circle(cr, cx, cy, radius);
        cairo_arc(cr, cx, cy, radius * 0.45, 0, 2.0 * G_PI);
        cairo_set_source_rgba(cr,
                              app->custom_color.red,
                              app->custom_color.green,
                              app->custom_color.blue,
                              app->custom_color.alpha);
        cairo_fill(cr);
    } else {
        GdkRGBA color;
        get_slot_color(app, slot, &color);
        cairo_arc(cr, cx, cy, radius, 0, 2.0 * G_PI);
        cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
        cairo_fill_preserve(cr);
    }

    cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);

    if (app->color_slot == slot) {
        cairo_arc(cr, cx, cy, radius + 3.0, 0, 2.0 * G_PI);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.90);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
    }

    cairo_restore(cr);
}

static GtkWidget *make_color_button(App *app, ColorSlot slot, const char *tooltip)
{
    GtkWidget *button = gtk_button_new();
    gtk_widget_set_focus_on_click(button, FALSE);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_add_css_class(button, "flat");

    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 24, 24);

    ColorSwatchData *data = g_new0(ColorSwatchData, 1);
    data->app = app;
    data->slot = slot;
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_color_swatch, data, g_free);

    gtk_button_set_child(GTK_BUTTON(button), area);

    switch (slot) {
        case COLOR_SLOT_RECENT1: app->color_area_green = area;  app->color_btn_green = button; break;
        case COLOR_SLOT_RECENT2: app->color_area_blue = area;   app->color_btn_blue = button; break;
        case COLOR_SLOT_CUSTOM:  app->color_area_custom = area; app->color_btn_custom = button; break;
        case COLOR_SLOT_RECENT0:
        default:                 app->color_area_red = area;    app->color_btn_red = button; break;
    }

    g_object_set_data(G_OBJECT(button), "rsfpy-color-slot", GINT_TO_POINTER((int)slot));
    return button;
}

static void move_recent_color_to_front(App *app, int index)
{
    if (!app || index < 0 || index > 2) return;

    GdkRGBA selected = app->recent_colors[index];
    for (int i = index; i > 0; i--) {
        app->recent_colors[i] = app->recent_colors[i - 1];
    }
    app->recent_colors[0] = selected;
    app->pen_color = app->recent_colors[0];
    app->color_slot = COLOR_SLOT_RECENT0;

    update_ui(app);
}

static void push_recent_color(App *app, const GdkRGBA *color)
{
    if (!app || !color) return;

    app->recent_colors[2] = app->recent_colors[1];
    app->recent_colors[1] = app->recent_colors[0];
    app->recent_colors[0] = *color;
    app->pen_color = app->recent_colors[0];
    app->custom_color = *color;
    app->color_slot = COLOR_SLOT_RECENT0;

    update_ui(app);
}

static void set_pen_color(App *app, ColorSlot slot, const GdkRGBA *color)
{
    if (!app) return;

    switch (slot) {
        case COLOR_SLOT_RECENT1:
            move_recent_color_to_front(app, 1);
            break;
        case COLOR_SLOT_RECENT2:
            move_recent_color_to_front(app, 2);
            break;
        case COLOR_SLOT_CUSTOM:
            if (color) push_recent_color(app, color);
            break;
        case COLOR_SLOT_RECENT0:
        default:
            move_recent_color_to_front(app, 0);
            break;
    }
}

static void on_pen_tool_selected(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    if (!app || app->updating_ui) return;

    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected > PEN_TOOL_ERASER_FULL) selected = PEN_TOOL_BRUSH;
    app->pen_tool = (PenTool)selected;
    update_ui(app);
}

static void on_pen_width_selected(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    if (!app || app->updating_ui) return;

    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    app->pen_width = selected_width_from_index(selected);
    update_ui(app);
}

static void color_dialog_sync_spins(ColorDialogState *st, const GdkRGBA *c)
{
    if (!st || !c) return;
    st->updating = TRUE;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(st->spin_r), c->red);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(st->spin_g), c->green);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(st->spin_b), c->blue);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(st->spin_a), c->alpha);
    st->updating = FALSE;
}

static void color_dialog_sync_chooser(ColorDialogState *st)
{
    if (!st || st->updating) return;

    GdkRGBA c;
    c.red = gtk_spin_button_get_value(GTK_SPIN_BUTTON(st->spin_r));
    c.green = gtk_spin_button_get_value(GTK_SPIN_BUTTON(st->spin_g));
    c.blue = gtk_spin_button_get_value(GTK_SPIN_BUTTON(st->spin_b));
    c.alpha = gtk_spin_button_get_value(GTK_SPIN_BUTTON(st->spin_a));

    st->updating = TRUE;
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(st->chooser), &c);
    st->updating = FALSE;
}

static void on_color_chooser_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    ColorDialogState *st = user_data;
    if (!st || st->updating) return;

    GdkRGBA c;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(object), &c);
    color_dialog_sync_spins(st, &c);
}

static void on_color_spin_changed(GtkSpinButton *spin, gpointer user_data)
{
    (void)spin;
    color_dialog_sync_chooser((ColorDialogState *)user_data);
}

static void on_color_dialog_response(GtkDialog *dialog, int response_id, gpointer user_data)
{
    ColorDialogState *st = user_data;
    if (st && response_id == GTK_RESPONSE_OK) {
        GdkRGBA c;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(st->chooser), &c);
        set_pen_color(st->app, COLOR_SLOT_CUSTOM, &c);
    }

    if (st) {
        g_signal_handlers_disconnect_by_data(st->chooser, st);
        g_signal_handlers_disconnect_by_data(st->spin_r, st);
        g_signal_handlers_disconnect_by_data(st->spin_g, st);
        g_signal_handlers_disconnect_by_data(st->spin_b, st);
        g_signal_handlers_disconnect_by_data(st->spin_a, st);
        g_free(st);
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
}

static GtkWidget *make_rgba_spin(double value)
{
    GtkWidget *spin = gtk_spin_button_new_with_range(0.0, 1.0, 0.01);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_widget_set_size_request(spin, 72, -1);
    return spin;
}

static void open_custom_color_dialog(App *app)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Choose RGBA color",
                                                    GTK_WINDOW(app->window),
                                                    GTK_DIALOG_MODAL,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_OK", GTK_RESPONSE_OK,
                                                    NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(content), 8);
    gtk_widget_set_margin_top(content, 8);
    gtk_widget_set_margin_bottom(content, 8);
    gtk_widget_set_margin_start(content, 8);
    gtk_widget_set_margin_end(content, 8);

    ColorDialogState *st = g_new0(ColorDialogState, 1);
    st->app = app;
    st->dialog = dialog;

    st->chooser = gtk_color_chooser_widget_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(st->chooser), TRUE);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(st->chooser), &app->custom_color);
    gtk_box_append(GTK_BOX(content), st->chooser);

    GtkWidget *rgba_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(rgba_box, 4);
    gtk_box_append(GTK_BOX(content), rgba_box);

    GtkWidget *label = gtk_label_new("RGBA:");
    gtk_box_append(GTK_BOX(rgba_box), label);

    st->spin_r = make_rgba_spin(app->custom_color.red);
    st->spin_g = make_rgba_spin(app->custom_color.green);
    st->spin_b = make_rgba_spin(app->custom_color.blue);
    st->spin_a = make_rgba_spin(app->custom_color.alpha);

    gtk_box_append(GTK_BOX(rgba_box), gtk_label_new("R"));
    gtk_box_append(GTK_BOX(rgba_box), st->spin_r);
    gtk_box_append(GTK_BOX(rgba_box), gtk_label_new("G"));
    gtk_box_append(GTK_BOX(rgba_box), st->spin_g);
    gtk_box_append(GTK_BOX(rgba_box), gtk_label_new("B"));
    gtk_box_append(GTK_BOX(rgba_box), st->spin_b);
    gtk_box_append(GTK_BOX(rgba_box), gtk_label_new("A"));
    gtk_box_append(GTK_BOX(rgba_box), st->spin_a);

    g_signal_connect(st->chooser, "notify::rgba", G_CALLBACK(on_color_chooser_changed), st);
    g_signal_connect(st->spin_r, "value-changed", G_CALLBACK(on_color_spin_changed), st);
    g_signal_connect(st->spin_g, "value-changed", G_CALLBACK(on_color_spin_changed), st);
    g_signal_connect(st->spin_b, "value-changed", G_CALLBACK(on_color_spin_changed), st);
    g_signal_connect(st->spin_a, "value-changed", G_CALLBACK(on_color_spin_changed), st);
    g_signal_connect(dialog, "response", G_CALLBACK(on_color_dialog_response), st);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_color_button_clicked(GtkButton *button, gpointer user_data)
{
    App *app = user_data;
    ColorSlot slot = (ColorSlot)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "rsfpy-color-slot"));

    if (slot == COLOR_SLOT_CUSTOM) {
        open_custom_color_dialog(app);
        return;
    }

    GdkRGBA color;
    get_slot_color(app, slot, &color);
    set_pen_color(app, slot, &color);
}

static void on_pen_clear_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    App *app = user_data;
    if (app->annotations && app->annotations->len > 0) history_save_before_edit(app);
    annotation_layer_clear(app);
    update_ui(app);
    queue_canvas(app);
}

static GtkWidget *create_main_toolbar(App *app)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_top(bar, 4);
    gtk_widget_set_margin_bottom(bar, 4);
    gtk_widget_set_margin_start(bar, 6);
    gtk_widget_set_margin_end(bar, 6);

    app->btn_prev = make_icon_button(ICON_PREV, "Previous frame", "Prev",
                                     G_CALLBACK(on_prev_clicked), app);
    app->btn_next = make_icon_button(ICON_NEXT, "Next frame", "Next",
                                     G_CALLBACK(on_next_clicked), app);
    app->btn_run = make_icon_button(ICON_PLAY, "Play sequence", "Run",
                                    G_CALLBACK(on_run_clicked), app);
    app->btn_pause = make_icon_button(ICON_PAUSE, "Pause sequence", "Pause",
                                      G_CALLBACK(on_pause_clicked), app);
    app->btn_slower = make_icon_button(ICON_SLOWER, "Slower playback", "Slower",
                                       G_CALLBACK(on_slower_clicked), app);
    app->btn_faster = make_icon_button(ICON_FASTER, "Faster playback", "Faster",
                                       G_CALLBACK(on_faster_clicked), app);
    app->toggle_pan = make_icon_toggle(ICON_PAN, "Pan tool", "Pan",
                                       G_CALLBACK(on_pan_toggled), app);
    app->toggle_zoom = make_icon_toggle(ICON_ZOOM, "Zoom box tool", "Zoom",
                                        G_CALLBACK(on_zoom_toggled), app);
    app->toggle_pen = make_icon_toggle(ICON_PEN, "Annotation tools", "Pen",
                                       G_CALLBACK(on_pen_toggled), app);
    app->btn_reset = make_icon_button(ICON_RESET, "Reset view", "Reset",
                                      G_CALLBACK(on_reset_clicked), app);
    app->toggle_stretch = make_icon_toggle(ICON_STRETCH, "Stretch to fit", "Stretch",
                                           G_CALLBACK(on_stretch_toggled), app);

    gtk_box_append(GTK_BOX(bar), app->btn_prev);
    gtk_box_append(GTK_BOX(bar), app->btn_next);
    append_sep(bar);
    gtk_box_append(GTK_BOX(bar), app->btn_run);
    gtk_box_append(GTK_BOX(bar), app->btn_pause);
    gtk_box_append(GTK_BOX(bar), app->btn_slower);
    gtk_box_append(GTK_BOX(bar), app->btn_faster);
    append_sep(bar);
    gtk_box_append(GTK_BOX(bar), app->toggle_pan);
    gtk_box_append(GTK_BOX(bar), app->toggle_zoom);
    gtk_box_append(GTK_BOX(bar), app->toggle_pen);
    append_sep(bar);
    gtk_box_append(GTK_BOX(bar), app->btn_reset);
    gtk_box_append(GTK_BOX(bar), app->toggle_stretch);

    return bar;
}

static GtkWidget *create_pen_toolbar(App *app)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_bottom(bar, 4);
    gtk_widget_set_margin_start(bar, 6);
    gtk_widget_set_margin_end(bar, 6);

    GtkWidget *title = gtk_label_new("Pen:");
    gtk_widget_add_css_class(title, "dim-label");
    gtk_box_append(GTK_BOX(bar), title);

    GtkWidget *tool_label = gtk_label_new("Tool");
    gtk_box_append(GTK_BOX(bar), tool_label);

    static const char * const tool_names[] = {
        "Brush",
        "Brush-dash",
        "Rectangle",
        "Rectangle-dash",
        "Arrow",
        "Eraser-partial",
        "Eraser-full",
        NULL
    };
    app->pen_tool_dropdown = gtk_drop_down_new_from_strings(tool_names);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->pen_tool_dropdown), app->pen_tool);
    gtk_widget_set_tooltip_text(app->pen_tool_dropdown, "Select annotation tool");
    g_signal_connect(app->pen_tool_dropdown, "notify::selected", G_CALLBACK(on_pen_tool_selected), app);
    gtk_box_append(GTK_BOX(bar), app->pen_tool_dropdown);

    GtkWidget *width_label = gtk_label_new("Width");
    gtk_widget_set_margin_start(width_label, 6);
    gtk_box_append(GTK_BOX(bar), width_label);

    static const char * const width_names[] = {
        "0.1", "0.25", "0.5", "1.0", "1.5", "2.0", "2.5",
        "3.0", "3.5", "4.0", "4.5", "5.0", NULL
    };
    app->pen_width_dropdown = gtk_drop_down_new_from_strings(width_names);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->pen_width_dropdown), width_index_from_value(app->pen_width));
    gtk_widget_set_tooltip_text(app->pen_width_dropdown, "Select annotation stroke width");
    g_signal_connect(app->pen_width_dropdown, "notify::selected", G_CALLBACK(on_pen_width_selected), app);
    gtk_box_append(GTK_BOX(bar), app->pen_width_dropdown);

    GtkWidget *color_label = gtk_label_new("Color");
    gtk_widget_set_margin_start(color_label, 6);
    gtk_box_append(GTK_BOX(bar), color_label);

    GtkWidget *red = make_color_button(app, COLOR_SLOT_RECENT0, "Current color / most recent color");
    GtkWidget *green = make_color_button(app, COLOR_SLOT_RECENT1, "Recent color 2; click to promote to current");
    GtkWidget *blue = make_color_button(app, COLOR_SLOT_RECENT2, "Recent color 3; click to promote to current");
    GtkWidget *custom = make_color_button(app, COLOR_SLOT_CUSTOM, "Choose custom RGBA color and push it to the front");

    g_signal_connect(red, "clicked", G_CALLBACK(on_color_button_clicked), app);
    g_signal_connect(green, "clicked", G_CALLBACK(on_color_button_clicked), app);
    g_signal_connect(blue, "clicked", G_CALLBACK(on_color_button_clicked), app);
    g_signal_connect(custom, "clicked", G_CALLBACK(on_color_button_clicked), app);

    gtk_box_append(GTK_BOX(bar), red);
    gtk_box_append(GTK_BOX(bar), green);
    gtk_box_append(GTK_BOX(bar), blue);
    gtk_box_append(GTK_BOX(bar), custom);

    app->pen_clear_button = make_icon_button(ICON_CLEAR, "Clear all annotations", "Clear",
                                             G_CALLBACK(on_pen_clear_clicked), app);
    gtk_widget_set_margin_start(app->pen_clear_button, 6);
    gtk_box_append(GTK_BOX(bar), app->pen_clear_button);

    append_sep(bar);
    app->btn_undo = make_icon_button(ICON_UNDO, "Undo annotation edit (Ctrl+Z)", "Undo",
                                     G_CALLBACK(on_undo_clicked), app);
    app->btn_redo = make_icon_button(ICON_REDO,
                                     "Redo annotation edit (Ctrl+Y / Ctrl+Shift+Z)",
                                     "Redo", G_CALLBACK(on_redo_clicked), app);
    gtk_box_append(GTK_BOX(bar), app->btn_undo);
    gtk_box_append(GTK_BOX(bar), app->btn_redo);

    GtkWidget *hint = gtk_label_new("Annotations are stored in SVG document coordinates");
    gtk_widget_set_hexpand(hint, TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_box_append(GTK_BOX(bar), hint);

    return bar;
}


static void set_annotation_dash(cairo_t *cr, const Annotation *a)
{
    if (!a || !a->style.dashed) {
        cairo_set_dash(cr, NULL, 0, 0.0);
        return;
    }

    double w = a->style.width_doc;
    if (w <= 0.0) w = 1.0;
    double dashes[] = {6.0 * w, 4.0 * w};
    cairo_set_dash(cr, dashes, 2, 0.0);
}

static void draw_arrow_head(cairo_t *cr, const Annotation *a)
{
    double dx = a->x1 - a->x0;
    double dy = a->y1 - a->y0;
    double len = local_sqrt(dx * dx + dy * dy);
    if (len <= 1e-12) return;

    double ux = dx / len;
    double uy = dy / len;
    double px = -uy;
    double py = ux;
    double size = a->style.width_doc * 5.0;
    if (size < 6.0 * a->style.width_doc) size = 6.0 * a->style.width_doc;

    double bx = a->x1 - ux * size;
    double by = a->y1 - uy * size;
    double lx = bx + px * size * 0.45;
    double ly = by + py * size * 0.45;
    double rx = bx - px * size * 0.45;
    double ry = by - py * size * 0.45;

    cairo_move_to(cr, a->x1, a->y1);
    cairo_line_to(cr, lx, ly);
    cairo_move_to(cr, a->x1, a->y1);
    cairo_line_to(cr, rx, ry);
    cairo_stroke(cr);
}

static void draw_one_annotation(cairo_t *cr, const Annotation *a)
{
    if (!a || !annotation_is_valid(a)) return;

    cairo_save(cr);
    cairo_set_source_rgba(cr,
                          a->style.color.red,
                          a->style.color.green,
                          a->style.color.blue,
                          a->style.color.alpha);
    cairo_set_line_width(cr, a->style.width_doc > 0.0 ? a->style.width_doc : 1.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    set_annotation_dash(cr, a);

    if (a->type == ANNO_BRUSH || a->type == ANNO_BRUSH_DASH) {
        if (a->points && a->points->len >= 2) {
            DocPoint *pts = (DocPoint *)a->points->data;
            cairo_move_to(cr, pts[0].x, pts[0].y);
            for (guint i = 1; i < a->points->len; i++) {
                cairo_line_to(cr, pts[i].x, pts[i].y);
            }
            cairo_stroke(cr);
        }
    } else if (a->type == ANNO_RECT || a->type == ANNO_RECT_DASH) {
        double x = a->x0 < a->x1 ? a->x0 : a->x1;
        double y = a->y0 < a->y1 ? a->y0 : a->y1;
        double w = fabs(a->x1 - a->x0);
        double h = fabs(a->y1 - a->y0);
        cairo_rectangle(cr, x, y, w, h);
        cairo_stroke(cr);
    } else if (a->type == ANNO_ARROW) {
        cairo_move_to(cr, a->x0, a->y0);
        cairo_line_to(cr, a->x1, a->y1);
        cairo_stroke(cr);
        set_annotation_dash(cr, NULL);
        draw_arrow_head(cr, a);
    }

    cairo_restore(cr);
}

static void draw_annotations(App *app, cairo_t *cr)
{
    if (!app || !app->annotations || app->annotations->len == 0) return;

    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, app->canvas_w, app->canvas_h);
    cairo_clip(cr);
    apply_view_transform(app, cr);

    for (guint i = 0; i < app->annotations->len; i++) {
        Annotation *a = g_ptr_array_index(app->annotations, i);
        draw_one_annotation(cr, a);
    }

    cairo_restore(cr);
}

static double annotation_hit_distance(const Annotation *a, double x, double y)
{
    if (!a || !annotation_is_valid(a)) return G_MAXDOUBLE;

    if (a->type == ANNO_BRUSH || a->type == ANNO_BRUSH_DASH) {
        double best = G_MAXDOUBLE;
        DocPoint *pts = (DocPoint *)a->points->data;
        for (guint i = 1; i < a->points->len; i++) {
            double d = point_segment_distance(x, y, pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y);
            if (d < best) best = d;
        }
        return best;
    }

    if (a->type == ANNO_ARROW) {
        return point_segment_distance(x, y, a->x0, a->y0, a->x1, a->y1);
    }

    if (a->type == ANNO_RECT || a->type == ANNO_RECT_DASH) {
        double x0 = a->x0 < a->x1 ? a->x0 : a->x1;
        double x1 = a->x0 < a->x1 ? a->x1 : a->x0;
        double y0 = a->y0 < a->y1 ? a->y0 : a->y1;
        double y1 = a->y0 < a->y1 ? a->y1 : a->y0;
        double d1 = point_segment_distance(x, y, x0, y0, x1, y0);
        double d2 = point_segment_distance(x, y, x1, y0, x1, y1);
        double d3 = point_segment_distance(x, y, x1, y1, x0, y1);
        double d4 = point_segment_distance(x, y, x0, y1, x0, y0);
        return min4(d1, d2, d3, d4);
    }

    return G_MAXDOUBLE;
}

static gboolean erase_full_at_doc(App *app, double x, double y)
{
    if (!app || !app->annotations) return FALSE;
    double radius = current_doc_eraser_radius(app);

    for (gint i = (gint)app->annotations->len - 1; i >= 0; i--) {
        Annotation *a = g_ptr_array_index(app->annotations, (guint)i);
        double hit = annotation_hit_distance(a, x, y);
        double threshold = radius + a->style.width_doc * 0.5;
        if (hit <= threshold) {
            g_ptr_array_remove_index(app->annotations, (guint)i);
            return TRUE;
        }
    }
    return FALSE;
}

static void append_freehand_run(App *app, const Annotation *src, DocPoint *pts, guint start, guint end)
{
    if (!app || !src || !pts || end <= start || end - start < 2) return;
    Annotation *na = annotation_new(src->type, &src->style);
    for (guint i = start; i < end; i++) annotation_add_point(na, pts[i].x, pts[i].y);
    g_ptr_array_add(app->annotations, na);
}

static gboolean erase_partial_at_doc(App *app, double x, double y)
{
    if (!app || !app->annotations) return FALSE;
    double radius = current_doc_eraser_radius(app);

    for (gint i = (gint)app->annotations->len - 1; i >= 0; i--) {
        Annotation *a = g_ptr_array_index(app->annotations, (guint)i);
        double threshold = radius + a->style.width_doc * 0.5;
        if (annotation_hit_distance(a, x, y) > threshold) continue;

        if (!annotation_is_freehand(a)) {
            g_ptr_array_remove_index(app->annotations, (guint)i);
            return TRUE;
        }

        DocPoint *pts = (DocPoint *)a->points->data;
        guint n = a->points->len;
        guint run_start = 0;
        gboolean in_run = FALSE;
        gboolean changed = FALSE;

        /* Remove the original first; new surviving freehand segments are appended. */
        Annotation *old = g_ptr_array_steal_index(app->annotations, (guint)i);

        for (guint j = 0; j < n; j++) {
            gboolean keep = local_sqrt(dist2(pts[j].x, pts[j].y, x, y)) > threshold;
            if (keep && !in_run) {
                in_run = TRUE;
                run_start = j;
            } else if (!keep && in_run) {
                append_freehand_run(app, old, pts, run_start, j);
                in_run = FALSE;
                changed = TRUE;
            } else if (!keep) {
                changed = TRUE;
            }
        }
        if (in_run) append_freehand_run(app, old, pts, run_start, n);

        annotation_free(old);
        return changed;
    }
    return FALSE;
}

static AnnotationStyle current_annotation_style(App *app)
{
    AnnotationStyle st;
    memset(&st, 0, sizeof(st));
    st.color = app->pen_color;
    st.width_doc = current_doc_width_for_screen_width(app, app->pen_width);
    if (st.width_doc <= 0.0) st.width_doc = app->pen_width;
    st.dashed = (app->pen_tool == PEN_TOOL_BRUSH_DASH ||
                 app->pen_tool == PEN_TOOL_RECT_DASH);
    return st;
}

static void start_annotation_at_doc(App *app, double x, double y)
{
    if (!app || !app->annotations) return;

    AnnotationStyle st = current_annotation_style(app);
    Annotation *a = NULL;

    switch (app->pen_tool) {
        case PEN_TOOL_BRUSH:
            a = annotation_new(ANNO_BRUSH, &st);
            annotation_add_point(a, x, y);
            annotation_add_point(a, x, y);
            break;
        case PEN_TOOL_BRUSH_DASH:
            a = annotation_new(ANNO_BRUSH_DASH, &st);
            annotation_add_point(a, x, y);
            annotation_add_point(a, x, y);
            break;
        case PEN_TOOL_RECT:
            a = annotation_new(ANNO_RECT, &st);
            a->x0 = a->x1 = x;
            a->y0 = a->y1 = y;
            break;
        case PEN_TOOL_RECT_DASH:
            a = annotation_new(ANNO_RECT_DASH, &st);
            a->x0 = a->x1 = x;
            a->y0 = a->y1 = y;
            break;
        case PEN_TOOL_ARROW:
            a = annotation_new(ANNO_ARROW, &st);
            a->x0 = a->x1 = x;
            a->y0 = a->y1 = y;
            break;
        case PEN_TOOL_ERASER_PARTIAL:
            erase_partial_at_doc(app, x, y);
            return;
        case PEN_TOOL_ERASER_FULL:
            erase_full_at_doc(app, x, y);
            return;
    }

    if (a) {
        app->active_annotation = a;
        g_ptr_array_add(app->annotations, a);
    }
}

static void update_active_annotation_at_doc(App *app, double x, double y)
{
    if (!app) return;

    if (app->pen_tool == PEN_TOOL_ERASER_PARTIAL) {
        erase_partial_at_doc(app, x, y);
        return;
    }
    if (app->pen_tool == PEN_TOOL_ERASER_FULL) {
        erase_full_at_doc(app, x, y);
        return;
    }

    Annotation *a = app->active_annotation;
    if (!a) return;

    if (a->type == ANNO_BRUSH || a->type == ANNO_BRUSH_DASH) {
        DocPoint *pts = (DocPoint *)a->points->data;
        DocPoint *last = &pts[a->points->len - 1];
        double min_step = current_doc_width_for_screen_width(app, 1.0);
        if (min_step <= 0.0) min_step = 0.0;
        if (local_sqrt(dist2(last->x, last->y, x, y)) >= min_step) {
            annotation_add_point(a, x, y);
        } else {
            last->x = x;
            last->y = y;
        }
    } else {
        a->x1 = x;
        a->y1 = y;
    }
}

static void finish_active_annotation(App *app)
{
    if (!app || !app->active_annotation) return;

    Annotation *a = app->active_annotation;
    app->active_annotation = NULL;

    if (!annotation_is_valid(a)) {
        g_ptr_array_remove(app->annotations, a);
    }
}

static void draw_zoom_box_overlay(App *app, cairo_t *cr)
{
    if (!app || !app->zoom_box_active) return;

    double x0 = app->zoom_box_x0;
    double y0 = app->zoom_box_y0;
    double x1 = app->zoom_box_x1;
    double y1 = app->zoom_box_y1;
    double x = x0 < x1 ? x0 : x1;
    double y = y0 < y1 ? y0 : y1;
    double w = fabs(x1 - x0);
    double h = fabs(y1 - y0);
    if (w < 1.0 || h < 1.0) return;

    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_source_rgba(cr, 0.1, 0.35, 1.0, 0.16);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.1, 0.35, 1.0, 0.90);
    cairo_set_line_width(cr, 1.5);
    double dashes[] = {6.0, 4.0};
    cairo_set_dash(cr, dashes, 2, 0.0);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static double current_screen_eraser_radius(App *app)
{
    double r = app ? app->pen_width * 2.0 : 6.0;
    if (r < 6.0) r = 6.0;
    return r;
}

static void draw_eraser_cursor_overlay(App *app, cairo_t *cr)
{
    if (!app || !cr) return;
    if (!app->pointer_in_canvas) return;
    if (app->tool != TOOL_PEN || !pen_tool_is_partial_eraser(app)) return;

    double r = current_screen_eraser_radius(app);

    cairo_save(cr);
    cairo_arc(cr, app->pointer_x, app->pointer_y, r, 0.0, 2.0 * G_PI);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.10);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.85);
    cairo_set_line_width(cr, 1.2);
    cairo_set_dash(cr, NULL, 0, 0.0);
    cairo_stroke(cr);

    cairo_move_to(cr, app->pointer_x - r * 0.45, app->pointer_y);
    cairo_line_to(cr, app->pointer_x + r * 0.45, app->pointer_y);
    cairo_move_to(cr, app->pointer_x, app->pointer_y - r * 0.45);
    cairo_line_to(cr, app->pointer_x, app->pointer_y + r * 0.45);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void apply_zoom_box(App *app, double sx0, double sy0, double sx1, double sy1)
{
    if (!app) return;

    double rx0 = sx0 < sx1 ? sx0 : sx1;
    double rx1 = sx0 < sx1 ? sx1 : sx0;
    double ry0 = sy0 < sy1 ? sy0 : sy1;
    double ry1 = sy0 < sy1 ? sy1 : sy0;
    double rw = rx1 - rx0;
    double rh = ry1 - ry0;

    if (rw < 6.0 || rh < 6.0) return;

    SvgFrame *f = current_frame(app);
    if (!f || f->width <= 0.0 || f->height <= 0.0) return;

    double dx0, dy0, dx1, dy1;
    if (!screen_to_doc(app, rx0, ry0, &dx0, &dy0)) return;
    if (!screen_to_doc(app, rx1, ry1, &dx1, &dy1)) return;

    double doc_x0 = dx0 < dx1 ? dx0 : dx1;
    double doc_x1 = dx0 < dx1 ? dx1 : dx0;
    double doc_y0 = dy0 < dy1 ? dy0 : dy1;
    double doc_y1 = dy0 < dy1 ? dy1 : dy0;
    double doc_w = doc_x1 - doc_x0;
    double doc_h = doc_y1 - doc_y0;
    if (doc_w <= 1e-12 || doc_h <= 1e-12) return;

    double doc_cx = 0.5 * (doc_x0 + doc_x1);
    double doc_cy = 0.5 * (doc_y0 + doc_y1);
    double new_zoom = 1.0;
    double new_sx = 1.0;
    double new_sy = 1.0;
    double dst_w = 0.0;
    double dst_h = 0.0;

    if (app->stretch_mode) {
        double zx = f->width / doc_w;
        double zy = f->height / doc_h;
        new_zoom = zx < zy ? zx : zy;
        if (new_zoom < MIN_ZOOM_SCALE) new_zoom = MIN_ZOOM_SCALE;
        if (new_zoom > MAX_ZOOM_SCALE) new_zoom = MAX_ZOOM_SCALE;
        new_sx = ((double)app->canvas_w / f->width) * new_zoom;
        new_sy = ((double)app->canvas_h / f->height) * new_zoom;
        dst_w = app->canvas_w * new_zoom;
        dst_h = app->canvas_h * new_zoom;
    } else {
        double base_sx = (double)app->canvas_w / f->width;
        double base_sy = (double)app->canvas_h / f->height;
        double base = base_sx < base_sy ? base_sx : base_sy;
        double target_sx = (double)app->canvas_w / doc_w;
        double target_sy = (double)app->canvas_h / doc_h;
        double target = target_sx < target_sy ? target_sx : target_sy;
        new_zoom = target / base;
        if (new_zoom < MIN_ZOOM_SCALE) new_zoom = MIN_ZOOM_SCALE;
        if (new_zoom > MAX_ZOOM_SCALE) new_zoom = MAX_ZOOM_SCALE;
        new_sx = base * new_zoom;
        new_sy = new_sx;
        dst_w = f->width * new_sx;
        dst_h = f->height * new_sy;
    }

    app->zoom_scale = new_zoom;
    app->pan_x = app->canvas_w * 0.5 - doc_cx * new_sx - (app->canvas_w - dst_w) * 0.5;
    app->pan_y = app->canvas_h * 0.5 - doc_cy * new_sy - (app->canvas_h - dst_h) * 0.5;
}

static void draw_canvas_background(App *app, cairo_t *cr)
{
    cairo_save(cr);
    cairo_set_source_rgba_string(cr, BACKGROUND_COLOR);
    cairo_rectangle(cr, 0, 0, app->canvas_w, app->canvas_h);
    cairo_fill(cr);
    cairo_restore(cr);
}

static void draw_canvas(App *app, cairo_t *cr)
{
    draw_canvas_background(app, cr);

    /* Clean GTK architecture: DrawingArea owns only the canvas.  Native GTK
     * widgets own toolbar/statusbar.  Pass zero toolbar/hintbar heights so the
     * sequence renderer can use the full canvas. */
    svg_sequence_render_frame(&app->sequence, cr,
                              app->canvas_w, app->canvas_h,
                              app->pan_x, app->pan_y,
                              app->zoom_scale,
                              app->stretch_mode,
                              0, 0);

    draw_annotations(app, cr);
    draw_zoom_box_overlay(app, cr);
    draw_eraser_cursor_overlay(app, cr);
}

static void on_draw(GtkDrawingArea *area,
                    cairo_t *cr,
                    int width,
                    int height,
                    gpointer user_data)
{
    (void)area;
    App *app = user_data;

    app->canvas_w = width > 1 ? width : 1;
    app->canvas_h = height > 1 ? height : 1;

    draw_canvas(app, cr);
}

static gboolean pen_tool_is_eraser(App *app)
{
    return app && (app->pen_tool == PEN_TOOL_ERASER_PARTIAL ||
                   app->pen_tool == PEN_TOOL_ERASER_FULL);
}

static gboolean pen_tool_is_partial_eraser(App *app)
{
    return app && app->pen_tool == PEN_TOOL_ERASER_PARTIAL;
}

static void on_drag_begin(GtkGestureDrag *gesture,
                          double start_x,
                          double start_y,
                          gpointer user_data)
{
    (void)gesture;
    App *app = user_data;

    app->dragging = TRUE;
    app->drag_start_x = start_x;
    app->drag_start_y = start_y;
    app->pointer_in_canvas = TRUE;
    app->pointer_x = start_x;
    app->pointer_y = start_y;

    if (app->tool == TOOL_PAN) {
        app->drag_origin_pan_x = app->pan_x;
        app->drag_origin_pan_y = app->pan_y;
        return;
    }

    if (app->tool == TOOL_ZOOM) {
        app->zoom_box_active = TRUE;
        app->zoom_box_x0 = app->zoom_box_x1 = start_x;
        app->zoom_box_y0 = app->zoom_box_y1 = start_y;
        queue_canvas(app);
        return;
    }

    if (app->tool == TOOL_PEN) {
        double dx, dy;
        if (!screen_to_doc(app, start_x, start_y, &dx, &dy)) return;
        history_save_before_edit(app);
        start_annotation_at_doc(app, dx, dy);
        update_ui(app);
        queue_canvas(app);
    }
}

static void on_drag_update(GtkGestureDrag *gesture,
                           double offset_x,
                           double offset_y,
                           gpointer user_data)
{
    (void)gesture;
    App *app = user_data;
    if (!app->dragging) return;

    double x = app->drag_start_x + offset_x;
    double y = app->drag_start_y + offset_y;
    app->pointer_in_canvas = TRUE;
    app->pointer_x = x;
    app->pointer_y = y;

    if (app->tool == TOOL_PAN) {
        app->pan_x = app->drag_origin_pan_x + offset_x;
        app->pan_y = app->drag_origin_pan_y + offset_y;
        update_ui(app);
        queue_canvas(app);
        return;
    }

    if (app->tool == TOOL_ZOOM) {
        app->zoom_box_x1 = x;
        app->zoom_box_y1 = y;
        queue_canvas(app);
        return;
    }

    if (app->tool == TOOL_PEN) {
        double dx, dy;
        if (screen_to_doc(app, x, y, &dx, &dy)) {
            update_active_annotation_at_doc(app, dx, dy);
            update_ui(app);
            queue_canvas(app);
        }
    }
}

static void on_drag_end(GtkGestureDrag *gesture,
                        double offset_x,
                        double offset_y,
                        gpointer user_data)
{
    (void)gesture;
    App *app = user_data;

    double x = app->drag_start_x + offset_x;
    double y = app->drag_start_y + offset_y;
    app->pointer_in_canvas = TRUE;
    app->pointer_x = x;
    app->pointer_y = y;

    if (app->tool == TOOL_ZOOM) {
        double x0 = app->zoom_box_x0;
        double y0 = app->zoom_box_y0;
        double x1 = x;
        double y1 = y;
        app->zoom_box_active = FALSE;
        apply_zoom_box(app, x0, y0, x1, y1);
        update_ui(app);
        queue_canvas(app);
    } else if (app->tool == TOOL_PEN) {
        double dx, dy;
        if (screen_to_doc(app, x, y, &dx, &dy)) {
            update_active_annotation_at_doc(app, dx, dy);
        }
        finish_active_annotation(app);
        update_ui(app);
        queue_canvas(app);
    }

    app->dragging = FALSE;
}

static void on_motion_enter(GtkEventControllerMotion *controller,
                            double x,
                            double y,
                            gpointer user_data)
{
    (void)controller;
    App *app = user_data;
    app->pointer_in_canvas = TRUE;
    app->pointer_x = x;
    app->pointer_y = y;
    queue_canvas(app);
}

static void on_motion_move(GtkEventControllerMotion *controller,
                           double x,
                           double y,
                           gpointer user_data)
{
    (void)controller;
    App *app = user_data;
    app->pointer_in_canvas = TRUE;
    app->pointer_x = x;
    app->pointer_y = y;
    if (app->tool == TOOL_PEN && pen_tool_is_partial_eraser(app)) {
        queue_canvas(app);
    }
}

static void on_motion_leave(GtkEventControllerMotion *controller,
                            gpointer user_data)
{
    (void)controller;
    App *app = user_data;
    app->pointer_in_canvas = FALSE;
    queue_canvas(app);
}

static gboolean on_scroll(GtkEventControllerScroll *controller,
                          double dx,
                          double dy,
                          gpointer user_data)
{
    (void)controller;
    (void)dx;
    (void)dy;
    (void)user_data;
    return FALSE;
}

static gboolean on_key_pressed(GtkEventControllerKey *controller,
                               guint keyval,
                               guint keycode,
                               GdkModifierType state,
                               gpointer user_data)
{
    (void)controller;
    (void)keycode;
    (void)state;
    App *app = user_data;

    if (state & GDK_CONTROL_MASK) {
        if (keyval == GDK_KEY_z || keyval == GDK_KEY_Z) {
            if (state & GDK_SHIFT_MASK) history_redo(app);
            else history_undo(app);
            return TRUE;
        }
        if (keyval == GDK_KEY_y || keyval == GDK_KEY_Y) {
            history_redo(app);
            return TRUE;
        }
    }

    switch (keyval) {
        case GDK_KEY_q:
        case GDK_KEY_Q:
        case GDK_KEY_Escape:
            gtk_window_close(GTK_WINDOW(app->window));
            return TRUE;

        case GDK_KEY_Left:
        case GDK_KEY_m:
        case GDK_KEY_M:
            step_frame(app, -1);
            return TRUE;

        case GDK_KEY_Right:
        case GDK_KEY_n:
        case GDK_KEY_N:
            step_frame(app, 1);
            return TRUE;

        case GDK_KEY_space:
            if (app->sequence.count > 1) {
                app->sequence.playing = !app->sequence.playing;
                app->last_frame_ms = now_ms();
                update_ui(app);
            }
            return TRUE;

        case GDK_KEY_p:
        case GDK_KEY_P:
            app->sequence.playing = FALSE;
            update_ui(app);
            return TRUE;

        case GDK_KEY_r:
        case GDK_KEY_R:
            if (app->sequence.count > 1) {
                app->sequence.playing = TRUE;
                app->last_frame_ms = now_ms();
                update_ui(app);
            }
            return TRUE;

        case GDK_KEY_0:
        case GDK_KEY_Home:
        case GDK_KEY_h:
        case GDK_KEY_H:
            reset_view(app);
            return TRUE;

        case GDK_KEY_1:
            set_tool(app, TOOL_PAN);
            return TRUE;

        case GDK_KEY_2:
            set_tool(app, TOOL_ZOOM);
            return TRUE;

        case GDK_KEY_3:
            set_tool(app, TOOL_PEN);
            return TRUE;

        default:
            break;
    }

    return FALSE;
}

static gboolean on_tick(gpointer user_data)
{
    App *app = user_data;
    gint64 t = now_ms();

    if (app->sequence.playing && app->sequence.count > 1 && app->sequence.fps > 0) {
        int interval_ms = 1000 / app->sequence.fps;
        if (interval_ms < 1) interval_ms = 1;

        if (t - app->last_frame_ms >= interval_ms) {
            svg_sequence_advance(&app->sequence);
            app->last_frame_ms = t;
            update_ui(app);
            queue_canvas(app);
        }
    }

    return G_SOURCE_CONTINUE;
}

static void activate(GtkApplication *gtk_app, gpointer user_data)
{
    App *app = user_data;
    app->gtk_app = gtk_app;

    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), WINDOW_TITLE " (GTK)");
    gtk_window_set_default_size(GTK_WINDOW(app->window), DEFAULT_WIDTH, DEFAULT_HEIGHT);

    app->root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(app->window), app->root_box);

    app->main_toolbar = create_main_toolbar(app);
    gtk_box_append(GTK_BOX(app->root_box), app->main_toolbar);

    app->pen_toolbar = create_pen_toolbar(app);
    app->pen_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(app->pen_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(GTK_REVEALER(app->pen_revealer), app->pen_toolbar);
    gtk_box_append(GTK_BOX(app->root_box), app->pen_revealer);

    app->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->area, MIN_WIDTH, MIN_HEIGHT);
    gtk_widget_set_hexpand(app->area, TRUE);
    gtk_widget_set_vexpand(app->area, TRUE);
    gtk_widget_set_focusable(app->area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->area), on_draw, app, NULL);
    gtk_box_append(GTK_BOX(app->root_box), app->area);

    app->status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0);
    gtk_widget_set_margin_top(app->status_label, 3);
    gtk_widget_set_margin_bottom(app->status_label, 3);
    gtk_widget_set_margin_start(app->status_label, 8);
    gtk_widget_set_margin_end(app->status_label, 8);
    gtk_box_append(GTK_BOX(app->root_box), app->status_label);

    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), app);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), app);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), app);
    gtk_widget_add_controller(app->area, GTK_EVENT_CONTROLLER(drag));

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(on_motion_enter), app);
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion_move), app);
    g_signal_connect(motion, "leave", G_CALLBACK(on_motion_leave), app);
    gtk_widget_add_controller(app->area, motion);

    GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL |
                                                                 GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), app);
    gtk_widget_add_controller(app->area, scroll);

    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), app);
    gtk_widget_add_controller(app->window, key);

    app->last_frame_ms = now_ms();
    app->tick_id = g_timeout_add(1000 / MAX_FPS, on_tick, app);

    update_ui(app);

    gtk_window_present(GTK_WINDOW(app->window));
    gtk_widget_grab_focus(app->area);
}

static gboolean slurp_stdin(char **out_data, gsize *out_len)
{
    *out_data = NULL;
    *out_len = 0;

    GByteArray *buf = g_byte_array_new();
    guint8 tmp[65536];

    while (1) {
        size_t n = fread(tmp, 1, sizeof(tmp), stdin);
        if (n > 0) g_byte_array_append(buf, tmp, (guint)n);
        if (n < sizeof(tmp)) {
            if (ferror(stdin)) {
                g_byte_array_free(buf, TRUE);
                return FALSE;
            }
            break;
        }
    }

    g_byte_array_append(buf, (const guint8 *)"\0", 1);
    *out_len = buf->len - 1;
    *out_data = (char *)g_byte_array_free(buf, FALSE);
    return TRUE;
}

static gboolean load_inputs(App *app, int argc, char **argv)
{
    gboolean has_stdin = !isatty(fileno(stdin));
    gboolean stdin_requested = FALSE;
    GPtrArray *files = g_ptr_array_new();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            stdin_requested = TRUE;
            continue;
        }

        /* Keep RSF-style key=value arguments for sf_init(), but do not treat
         * them as SVG paths unless such a file actually exists. */
        if (strchr(argv[i], '=') && !g_file_test(argv[i], G_FILE_TEST_EXISTS)) {
            continue;
        }

        g_ptr_array_add(files, argv[i]);
    }

    gboolean ok = FALSE;

    if (files->len > 0) {
        ok = svg_sequence_load_files(&app->sequence, (char **)files->pdata, (int)files->len) || ok;
    }

    if (stdin_requested || (files->len == 0 && has_stdin)) {
        char *data = NULL;
        gsize len = 0;
        if (!slurp_stdin(&data, &len)) {
            fprintf(stderr, "Failed to read SVG data from stdin.\n");
        } else {
            ok = svg_sequence_load_from_stream(&app->sequence, data, len) || ok;
            g_free(data);
        }
    }

    g_ptr_array_free(files, TRUE);
    return ok;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s file.svg             # Load from file\n"
            "  cat file.svg | %s       # Load from stdin\n"
            "  %s - < file.svg         # Explicit stdin\n",
            prog, prog, prog);
}

int main(int argc, char **argv)
{
    gboolean has_stdin = !isatty(fileno(stdin));
    if (argc < 2 && !has_stdin) {
        print_usage(argv[0]);
        return 1;
    }

    sf_init(argc, argv);

    App app;
    memset(&app, 0, sizeof(app));
    app.canvas_w = DEFAULT_WIDTH;
    app.canvas_h = DEFAULT_HEIGHT;
    app.pan_x = 0.0;
    app.pan_y = 0.0;
    app.dragging = FALSE;
    app.zoom_scale = 1.0;
    app.stretch_mode = FALSE;
    app.tool = TOOL_NONE;
    app.pen_tool = PEN_TOOL_BRUSH;
    app.pen_width = 3.0;
    set_rgba(&app.recent_colors[0], 1.0, 0.10, 0.10, 1.0);
    set_rgba(&app.recent_colors[1], 0.00, 0.75, 0.20, 1.0);
    set_rgba(&app.recent_colors[2], 0.05, 0.25, 1.00, 1.0);
    app.pen_color = app.recent_colors[0];
    app.custom_color = app.pen_color;
    app.color_slot = COLOR_SLOT_RECENT0;
    app.annotations = g_ptr_array_new_with_free_func(annotation_free);
    app.undo_stack = g_ptr_array_new_with_free_func(annotation_layer_snapshot_free);
    app.redo_stack = g_ptr_array_new_with_free_func(annotation_layer_snapshot_free);
    app.active_annotation = NULL;
    app.sequence.fps = 12;
    app.sequence.playing = FALSE;

    if (!load_inputs(&app, argc, argv)) {
        fprintf(stderr, "Failed to load SVG input.\n");
        return 1;
    }

    if (app.sequence.count > 1) app.sequence.playing = TRUE;
    svg_sequence_set_handle_cache_radius(&app.sequence, 1);

    GtkApplication *gtk_app = gtk_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), &app);

    char *gtk_argv[] = { argv[0], NULL };
    int status = g_application_run(G_APPLICATION(gtk_app), 1, gtk_argv);

    if (app.tick_id) g_source_remove(app.tick_id);
    g_object_unref(gtk_app);
    svg_sequence_free(&app.sequence);
    if (app.annotations) g_ptr_array_free(app.annotations, TRUE);
    if (app.undo_stack) g_ptr_array_free(app.undo_stack, TRUE);
    if (app.redo_stack) g_ptr_array_free(app.redo_stack, TRUE);

    return status;
}
