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
#define MAX_BUTTONS 10
#define NEXT_BUTTON 1
#define PREV_BUTTON 0
#define RUN_BUTTON 2
#define PAUSE_BUTTON 3
#define SLOWER_BUTTON 4
#define FASTER_BUTTON 5
#define MOVE_BUTTON 6
#define ZOOM_BUTTON 7
#define HOME_BUTTON 8 /* Reset button */
#define STRETCH_BUTTON 9
#define ENABLED_COLOR "#000"
#define DISABLED_COLOR "#888"
#define PRESSED_COLOR "#aaa"
#define BAR_COLOR "#eee"
#define BACKGROUND_COLOR "#ccc"
#define WARNING_COLOR "#f00"
#define WHITE "#fff"

#define WAIT_TIME_MS 100

/* The old code used fixed MAX_FRAMES/MAX_TMP_FILES arrays.  The new
 * sequence layer grows dynamically, so these values are no longer hard
 * limits.  Keep the names for source compatibility with older callers. */
#define MAX_FRAMES 1000
#define MAX_TMP_FILES 1000

#define PXPT_TRANS 1.066667f /* 96/90 */
#define SPLIT_MARKER "<!-- RSFPY_SPLIT -->"

/* How many non-current RsvgHandle objects to keep around on each side of the
 * current frame.  0 is the most memory-friendly default.  Increase it later if
 * playback smoothness matters more than memory. */
#define SVG_SEQUENCE_DEFAULT_HANDLE_CACHE_RADIUS 0

typedef struct {
    /* User-facing path shown by the viewer, e.g. file.svg.frame[12]. */
    char *path;
    char *framelabel;

    /* Actual source.  For a normal SVG this is the original file.  For a
     * sequence frame it is the parent file plus byte range.  For stdin it is a
     * temporary file. */
    char *source_path;
    goffset data_offset;
    gsize data_len;
    gboolean use_range;
    gboolean remove_source_on_free;

    /* Loaded lazily.  No cairo_surface_t is stored here; X11/GTK frontends pass
     * their drawing cairo_t into svg_sequence_render_frame(). */
    RsvgHandle *handle;
    double width, height;
    gboolean dimensions_valid;
} SvgFrame;

typedef struct {
    SvgFrame *frames;
    int count;
    int capacity;
    int current_index;
    int fps;
    gboolean playing;

    int handle_cache_radius;
} SvgSequence;

void sf_init(int argc, char **argv);
void cairo_set_source_rgba_string(cairo_t *cr, const char *color_str);

gboolean svg_sequence_load_files(SvgSequence *seq, char **paths, int num);
void svg_sequence_render_frame(SvgSequence *seq, cairo_t *cr,
                               int win_w, int win_h,
                               double pan_x, double pan_y,
                               double zoom_scale,
                               gboolean stretch_mode,
                               int toolbar_h, int hintbar_h);
void svg_sequence_advance(SvgSequence *seq);
void svg_sequence_free(SvgSequence *seq);
gboolean svg_sequence_load_from_stream(SvgSequence *seq, const char *data, size_t len);

/* Optional tuning hook for future frontends.  0 keeps only the current frame's
 * handle; 1 keeps prev/current/next; negative values are treated as 0. */
void svg_sequence_set_handle_cache_radius(SvgSequence *seq, int radius);
