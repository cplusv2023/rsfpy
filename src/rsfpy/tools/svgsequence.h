#pragma once
#include <cairo.h>
#include <librsvg/rsvg.h>
#include <glib.h>
#include <limits.h>

#define MAX_FRAMES 200

#define PXPT_TRANS 1.066667f /* 96/90 */
#define SPLIT_MARKER "<!-- RSFPY_SPLIT -->"

#define MIN_WIDTH 500
#define MIN_HEIGHT 450

#define MAX_IMAGE_SURFACE ((size_t)INT_MAX)

typedef struct {
    char *path;
    char *framelabel;
    RsvgHandle *handle;
    cairo_surface_t *surface;
    double width, height;
    gboolean rendered;

    // 新增：缓存生成时的缩放和窗口大小
    double cached_scale;
    int cached_win_w;
    int cached_win_h;
} SvgFrame;

typedef struct {
    SvgFrame frames[MAX_FRAMES];
    int count;
    int current_index;
    int fps;
    gboolean playing;
} SvgSequence;

gboolean svg_sequence_load_files(SvgSequence *seq, char **paths, int num);
void svg_sequence_render_frame(SvgSequence *seq, cairo_t *cr, int win_w, int win_h, int pan_x, int pan_y, double zoom_scale, int toolbar_h, int hintbar_h) ;
void svg_sequence_advance(SvgSequence *seq);
void svg_sequence_free(SvgSequence *seq);
gboolean svg_sequence_load_from_stream(SvgSequence *seq, const char *data, size_t len);


