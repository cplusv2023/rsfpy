#pragma once
#include <cairo.h>
#include <librsvg/rsvg.h>
#include <glib.h>
#include <math.h>

#define WINDOW_TITLE "RSFPY - SVG Sequence Viewer"


/* Aspect */
#define DEFAULT_DPI 90
#define DEFAULT_WIDTH 960
#define DEFAULT_HEIGHT 720
#define MIN_WIDTH 500
#define MIN_HEIGHT 450
#define MIN_BAR_HEIGHT 24
#define BAR_HEIGHT_RATIO 0.025
#define DEFAULT_FONT_HEIGHT 12
#define INCH2PT 72

/* Zoom */
#define MAX_ZOOM_SCALE 100.00
#define MIN_ZOOM_SCALE 0.01

/* Playback */
#define MAX_FPS 50
#define MIN_FPS 1

#define RUNMSG "Running"
#define PAUSEMSG "Paused"

/* Buttons */
#define MAX_BUTTONS 9
#define NEXT_BUTTON 0
#define PREV_BUTTON 1
#define RUN_BUTTON 2
#define PAUSE_BUTTON 3
#define SLOWER_BUTTON 4
#define FASTER_BUTTON 5
#define MOVE_BUTTON 6
#define ZOOM_BUTTON 7
#define HOME_BUTTON 8 /* Reset button */
#define ENABLED_COLOR "#000"
#define DISABLED_COLOR "#888"
#define PRESSED_COLOR "#aaa"
#define BAR_COLOR "#eee"
#define BACKGROUND_COLOR "#ccc"
#define WARNING_COLOR "#f00"
#define WHITE "#fff"

#define WAIT_TIME_MS 100

#define MAX_FRAMES 1000
#define MAX_TMP_FILES 1000

#define PXPT_TRANS 1.066667f /* 96/90 */
#define SPLIT_MARKER "<!-- RSFPY_SPLIT -->"

#define MIN_WIDTH 500
#define MIN_HEIGHT 450

// #define MAX_IMAGE_SURFACE ((size_t)INT_MAX)

typedef struct {
    char *path;
    char *framelabel;
    RsvgHandle *handle;
    cairo_surface_t *surface;
    double width, height;
    gboolean rendered;
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

void sf_init(int argc, char **argv);
void cairo_set_source_rgba_string(cairo_t *cr, const char *color_str);
gboolean svg_sequence_load_files(SvgSequence *seq, char **paths, int num);
void svg_sequence_render_frame(SvgSequence *seq, cairo_t *cr, int win_w, int win_h, int pan_x, int pan_y, double zoom_scale, int toolbar_h, int hintbar_h) ;
void svg_sequence_advance(SvgSequence *seq);
void svg_sequence_free(SvgSequence *seq);
gboolean svg_sequence_load_from_stream(SvgSequence *seq, const char *data, size_t len);


