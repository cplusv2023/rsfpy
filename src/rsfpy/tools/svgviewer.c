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

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#if defined(__APPLE__) && defined(__MACH__)
gboolean rsfpy_macos_copy_svg_text_to_clipboard(const gchar *svg,
                                                gsize svg_len,
                                                const guint8 *png,
                                                gsize png_len,
                                                GError **err);
#endif

typedef enum {
    TOOL_NONE = 0,
    TOOL_PAN,
    TOOL_ZOOM,
    TOOL_PEN
} ToolMode;

typedef enum {
    PEN_TOOL_LINE = 0,
    PEN_TOOL_LINE_DASH,
    PEN_TOOL_BRUSH,
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
    ANNO_LINE = 0,
    ANNO_LINE_DASH,
    ANNO_BRUSH,
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
    GtkWidget *pen_width_label;
    GtkWidget *pen_width_preview;
    GtkWidget *pen_width_scale;
    GtkWidget *pen_clear_button;
    GtkWidget *btn_export;
    GtkWidget *btn_copy;
    GtkWidget *btn_quit;
    GtkWidget *color_btn_red;
    GtkWidget *color_btn_green;
    GtkWidget *color_btn_blue;
    GtkWidget *color_btn_custom;
    GtkWidget *color_area_red;
    GtkWidget *color_area_green;
    GtkWidget *color_area_blue;
    GtkWidget *color_area_custom;
    GtkWidget *area;
    GtkWidget *status_box;
    GtkWidget *status_label;
    GtkWidget *status_progress;
    gchar *load_error;

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
    gchar *watch_path;
    gsize watch_offset;
    guint watch_id;

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
    gboolean updating_width_ui;
    gboolean busy;
    guint busy_pulse_id;

    GPtrArray *annotations;
    GPtrArray *undo_stack;
    GPtrArray *redo_stack;
    Annotation *active_annotation;

    gint64 last_frame_ms;
    guint tick_id;
} App;

static const char *APP_ID = "org.rsfpy.svgviewer.gtk";
#define UNDO_LIMIT 64
#define CLIPBOARD_SVG_WARN_BYTES (2 * 1024 * 1024)

static void update_ui(App *app);

static const char *ICON_PREV =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='none' stroke='#20242a' stroke-width='2' d='M17 2L7 12l10 10'/>"
    "</svg>";

static const char *ICON_NEXT =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='none' stroke='#20242a' stroke-width='2' d='m7 2l10 10L7 22'/>"
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
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<g transform='translate(24 0) scale(-1 1)'>"
    "<path fill='#20242a' d='m12 13.333l-9.223 6.149A.5.5 0 0 1 2 19.066V4.934a.5.5 0 0 1 .777-.416L12 10.667V4.934a.5.5 0 0 1 .777-.416l10.599 7.066a.5.5 0 0 1 0 .832l-10.599 7.066a.5.5 0 0 1-.777-.416z'/>"
    "</g>"
    "</svg>";

static const char *ICON_FASTER =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='#20242a' d='m12 13.333l-9.223 6.149A.5.5 0 0 1 2 19.066V4.934a.5.5 0 0 1 .777-.416L12 10.667V4.934a.5.5 0 0 1 .777-.416l10.599 7.066a.5.5 0 0 1 0 .832l-10.599 7.066a.5.5 0 0 1-.777-.416z'/>"
    "</svg>";

static const char *ICON_PAN =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'>"
    "<path d='M0 0h512v512H0z' fill='none'/>"
    "<path fill='#20242a' fill-rule='evenodd' d='M277.334 298.667V384H320l-64 85.334L192 384h42.667v-85.333zM384 192l85.334 64L384 320v-42.666h-85.333v-42.667H384zm-256 0v42.667h85.334v42.667H128V320l-85.333-64zM256 42.667L320 128h-42.666v85.334h-42.667V128H192z'/>"
    "</svg>";

static const char *ICON_ZOOM =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='none' stroke='#20242a' stroke-linecap='round' stroke-linejoin='round' stroke-width='2' d='M3 10a7 7 0 1 0 14 0a7 7 0 1 0-14 0m18 11l-6-6'/>"
    "</svg>";

static const char *ICON_PEN =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='#20242a' d='M9.75 20.85c1.78-.7 1.39-2.63.49-3.85c-.89-1.25-2.12-2.11-3.36-2.94A9.8 9.8 0 0 1 4.54 12c-.28-.33-.85-.94-.27-1.06c.59-.12 1.61.46 2.13.68c.91.38 1.81.82 2.65 1.34l1.01-1.7C8.5 10.23 6.5 9.32 4.64 9.05c-1.06-.16-2.18.06-2.54 1.21c-.32.99.19 1.99.77 2.77c1.37 1.83 3.5 2.71 5.09 4.29c.34.33.75.72.95 1.18c.21.44.16.47-.31.47c-1.24 0-2.79-.97-3.8-1.61l-1.01 1.7c1.53.94 4.09 2.41 5.96 1.79m11.09-15.6c.22-.22.22-.58 0-.79l-1.3-1.3a.56.56 0 0 0-.78 0l-1.02 1.02l2.08 2.08M11 10.92V13h2.08l6.15-6.15l-2.08-2.08z'/>"
    "</svg>";

static const char *ICON_RESET =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 21 21'>"
    "<path d='M0 0h21v21H0z' fill='none'/>"
    "<g fill='none' fill-rule='evenodd' stroke='#20242a' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M3.578 6.487A8 8 0 1 1 2.5 10.5'/>"
    "<path d='M7.5 6.5h-4v-4'/>"
    "</g>"
    "</svg>";

static const char *ICON_STRETCH =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<g fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M8 8L3 3M3 3h6M3 3v6M16 8l5-5M21 3h-6M21 3v6M8 16l-5 5M3 21h6M3 21v-6M16 16l5 5M21 21h-6M21 21v-6'/>"
    "</g>"
    "</svg>";

static const char *ICON_UNDO =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='#20242a' d='M7 19v-2h7.1q1.575 0 2.738-1T18 13.5T16.838 11T14.1 10H7.8l2.6 2.6L9 14L4 9l5-5l1.4 1.4L7.8 8h6.3q2.425 0 4.163 1.575T20 13.5t-1.737 3.925T14.1 19z'/>"
    "</svg>";

static const char *ICON_REDO =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='#20242a' d='M9.9 19q-2.425 0-4.163-1.575T4 13.5t1.738-3.925T9.9 8h6.3l-2.6-2.6L15 4l5 5l-5 5l-1.4-1.4l2.6-2.6H9.9q-1.575 0-2.738 1T6 13.5T7.163 16T9.9 17H17v2z'/>"
    "</svg>";

static const char *ICON_BRUSH =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 14 14'>"
    "<path d='M0 0h14v14H0z' fill='none'/>"
    "<path fill='none' stroke='#20242a' stroke-linecap='round' stroke-linejoin='round' d='M.64 7.86c.785-1.097 3.164-4.214 5.947-5.967c2.093-1.318 4.07.422 2.537 2.174c-1.499 1.714-3.51 4.073-4.368 5.244c-.89 1.214.633 2.682 2.157 1.285c1.019-.934 2.08-2.008 3.158-2.802c1.456-1.071 2.705-.125 2.07 1.082c-.46.873-.793 1.258-1.176 1.993c-.384.734.022 1.615.603 1.69c.72.094 1.176-.423 1.791-1.228'/>"
    "</svg>";

static const char *ICON_BRUSH_DASH =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 14 14'>"
    "<path d='M0 0h14v14H0z' fill='none'/>"
    "<path fill='none' stroke='#20242a' stroke-linecap='round' stroke-linejoin='round' stroke-dasharray='1.3 3.6' d='M.64 7.86c.785-1.097 3.164-4.214 5.947-5.967c2.093-1.318 4.07.422 2.537 2.174c-1.499 1.714-3.51 4.073-4.368 5.244c-.89 1.214.633 2.682 2.157 1.285c1.019-.934 2.08-2.008 3.158-2.802c1.456-1.071 2.705-.125 2.07 1.082c-.46.873-.793 1.258-1.176 1.993c-.384.734.022 1.615.603 1.69c.72.094 1.176-.423 1.791-1.228'/>"
    "</svg>";

static const char *ICON_LINE =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M0 0h16v16H0z' fill='none'/>"
    "<path fill='#20242a' d='M0 7h16v1H0z'/>"
    "</svg>";

static const char *ICON_LINE_DASH =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M0 0h16v16H0z' fill='none'/>"
    "<path fill='none' stroke='#20242a' stroke-width='1.3' stroke-linecap='butt' stroke-dasharray='2 3.5' d='M0 7.5h16'/>"
    "</svg>";

static const char *ICON_RECT =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 256 256'>"
    "<path d='M0 0h256v256H0z' fill='none'/>"
    "<path fill='#20242a' d='M216 40H40a16 16 0 0 0-16 16v144a16 16 0 0 0 16 16h176a16 16 0 0 0 16-16V56a16 16 0 0 0-16-16m0 160H40V56h176z'/>"
    "</svg>";

static const char *ICON_RECT_DASH =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 256 256'>"
    "<path d='M0 0h256v256H0z' fill='none'/>"
    "<path fill='#20242a' d='M80 48a8 8 0 0 1-8 8H40v16a8 8 0 0 1-16 0V56a16 16 0 0 1 16-16h32a8 8 0 0 1 8 8M32 152a8 8 0 0 0 8-8v-32a8 8 0 0 0-16 0v32a8 8 0 0 0 8 8m40 48H40v-16a8 8 0 0 0-16 0v16a16 16 0 0 0 16 16h32a8 8 0 0 0 0-16m72 0h-32a8 8 0 0 0 0 16h32a8 8 0 0 0 0-16m80-24a8 8 0 0 0-8 8v16h-32a8 8 0 0 0 0 16h32a16 16 0 0 0 16-16v-16a8 8 0 0 0-8-8m0-72a8 8 0 0 0-8 8v32a8 8 0 0 0 16 0v-32a8 8 0 0 0-8-8m-8-64h-32a8 8 0 0 0 0 16h32v16a8 8 0 0 0 16 0V56a16 16 0 0 0-16-16m-72 0h-32a8 8 0 0 0 0 16h32a8 8 0 0 0 0-16'/>"
    "</svg>";

static const char *ICON_ARROW =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='#20242a' d='M20 11v2H8l5.5 5.5l-1.42 1.42L4.16 12l7.92-7.92L13.5 5.5L8 11z'/>"
    "</svg>";

static const char *ICON_ERASER =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='#20242a' d='M21 12.41c.78-.78.78-2.05 0-2.83l-5.59-5.59c-.78-.78-2.05-.78-2.83 0L3 13.59c-.78.78-.78 2.05 0 2.83l4.29 4.29c.19.19.44.29.71.29h14v-2h-7.59zM8.41 19l-4-4L10 9.41L15.59 15l-4 4H8.42Z'/>"
    "</svg>";

static const char *ICON_ERASER_FULL =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='#20242a' d='m16.24 3.56l4.95 4.94c.78.79.78 2.05 0 2.84L12 20.53a4.01 4.01 0 0 1-5.66 0L2.81 17c-.78-.79-.78-2.05 0-2.84l10.6-10.6c.79-.78 2.05-.78 2.83 0M4.22 15.58l3.54 3.53c.78.79 2.04.79 2.83 0l3.53-3.53l-4.95-4.95z'/>"
    "</svg>";

static const char *ICON_CLEAR =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<g fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M4 7h16M10 11v6M14 11v6M6 7l1 14h10l1-14M9 7V4h6v3'/>"
    "</g>"
    "</svg>";

static const char *ICON_EXPORT =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<g fill='none' stroke='#20242a' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M12 3v12M8 7l4-4 4 4M5 15v4h14v-4'/>"
    "</g>"
    "</svg>";

static const char *ICON_COPY =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M0 0h16v16H0z' fill='none'/>"
    "<path fill='#20242a' d='M0 2.729V2a1 1 0 0 1 1-1h2v1H1v12h4v1H1a1 1 0 0 1-1-1zM12 5V2a1 1 0 0 0-1-1H9v1h2v3zm-1 1h2v9H6V6zV5H6a1 1 0 0 0-1 1v9a1 1 0 0 0 1 1h7a1 1 0 0 0 1-1V6a1 1 0 0 0-1-1h-2z'/>"
    "<path fill='#20242a' d='M7 10h5V9H7zm0-2h5V7H7zm0 4h5v-1H7zm0 2h5v-1H7zM9 2V1a1 1 0 0 0-1-1H4a1 1 0 0 0-1 1v1h1V1h4v1zM3 3h6V2H3z'/>"
    "</svg>";

static const char *ICON_IMAGE_ERROR =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='none' stroke='#6b7280' stroke-linecap='square' stroke-width='2' d='M21 11V3H3v18h8m2-7l-4-4l-5.5 5.5m14.25-7.25a2 2 0 1 1-4 0a2 2 0 0 1 4 0Zm4.079 7.922L19 19m0 0l-2.828 2.829M19 19l-2.828-2.828M19 19l2.829 2.829'/>"
    "</svg>";

static const char *ICON_QUIT =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path d='M0 0h24v24H0z' fill='none'/>"
    "<path fill='#8f2f2b' d='M6.4 19L5 17.6l5.6-5.6L5 6.4L6.4 5l5.6 5.6L17.6 5L19 6.4L13.4 12l5.6 5.6l-1.4 1.4l-5.6-5.6z'/>"
    "</svg>";

static void update_ui(App *app);
static void queue_canvas(App *app);
static gboolean pen_tool_is_eraser(App *app);
static gboolean pen_tool_is_partial_eraser(App *app);
static void on_export_clicked(GtkButton *button, gpointer user_data);
static void on_copy_clicked(GtkButton *button, gpointer user_data);
static void on_quit_clicked(GtkButton *button, gpointer user_data);
static gboolean on_window_close_request(GtkWindow *window, gpointer user_data);
static gchar *build_current_view_svg(App *app, gsize *out_len, GError **err);

static gint64 now_ms(void)
{
    return g_get_monotonic_time() / 1000;
}

static gboolean str_empty(const gchar *s)
{
    return !s || !*s;
}

#ifdef G_OS_WIN32
static gchar *win_self_exe_path(const gchar *argv0)
{
    wchar_t wbuf[MAX_PATH + 1];
    DWORD n = GetModuleFileNameW(NULL, wbuf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        return g_utf16_to_utf8((const gunichar2 *)wbuf, (glong)n, NULL, NULL, NULL);
    }

    if (argv0 && g_path_is_absolute(argv0)) return g_strdup(argv0);
    return argv0 ? g_find_program_in_path(argv0) : NULL;
}

static void setup_windows_bundle_env(const gchar *argv0)
{
    gchar *exe = win_self_exe_path(argv0);
    gchar *dir = exe ? g_path_get_dirname(exe) : g_get_current_dir();
    const gchar *old_path = g_getenv("PATH");
    gchar *new_path = g_strconcat(dir, G_SEARCHPATH_SEPARATOR_S,
                                  old_path ? old_path : "", NULL);
    gchar *share = g_build_filename(dir, "share", NULL);
    gchar *schemas = g_build_filename(dir, "share", "glib-2.0", "schemas", NULL);
    gchar *loaders = g_build_filename(dir, "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders", NULL);
    gchar *loader_cache = g_build_filename(dir, "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders.cache", NULL);

    g_setenv("PATH", new_path, TRUE);
    g_setenv("XDG_DATA_DIRS", share, TRUE);
    g_setenv("GSETTINGS_SCHEMA_DIR", schemas, TRUE);
    g_setenv("GTK_DATA_PREFIX", dir, TRUE);
    g_setenv("GTK_EXE_PREFIX", dir, TRUE);
    g_setenv("GTK_PATH", dir, TRUE);
    g_setenv("GDK_PIXBUF_MODULEDIR", loaders, TRUE);
    if (!g_getenv("GSK_RENDERER")) {
        g_setenv("GSK_RENDERER", "cairo", TRUE);
    }
    if (g_file_test(loader_cache, G_FILE_TEST_EXISTS)) {
        g_setenv("GDK_PIXBUF_MODULE_FILE", loader_cache, TRUE);
    }

    g_free(loader_cache);
    g_free(loaders);
    g_free(schemas);
    g_free(share);
    g_free(new_path);
    g_free(dir);
    g_free(exe);
}
#endif

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
    if (a->type == ANNO_LINE || a->type == ANNO_LINE_DASH) {
        return dist2(a->x0, a->y0, a->x1, a->y1) > 1e-12;
    }
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
        case PEN_TOOL_LINE:       return "Line";
        case PEN_TOOL_LINE_DASH:  return "Line-dash";
        case PEN_TOOL_BRUSH:      return "Brush";
        case PEN_TOOL_BRUSH_DASH: return "Brush-dash";
        case PEN_TOOL_RECT:       return "Rectangle";
        case PEN_TOOL_RECT_DASH:  return "Rectangle-dash";
        case PEN_TOOL_ARROW:          return "Arrow";
        case PEN_TOOL_ERASER_PARTIAL: return "Eraser-partial";
        case PEN_TOOL_ERASER_FULL:    return "Eraser-full";
        default:                      return "Brush";
    }
}

static const char *pen_tool_icon(PenTool tool)
{
    switch (tool) {
        case PEN_TOOL_LINE:           return ICON_LINE;
        case PEN_TOOL_LINE_DASH:      return ICON_LINE_DASH;
        case PEN_TOOL_BRUSH:          return ICON_BRUSH;
        case PEN_TOOL_BRUSH_DASH:     return ICON_BRUSH_DASH;
        case PEN_TOOL_RECT:           return ICON_RECT;
        case PEN_TOOL_RECT_DASH:      return ICON_RECT_DASH;
        case PEN_TOOL_ARROW:          return ICON_ARROW;
        case PEN_TOOL_ERASER_PARTIAL: return ICON_ERASER;
        case PEN_TOOL_ERASER_FULL:    return ICON_ERASER_FULL;
        default:                      return ICON_BRUSH;
    }
}

static double clamp_pen_width(double value)
{
    if (value < 0.1) return 0.1;
    if (value > 10.0) return 10.0;
    return value;
}

static void format_pen_width(char *buf, gsize size, double value)
{
    if (!buf || size == 0) return;
    value = clamp_pen_width(value);
    g_snprintf(buf, size, "%.2g", value);
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

static void queue_pen_width_preview(App *app)
{
    if (app && app->pen_width_preview) gtk_widget_queue_draw(app->pen_width_preview);
}

static void draw_width_preview(GtkDrawingArea *area,
                               cairo_t *cr,
                               int width,
                               int height,
                               gpointer user_data)
{
    (void)area;
    App *app = user_data;
    double r = clamp_pen_width(app ? app->pen_width : 3.0);
    double max_r = 10.0;
    double radius = 2.0 + (r - 0.1) / (max_r - 0.1) * 7.0;
    double cx = width * 0.5;
    double cy = height * 0.5;

    cairo_save(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    cairo_arc(cr, cx, cy, radius, 0.0, 2.0 * G_PI);
    cairo_set_source_rgba(cr,
                          app ? app->pen_color.red : 0.1,
                          app ? app->pen_color.green : 0.1,
                          app ? app->pen_color.blue : 0.1,
                          app ? app->pen_color.alpha : 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.42);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void sync_pen_width_widgets(App *app)
{
    char text[16];

    if (!app || app->updating_width_ui) return;

    format_pen_width(text, sizeof(text), app->pen_width);
    app->updating_width_ui = TRUE;
    if (app->pen_width_label) {
        gtk_label_set_text(GTK_LABEL(app->pen_width_label), text);
    }
    if (app->pen_width_scale) {
        gtk_range_set_value(GTK_RANGE(app->pen_width_scale), app->pen_width);
    }
    app->updating_width_ui = FALSE;
    queue_pen_width_preview(app);
}

static void set_pen_width_value(App *app, double value)
{
    if (!app) return;
    app->pen_width = clamp_pen_width(value);
    sync_pen_width_widgets(app);
    update_ui(app);
    queue_canvas(app);
}

static void queue_canvas(App *app)
{
    if (app && app->area) gtk_widget_queue_draw(app->area);
}

static void update_ui(App *app)
{
    if (!app) return;

    gboolean multi = (app->sequence.count > 1);
    gboolean ready = !app->busy;

    if (app->btn_prev)   gtk_widget_set_sensitive(app->btn_prev,   ready && multi);
    if (app->btn_next)   gtk_widget_set_sensitive(app->btn_next,   ready && multi);
    if (app->btn_run)    gtk_widget_set_sensitive(app->btn_run,    ready && multi && !app->sequence.playing);
    if (app->btn_pause)  gtk_widget_set_sensitive(app->btn_pause,  ready && multi && app->sequence.playing);
    if (app->btn_slower) gtk_widget_set_sensitive(app->btn_slower, ready && multi && app->sequence.fps > MIN_FPS);
    if (app->btn_faster) gtk_widget_set_sensitive(app->btn_faster, ready && multi && app->sequence.fps < MAX_FPS);
    if (app->btn_export) gtk_widget_set_sensitive(app->btn_export, ready && current_frame(app) != NULL);
    if (app->btn_copy)   gtk_widget_set_sensitive(app->btn_copy,   ready && current_frame(app) != NULL);

    if (app->btn_reset) {
        gboolean changed = (app->zoom_scale != 1.0 || app->pan_x != 0.0 || app->pan_y != 0.0);
        gtk_widget_set_sensitive(app->btn_reset, ready && changed);
    }
    if (app->btn_undo) gtk_widget_set_sensitive(app->btn_undo, ready && app->undo_stack && app->undo_stack->len > 0);
    if (app->btn_redo) gtk_widget_set_sensitive(app->btn_redo, ready && app->redo_stack && app->redo_stack->len > 0);
    if (app->pen_clear_button) gtk_widget_set_sensitive(app->pen_clear_button, ready);
    if (app->pen_tool_dropdown) gtk_widget_set_sensitive(app->pen_tool_dropdown, ready);
    if (app->pen_width_scale) gtk_widget_set_sensitive(app->pen_width_scale, ready);
    if (app->color_btn_red) gtk_widget_set_sensitive(app->color_btn_red, ready);
    if (app->color_btn_green) gtk_widget_set_sensitive(app->color_btn_green, ready);
    if (app->color_btn_blue) gtk_widget_set_sensitive(app->color_btn_blue, ready);
    if (app->color_btn_custom) gtk_widget_set_sensitive(app->color_btn_custom, ready);
    if (app->toggle_pan) gtk_widget_set_sensitive(app->toggle_pan, ready);
    if (app->toggle_zoom) gtk_widget_set_sensitive(app->toggle_zoom, ready);
    if (app->toggle_stretch) gtk_widget_set_sensitive(app->toggle_stretch, ready);
    if (app->toggle_pen) gtk_widget_set_sensitive(app->toggle_pen, ready);
    if (app->area) gtk_widget_set_sensitive(app->area, ready);

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
    sync_pen_width_widgets(app);
    app->updating_ui = FALSE;

    queue_pen_color_areas(app);
    queue_pen_width_preview(app);

    if (!app->busy && app->status_label) {
        char status[1024];
        const char *path = "N/A";
        const char *frame_label = NULL;
        int iframe = 0;
        int nframe = app->sequence.count;
        char *color_text = gdk_rgba_to_string(&app->pen_color);

        if (app->sequence.count <= 0) {
            g_snprintf(status, sizeof(status), "%s",
                       str_empty(app->load_error) ? "No displayable SVG content." : app->load_error);
            gtk_label_set_text(GTK_LABEL(app->status_label), status);
            g_free(color_text);
            return;
        }

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

static gboolean busy_progress_pulse(gpointer user_data)
{
    App *app = user_data;
    if (!app || !app->busy || !app->status_progress) return G_SOURCE_REMOVE;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(app->status_progress));
    return G_SOURCE_CONTINUE;
}

static void set_busy(App *app, gboolean busy, const char *message)
{
    if (!app) return;

    app->busy = busy;

    if (busy) {
        app->sequence.playing = FALSE;
        if (app->status_progress) {
            gtk_widget_set_visible(app->status_progress, TRUE);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->status_progress), 0.0);
            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(app->status_progress));
        }
        if (app->busy_pulse_id == 0) {
            app->busy_pulse_id = g_timeout_add(120, busy_progress_pulse, app);
        }
    } else {
        if (app->busy_pulse_id != 0) {
            g_source_remove(app->busy_pulse_id);
            app->busy_pulse_id = 0;
        }
        if (app->status_progress) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->status_progress), 0.0);
            gtk_widget_set_visible(app->status_progress, FALSE);
        }
    }

    update_ui(app);
    if (message && app->status_label) {
        gtk_label_set_text(GTK_LABEL(app->status_label), message);
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
            g_clear_error(&err);
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

static void on_pen_tool_factory_bind(GtkSignalListItemFactory *factory,
                                     GtkListItem *item,
                                     gpointer user_data)
{
    (void)factory;
    (void)user_data;

    guint pos = gtk_list_item_get_position(item);
    PenTool tool = pos <= PEN_TOOL_ERASER_FULL ? (PenTool)pos : PEN_TOOL_BRUSH;
    GtkWidget *icon = make_icon_child(pen_tool_icon(tool), pen_tool_name(tool));

    gtk_widget_set_tooltip_text(icon, pen_tool_name(tool));
    gtk_list_item_set_accessible_label(item, pen_tool_name(tool));
    gtk_list_item_set_child(item, icon);
}

static void on_pen_tool_factory_unbind(GtkSignalListItemFactory *factory,
                                       GtkListItem *item,
                                       gpointer user_data)
{
    (void)factory;
    (void)user_data;
    gtk_list_item_set_child(item, NULL);
}

static GtkListItemFactory *make_pen_tool_icon_factory(void)
{
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "bind", G_CALLBACK(on_pen_tool_factory_bind), NULL);
    g_signal_connect(factory, "unbind", G_CALLBACK(on_pen_tool_factory_unbind), NULL);
    return factory;
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

static GtkWidget *make_toolbar_scroller(GtkWidget *child)
{
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_NEVER);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), child);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(sw), FALSE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sw), FALSE);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw), 38);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), 36);
    gtk_widget_set_hexpand(sw, TRUE);
    gtk_widget_set_size_request(sw, 38, -1);
    return sw;
}

static void ensure_app_css(void)
{
    static gboolean installed = FALSE;
    GdkDisplay *display;
    GtkCssProvider *provider;
    static const char css[] =
        "button.rsfpy-quit {"
        "  background: rgba(199, 54, 47, 0.16);"
        "  color: #8f2f2b;"
        "}"
        "button.rsfpy-quit:hover {"
        "  background: rgba(199, 54, 47, 0.26);"
        "}"
        "button.rsfpy-quit:disabled {"
        "  background: rgba(199, 54, 47, 0.08);"
        "  color: rgba(143, 47, 43, 0.55);"
        "}";

    if (installed) return;
    display = gdk_display_get_default();
    if (!display) return;

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(display,
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    installed = TRUE;
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

static void on_pen_width_scale_changed(GtkRange *range, gpointer user_data)
{
    App *app = user_data;
    if (!app || app->updating_width_ui) return;
    set_pen_width_value(app, gtk_range_get_value(range));
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

static GtkWidget *create_width_control(App *app)
{
    GtkWidget *popover = gtk_popover_new();
    GtkWidget *pop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *menu_button = gtk_menu_button_new();
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    char width_text[16];

    app->pen_width_preview = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->pen_width_preview, 24, 28);
    gtk_widget_set_tooltip_text(app->pen_width_preview, "Stroke width preview");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->pen_width_preview),
                                   draw_width_preview, app, NULL);

    format_pen_width(width_text, sizeof(width_text), app->pen_width);
    app->pen_width_label = gtk_label_new(width_text);
    gtk_widget_set_size_request(app->pen_width_label, 28, -1);
    gtk_label_set_xalign(GTK_LABEL(app->pen_width_label), 0.0);

    app->pen_width_scale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0.1, 10.0, 0.1);
    gtk_range_set_value(GTK_RANGE(app->pen_width_scale), clamp_pen_width(app->pen_width));
    gtk_scale_set_draw_value(GTK_SCALE(app->pen_width_scale), FALSE);
    gtk_widget_set_size_request(app->pen_width_scale, 36, 160);
    gtk_widget_set_tooltip_text(app->pen_width_scale, "Drag to adjust stroke width");
    g_signal_connect(app->pen_width_scale, "value-changed",
                     G_CALLBACK(on_pen_width_scale_changed), app);

    gtk_widget_set_margin_top(pop_box, 8);
    gtk_widget_set_margin_bottom(pop_box, 8);
    gtk_widget_set_margin_start(pop_box, 8);
    gtk_widget_set_margin_end(pop_box, 8);
    gtk_box_append(GTK_BOX(pop_box), app->pen_width_scale);
    gtk_popover_set_child(GTK_POPOVER(popover), pop_box);

    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button), popover);
    gtk_box_append(GTK_BOX(button_box), app->pen_width_preview);
    gtk_box_append(GTK_BOX(button_box), app->pen_width_label);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button), button_box);
    gtk_widget_set_tooltip_text(menu_button, "Stroke width");

    return menu_button;
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
    app->btn_export = make_icon_button(ICON_EXPORT, "Export current view", "Export",
                                       G_CALLBACK(on_export_clicked), app);
    app->btn_copy = make_icon_button(ICON_COPY, "Copy current SVG area", "Copy",
                                     G_CALLBACK(on_copy_clicked), app);
    app->btn_quit = make_icon_button(ICON_QUIT, "Quit", "Quit",
                                     G_CALLBACK(on_quit_clicked), app);
    gtk_widget_add_css_class(app->btn_quit, "destructive-action");
    gtk_widget_add_css_class(app->btn_quit, "rsfpy-quit");

    gtk_box_append(GTK_BOX(bar), app->btn_prev);
    gtk_box_append(GTK_BOX(bar), app->btn_next);
    append_sep(bar);
    gtk_box_append(GTK_BOX(bar), app->btn_slower);
    gtk_box_append(GTK_BOX(bar), app->btn_run);
    gtk_box_append(GTK_BOX(bar), app->btn_pause);
    gtk_box_append(GTK_BOX(bar), app->btn_faster);
    append_sep(bar);
    gtk_box_append(GTK_BOX(bar), app->toggle_pan);
    gtk_box_append(GTK_BOX(bar), app->toggle_zoom);
    gtk_box_append(GTK_BOX(bar), app->btn_reset);
    append_sep(bar);
    gtk_box_append(GTK_BOX(bar), app->toggle_pen);
    gtk_box_append(GTK_BOX(bar), app->toggle_stretch);
    append_sep(bar);
    gtk_box_append(GTK_BOX(bar), app->btn_export);
#if defined(G_OS_WIN32) || (defined(__APPLE__) && defined(__MACH__))
    gtk_box_append(GTK_BOX(bar), app->btn_copy);
#endif
    append_sep(bar);
    gtk_box_append(GTK_BOX(bar), app->btn_quit);

    return bar;
}

static GtkWidget *create_pen_toolbar(App *app)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_bottom(bar, 4);
    gtk_widget_set_margin_start(bar, 6);
    gtk_widget_set_margin_end(bar, 6);

    static const char * const tool_names[] = {
        "Line",
        "Line-dash",
        "Brush",
        "Brush-dash",
        "Rectangle",
        "Rectangle-dash",
        "Arrow",
        "Eraser-partial",
        "Eraser-full",
        NULL
    };
    {
        GtkStringList *tool_model = gtk_string_list_new(tool_names);
        GtkListItemFactory *selected_factory = make_pen_tool_icon_factory();
        GtkListItemFactory *list_factory = make_pen_tool_icon_factory();

        app->pen_tool_dropdown = gtk_drop_down_new(G_LIST_MODEL(tool_model), NULL);
        gtk_drop_down_set_factory(GTK_DROP_DOWN(app->pen_tool_dropdown), selected_factory);
        gtk_drop_down_set_list_factory(GTK_DROP_DOWN(app->pen_tool_dropdown), list_factory);
        gtk_drop_down_set_enable_search(GTK_DROP_DOWN(app->pen_tool_dropdown), FALSE);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->pen_tool_dropdown), app->pen_tool);
        gtk_widget_set_size_request(app->pen_tool_dropdown, 52, -1);
        gtk_widget_set_tooltip_text(app->pen_tool_dropdown, "Select annotation tool");
        g_signal_connect(app->pen_tool_dropdown, "notify::selected", G_CALLBACK(on_pen_tool_selected), app);
        gtk_box_append(GTK_BOX(bar), app->pen_tool_dropdown);
    }

    gtk_box_append(GTK_BOX(bar), create_width_control(app));
    append_sep(bar);

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
    append_sep(bar);

    app->pen_clear_button = make_icon_button(ICON_CLEAR, "Clear all annotations", "Clear",
                                             G_CALLBACK(on_pen_clear_clicked), app);
    app->btn_undo = make_icon_button(ICON_UNDO, "Undo annotation edit (Ctrl+Z)", "Undo",
                                     G_CALLBACK(on_undo_clicked), app);
    app->btn_redo = make_icon_button(ICON_REDO,
                                     "Redo annotation edit (Ctrl+Y / Ctrl+Shift+Z)",
                                     "Redo", G_CALLBACK(on_redo_clicked), app);
    gtk_box_append(GTK_BOX(bar), app->btn_undo);
    gtk_box_append(GTK_BOX(bar), app->btn_redo);
    gtk_box_append(GTK_BOX(bar), app->pen_clear_button);

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
    double dashes[] = {6.0 * w, 12.0 * w};
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
    } else if (a->type == ANNO_LINE || a->type == ANNO_LINE_DASH) {
        cairo_move_to(cr, a->x0, a->y0);
        cairo_line_to(cr, a->x1, a->y1);
        cairo_stroke(cr);
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

    if (a->type == ANNO_LINE || a->type == ANNO_LINE_DASH) {
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
    st.dashed = (app->pen_tool == PEN_TOOL_LINE_DASH ||
                 app->pen_tool == PEN_TOOL_BRUSH_DASH ||
                 app->pen_tool == PEN_TOOL_RECT_DASH);
    return st;
}

static void start_annotation_at_doc(App *app, double x, double y)
{
    if (!app || !app->annotations) return;

    AnnotationStyle st = current_annotation_style(app);
    Annotation *a = NULL;

    switch (app->pen_tool) {
        case PEN_TOOL_LINE:
            a = annotation_new(ANNO_LINE, &st);
            a->x0 = a->x1 = x;
            a->y0 = a->y1 = y;
            break;
        case PEN_TOOL_LINE_DASH:
            a = annotation_new(ANNO_LINE_DASH, &st);
            a->x0 = a->x1 = x;
            a->y0 = a->y1 = y;
            break;
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

    app->stretch_mode = TRUE;

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

static void draw_image_error_icon(cairo_t *cr, double x, double y, double size)
{
    GError *err = NULL;
    RsvgHandle *handle = rsvg_handle_new_from_data((const guint8 *)ICON_IMAGE_ERROR,
                                                   strlen(ICON_IMAGE_ERROR),
                                                   &err);
    if (!handle) {
        if (err) g_error_free(err);
        return;
    }

#if LIBRSVG_CHECK_VERSION(2,52,0)
    RsvgRectangle viewport = {x, y, size, size};
    if (!rsvg_handle_render_document(handle, cr, &viewport, &err)) {
        g_clear_error(&err);
    }
#else
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, size / 24.0, size / 24.0);
    rsvg_handle_render_cairo(handle, cr);
    cairo_restore(cr);
#endif
    g_object_unref(handle);
}

static void draw_empty_state(App *app, cairo_t *cr)
{
    const char *msg = str_empty(app->load_error)
        ? "No displayable SVG content."
        : app->load_error;
    double icon_size = 72.0;
    double x = (app->canvas_w - icon_size) * 0.5;
    double y = (app->canvas_h - icon_size) * 0.5 - 28.0;

    if (x < 8.0) x = 8.0;
    if (y < 8.0) y = 8.0;

    cairo_save(cr);
    draw_image_error_icon(cr, x, y, icon_size);
    cairo_set_source_rgba(cr, 0.18, 0.20, 0.24, 0.88);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14.0);

    cairo_text_extents_t ext;
    cairo_text_extents(cr, msg, &ext);
    double tx = (app->canvas_w - ext.width) * 0.5 - ext.x_bearing;
    if (tx < 8.0) tx = 8.0;
    cairo_move_to(cr, tx, y + icon_size + 26.0);
    cairo_show_text(cr, msg);
    cairo_restore(cr);
}

static void draw_canvas_layers(App *app, cairo_t *cr, gboolean include_interaction_overlays)
{
    draw_canvas_background(app, cr);

    if (!app || app->sequence.count <= 0) {
        if (app) draw_empty_state(app, cr);
        return;
    }

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
    if (include_interaction_overlays) {
        draw_zoom_box_overlay(app, cr);
        draw_eraser_cursor_overlay(app, cr);
    }
}

static void draw_canvas(App *app, cairo_t *cr)
{
    draw_canvas_layers(app, cr, TRUE);
}

static const char *export_type_for_path(const char *path, gboolean *needs_alpha)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    const char *ext = dot ? dot + 1 : "";

    if (needs_alpha) *needs_alpha = FALSE;

    if (g_ascii_strcasecmp(ext, "jpg") == 0 ||
        g_ascii_strcasecmp(ext, "jpeg") == 0) {
        return "jpeg";
    }
    if (g_ascii_strcasecmp(ext, "bmp") == 0) {
        return "bmp";
    }
    if (g_ascii_strcasecmp(ext, "tif") == 0 ||
        g_ascii_strcasecmp(ext, "tiff") == 0) {
        if (needs_alpha) *needs_alpha = TRUE;
        return "tiff";
    }

    if (needs_alpha) *needs_alpha = TRUE;
    return "png";
}

static gboolean is_svg_export_path(const gchar *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    const char *ext = dot ? dot + 1 : "";
    return g_ascii_strcasecmp(ext, "svg") == 0;
}

static gchar *normalize_export_path(const gchar *path)
{
    const char *dot;

    if (!path || !*path) return NULL;

    dot = strrchr(path, '.');
    if (!dot || dot == path || strchr(dot, G_DIR_SEPARATOR)) {
        return g_strconcat(path, ".png", NULL);
    }

    return g_strdup(path);
}

static gchar *normalize_export_path_for_extension(const gchar *path, const gchar *ext)
{
    gchar *base;
    gchar *dot;
    gchar *out;

    if (!ext || !*ext) return normalize_export_path(path);

    base = normalize_export_path(path);
    if (!base) return NULL;

    dot = strrchr(base, '.');
    if (dot && dot != base && !strchr(dot, G_DIR_SEPARATOR)) {
        *dot = '\0';
    }

    out = g_strconcat(base, ext, NULL);
    g_free(base);
    return out;
}

static GtkFileFilter *make_export_filter(const char *name,
                                         const char *pattern,
                                         const char *ext)
{
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, name);
    gtk_file_filter_add_pattern(filter, pattern);
    if (ext) g_object_set_data(G_OBJECT(filter), "rsfpy-ext", (gpointer)ext);
    return filter;
}

typedef struct {
    int x;
    int y;
    int width;
    int height;
} ImageRect;

static gboolean current_svg_screen_rect(App *app, ImageRect *rect)
{
    SvgFrame *f;
    ViewTransform tr;
    double x0, y0, x1, y1;
    int ix0, iy0, ix1, iy1;

    if (!app || !rect) return FALSE;
    f = current_frame(app);
    if (!f || !compute_view_transform(app, &tr)) return FALSE;

    x0 = tr.tx;
    y0 = tr.ty;
    x1 = tr.tx + f->width * tr.sx;
    y1 = tr.ty + f->height * tr.sy;
    if (x1 < x0) {
        double t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y1 < y0) {
        double t = y0;
        y0 = y1;
        y1 = t;
    }

    if (x0 < 0.0) x0 = 0.0;
    if (y0 < 0.0) y0 = 0.0;
    if (x1 > app->canvas_w) x1 = app->canvas_w;
    if (y1 > app->canvas_h) y1 = app->canvas_h;

    ix0 = (int)floor(x0);
    iy0 = (int)floor(y0);
    ix1 = (int)ceil(x1);
    iy1 = (int)ceil(y1);
    if (ix1 <= ix0 || iy1 <= iy0) return FALSE;

    rect->x = ix0;
    rect->y = iy0;
    rect->width = ix1 - ix0;
    rect->height = iy1 - iy0;
    return TRUE;
}

static gboolean current_visible_doc_rect(App *app,
                                         double *doc_x,
                                         double *doc_y,
                                         double *doc_w,
                                         double *doc_h,
                                         int *pixel_w,
                                         int *pixel_h)
{
    ImageRect rect;
    double dx0, dy0, dx1, dy1;
    double x0, x1, y0, y1;

    if (!current_svg_screen_rect(app, &rect)) return FALSE;
    if (!screen_to_doc(app, rect.x, rect.y, &dx0, &dy0)) return FALSE;
    if (!screen_to_doc(app, rect.x + rect.width, rect.y + rect.height, &dx1, &dy1)) return FALSE;

    x0 = dx0 < dx1 ? dx0 : dx1;
    x1 = dx0 < dx1 ? dx1 : dx0;
    y0 = dy0 < dy1 ? dy0 : dy1;
    y1 = dy0 < dy1 ? dy1 : dy0;
    if (x1 <= x0 || y1 <= y0) return FALSE;

    if (doc_x) *doc_x = x0;
    if (doc_y) *doc_y = y0;
    if (doc_w) *doc_w = x1 - x0;
    if (doc_h) *doc_h = y1 - y0;
    if (pixel_w) *pixel_w = rect.width;
    if (pixel_h) *pixel_h = rect.height;
    return TRUE;
}

static cairo_surface_t *render_current_svg_area_surface(App *app, GError **err)
{
    int width = app && app->canvas_w > 1 ? app->canvas_w : DEFAULT_WIDTH;
    int height = app && app->canvas_h > 1 ? app->canvas_h : DEFAULT_HEIGHT;
    ImageRect rect;
    cairo_surface_t *full;
    cairo_surface_t *crop;
    cairo_t *cr;

    if (!app) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "viewer is not ready");
        return NULL;
    }

    if (!current_svg_screen_rect(app, &rect)) {
        rect.x = 0;
        rect.y = 0;
        rect.width = width;
        rect.height = height;
    }

    full = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(full) != CAIRO_STATUS_SUCCESS) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to create render surface");
        cairo_surface_destroy(full);
        return NULL;
    }

    cr = cairo_create(full);
    draw_canvas_layers(app, cr, FALSE);
    cairo_destroy(cr);

    crop = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rect.width, rect.height);
    if (cairo_surface_status(crop) != CAIRO_STATUS_SUCCESS) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to create cropped surface");
        cairo_surface_destroy(full);
        cairo_surface_destroy(crop);
        return NULL;
    }

    cr = cairo_create(crop);
    cairo_set_source_surface(cr, full, -rect.x, -rect.y);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(full);
    return crop;
}

static gchar *default_export_filename(App *app)
{
    SvgFrame *f = current_frame(app);
    const char *src = f && f->path ? f->path : "rsfpy-view.svg";
    gchar *base = g_path_get_basename(src);
    gchar *frame_suffix = strstr(base, ".frame[");
    gchar *dot;
    gchar *out;

    if (frame_suffix) *frame_suffix = '\0';
    dot = strrchr(base, '.');
    if (dot && dot != base) *dot = '\0';

    out = g_strdup_printf("%s.svg", base && *base ? base : "rsfpy-view");
    g_free(base);
    return out;
}

static GdkPixbuf *surface_to_pixbuf(cairo_surface_t *surface, gboolean alpha)
{
    int width;
    int height;
    int stride;
    guint8 *src;
    GdkPixbuf *pixbuf;
    guint8 *dst;
    int dst_stride;
    int channels;

    if (!surface) return NULL;

    cairo_surface_flush(surface);
    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);
    stride = cairo_image_surface_get_stride(surface);
    src = cairo_image_surface_get_data(surface);
    if (width <= 0 || height <= 0 || !src) return NULL;

    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, alpha, 8, width, height);
    if (!pixbuf) return NULL;

    dst = gdk_pixbuf_get_pixels(pixbuf);
    dst_stride = gdk_pixbuf_get_rowstride(pixbuf);
    channels = gdk_pixbuf_get_n_channels(pixbuf);

    for (int y = 0; y < height; y++) {
        guint32 *row = (guint32 *)(src + y * stride);
        guint8 *out = dst + y * dst_stride;
        for (int x = 0; x < width; x++) {
            guint32 px = row[x];
            guint a = (px >> 24) & 0xff;
            guint r = (px >> 16) & 0xff;
            guint g = (px >> 8) & 0xff;
            guint b = px & 0xff;

            if (a > 0 && a < 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
            }

            out[x * channels + 0] = (guint8)r;
            out[x * channels + 1] = (guint8)g;
            out[x * channels + 2] = (guint8)b;
            if (alpha) out[x * channels + 3] = (guint8)a;
        }
    }

    return pixbuf;
}

static gboolean export_current_view(App *app, const gchar *path, GError **err)
{
    cairo_surface_t *surface;
    GdkPixbuf *pixbuf;
    gboolean alpha = FALSE;
    const char *type;
    gboolean ok;

    if (!app || !path || !*path) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "no export path selected");
        return FALSE;
    }

    if (is_svg_export_path(path)) {
        gchar *svg = NULL;
        gsize svg_len = 0;
        gboolean ok;

        svg = build_current_view_svg(app, &svg_len, err);
        if (!svg) return FALSE;

        ok = g_file_set_contents(path, svg, (gssize)svg_len, err);
        g_free(svg);
        return ok;
    }

    type = export_type_for_path(path, &alpha);
    surface = render_current_svg_area_surface(app, err);
    if (!surface) return FALSE;

    pixbuf = surface_to_pixbuf(surface, alpha);
    cairo_surface_destroy(surface);
    if (!pixbuf) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to create export image");
        return FALSE;
    }

    ok = gdk_pixbuf_save(pixbuf, path, type, err, NULL);
    g_object_unref(pixbuf);
    return ok;
}

static gboolean build_current_view_png(App *app,
                                       guint8 **out_png,
                                       gsize *out_len,
                                       GError **err)
{
    cairo_surface_t *surface;
    GdkPixbuf *pixbuf;
    gchar *buffer = NULL;
    gsize len = 0;
    gboolean ok;

    if (out_png) *out_png = NULL;
    if (out_len) *out_len = 0;
    if (!out_png || !out_len) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "invalid PNG output buffer");
        return FALSE;
    }

    surface = render_current_svg_area_surface(app, err);
    if (!surface) return FALSE;

    pixbuf = surface_to_pixbuf(surface, FALSE);
    cairo_surface_destroy(surface);
    if (!pixbuf) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to create clipboard PNG");
        return FALSE;
    }

    ok = gdk_pixbuf_save_to_buffer(pixbuf, &buffer, &len, "png", err, NULL);
    g_object_unref(pixbuf);
    if (!ok) return FALSE;

    *out_png = (guint8 *)buffer;
    *out_len = len;
    return TRUE;
}

typedef struct {
    App *app;
    GtkFileChooserNative *dialog;
} ExportDialogState;

typedef struct {
    App *app;
    gchar *path;
} ExportJob;

static void export_job_free(ExportJob *job)
{
    if (!job) return;
    g_free(job->path);
    g_free(job);
}

static void export_task_thread(GTask *task,
                               gpointer source_object,
                               gpointer task_data,
                               GCancellable *cancellable)
{
    (void)source_object;
    (void)cancellable;
    ExportJob *job = task_data;
    GError *err = NULL;

    if (job && job->app && export_current_view(job->app, job->path, &err)) {
        g_task_return_boolean(task, TRUE);
    } else {
        if (!err) {
            g_set_error(&err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "export failed");
        }
        g_task_return_error(task, err);
    }
}

static void on_export_task_done(GObject *source_object,
                                GAsyncResult *result,
                                gpointer user_data)
{
    (void)source_object;
    (void)user_data;
    GTask *task = G_TASK(result);
    ExportJob *job = g_task_get_task_data(task);
    GError *err = NULL;

    if (g_task_propagate_boolean(task, &err)) {
        gchar *msg = g_strdup_printf("Exported view: %s",
                                     job && job->path ? job->path : "");
        set_busy(job ? job->app : NULL, FALSE, msg);
        g_free(msg);
    } else {
        gchar *msg = g_strdup_printf("Export failed: %s",
                                     err ? err->message : "unknown error");
        set_busy(job ? job->app : NULL, FALSE, msg);
        g_printerr("%s\n", msg);
        g_free(msg);
        if (err) g_error_free(err);
    }
}

static void start_export_task(App *app, const gchar *path)
{
    ExportJob *job;
    GTask *task;
    gchar *msg;

    if (!app || !path || !*path || app->busy) return;

    job = g_new0(ExportJob, 1);
    job->app = app;
    job->path = g_strdup(path);

    msg = g_strdup_printf("Exporting view: %s", path);
    set_busy(app, TRUE, msg);
    g_free(msg);

    task = g_task_new(NULL, NULL, on_export_task_done, NULL);
    g_task_set_task_data(task, job, (GDestroyNotify)export_job_free);
    g_task_run_in_thread(task, export_task_thread);
    g_object_unref(task);
}

static void on_export_dialog_response(GtkNativeDialog *dialog,
                                      int response,
                                      gpointer user_data)
{
    ExportDialogState *st = user_data;

    if (st && response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        gchar *raw_path = file ? g_file_get_path(file) : NULL;
        GtkFileFilter *filter = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(dialog));
        const gchar *ext = filter ? g_object_get_data(G_OBJECT(filter), "rsfpy-ext") : NULL;
        gchar *path = normalize_export_path_for_extension(raw_path, ext);

        if (path && !st->app->busy) {
            start_export_task(st->app, path);
        } else if (st->app->status_label) {
            gtk_label_set_text(GTK_LABEL(st->app->status_label),
                               "Export is already running.");
        }

        g_free(path);
        g_free(raw_path);
        if (file) g_object_unref(file);
    }

    if (st) {
        g_object_unref(st->dialog);
        g_free(st);
    }
}

static void on_export_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    App *app = user_data;
    ExportDialogState *st;
    GtkFileChooserNative *dialog;

    if (!app || !app->window) return;
    if (app->busy) {
        if (app->status_label) {
            gtk_label_set_text(GTK_LABEL(app->status_label),
                               "Export is already running.");
        }
        return;
    }

    dialog = gtk_file_chooser_native_new("Export Current View",
                                         GTK_WINDOW(app->window),
                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         "_Export",
                                         "_Cancel");
    gchar *default_name = default_export_filename(app);
    GtkFileFilter *filter_svg = make_export_filter("SVG with annotations (*.svg)", "*.svg", ".svg");
    GtkFileFilter *filter_png = make_export_filter("PNG image (*.png)", "*.png", ".png");
    GtkFileFilter *filter_jpg = make_export_filter("JPEG image (*.jpg)", "*.jpg", ".jpg");
    GtkFileFilter *filter_jpeg = make_export_filter("JPEG image (*.jpeg)", "*.jpeg", ".jpeg");
    GtkFileFilter *filter_bmp = make_export_filter("BMP image (*.bmp)", "*.bmp", ".bmp");
    GtkFileFilter *filter_tiff = make_export_filter("TIFF image (*.tiff)", "*.tiff", ".tiff");

    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_svg);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_png);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_jpg);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_jpeg);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_bmp);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_tiff);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), filter_svg);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), default_name);
    g_free(default_name);

    st = g_new0(ExportDialogState, 1);
    st->app = app;
    st->dialog = g_object_ref(dialog);
    g_signal_connect(dialog, "response", G_CALLBACK(on_export_dialog_response), st);

    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
    g_object_unref(dialog);
}

static gboolean read_current_svg_source(App *app, gchar **out, gsize *out_len, GError **err)
{
    SvgFrame *f = current_frame(app);
    FILE *fp;
    size_t nr;

    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!f || !out || !out_len) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "no SVG source is available");
        return FALSE;
    }

    if (f->source_bytes) {
        gsize total_len = 0;
        const guint8 *bytes = g_bytes_get_data(f->source_bytes, &total_len);
        if (!bytes || f->data_offset < 0 || (guint64)f->data_offset > (guint64)total_len ||
            f->data_len > total_len - (gsize)f->data_offset) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "invalid in-memory SVG range");
            return FALSE;
        }

        *out = g_malloc(f->data_len + 1);
        memcpy(*out, bytes + f->data_offset, f->data_len);
        (*out)[f->data_len] = '\0';
        *out_len = f->data_len;
        return TRUE;
    }

    if (!f->source_path) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "no SVG source path is available");
        return FALSE;
    }

    if (!f->use_range) {
        return g_file_get_contents(f->source_path, out, out_len, err);
    }

    fp = fopen(f->source_path, "rb");
    if (!fp) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to open SVG source: %s", f->source_path);
        return FALSE;
    }
#if defined(_WIN32)
    if (fseek(fp, (long)f->data_offset, SEEK_SET) != 0) {
#else
    if (fseeko(fp, (off_t)f->data_offset, SEEK_SET) != 0) {
#endif
        fclose(fp);
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to seek SVG source: %s", f->source_path);
        return FALSE;
    }

    *out = g_malloc(f->data_len + 1);
    nr = fread(*out, 1, f->data_len, fp);
    fclose(fp);
    if (nr != f->data_len) {
        g_free(*out);
        *out = NULL;
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to read complete SVG frame");
        return FALSE;
    }

    (*out)[f->data_len] = '\0';
    *out_len = f->data_len;
    return TRUE;
}

static void append_svg_stroke_attrs(GString *s, const Annotation *a)
{
    int r, g, b;
    if (!s || !a) return;

    r = (int)(a->style.color.red * 255.0 + 0.5);
    g = (int)(a->style.color.green * 255.0 + 0.5);
    b = (int)(a->style.color.blue * 255.0 + 0.5);
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;

    g_string_append_printf(s,
                           " fill=\"none\" stroke=\"#%02x%02x%02x\" stroke-opacity=\"%.3g\" stroke-width=\"%.6g\" stroke-linecap=\"round\" stroke-linejoin=\"round\"",
                           r, g, b,
                           a->style.color.alpha,
                           a->style.width_doc > 0.0 ? a->style.width_doc : 1.0);
    if (a->style.dashed) {
        double w = a->style.width_doc > 0.0 ? a->style.width_doc : 1.0;
        g_string_append_printf(s, " stroke-dasharray=\"%.6g %.6g\"", 6.0 * w, 12.0 * w);
    }
}

static void append_annotations_as_svg(App *app, GString *s)
{
    if (!app || !s || !app->annotations || app->annotations->len == 0) return;

    g_string_append(s, "\n<g id=\"rsfpy-annotations\">\n");
    for (guint i = 0; i < app->annotations->len; i++) {
        Annotation *a = g_ptr_array_index(app->annotations, i);
        if (!annotation_is_valid(a)) continue;

        if (a->type == ANNO_BRUSH || a->type == ANNO_BRUSH_DASH) {
            DocPoint *pts = (DocPoint *)a->points->data;
            g_string_append(s, "<path d=\"");
            g_string_append_printf(s, "M %.6g %.6g", pts[0].x, pts[0].y);
            for (guint j = 1; j < a->points->len; j++) {
                g_string_append_printf(s, " L %.6g %.6g", pts[j].x, pts[j].y);
            }
            g_string_append_c(s, '"');
            append_svg_stroke_attrs(s, a);
            g_string_append(s, "/>\n");
        } else if (a->type == ANNO_RECT || a->type == ANNO_RECT_DASH) {
            double x = a->x0 < a->x1 ? a->x0 : a->x1;
            double y = a->y0 < a->y1 ? a->y0 : a->y1;
            double w = fabs(a->x1 - a->x0);
            double h = fabs(a->y1 - a->y0);
            g_string_append_printf(s, "<rect x=\"%.6g\" y=\"%.6g\" width=\"%.6g\" height=\"%.6g\"",
                                   x, y, w, h);
            append_svg_stroke_attrs(s, a);
            g_string_append(s, "/>\n");
        } else {
            g_string_append_printf(s, "<path d=\"M %.6g %.6g L %.6g %.6g\"",
                                   a->x0, a->y0, a->x1, a->y1);
            append_svg_stroke_attrs(s, a);
            g_string_append(s, "/>\n");

            if (a->type == ANNO_ARROW) {
                double dx = a->x1 - a->x0;
                double dy = a->y1 - a->y0;
                double len = local_sqrt(dx * dx + dy * dy);
                if (len > 1e-12) {
                    double ux = dx / len;
                    double uy = dy / len;
                    double px = -uy;
                    double py = ux;
                    double size = a->style.width_doc * 6.0;
                    double bx = a->x1 - ux * size;
                    double by = a->y1 - uy * size;
                    double lx = bx + px * size * 0.45;
                    double ly = by + py * size * 0.45;
                    double rx = bx - px * size * 0.45;
                    double ry = by - py * size * 0.45;
                    g_string_append_printf(s,
                                           "<path d=\"M %.6g %.6g L %.6g %.6g M %.6g %.6g L %.6g %.6g\"",
                                           a->x1, a->y1, lx, ly,
                                           a->x1, a->y1, rx, ry);
                    append_svg_stroke_attrs(s, a);
                    g_string_append(s, "/>\n");
                }
            }
        }
    }
    g_string_append(s, "</g>\n");
}

static const gchar *find_svg_tag_end(const gchar *tag)
{
    gboolean in_quote = FALSE;
    gchar quote = '\0';

    if (!tag) return NULL;
    for (const gchar *p = tag; *p; p++) {
        if (in_quote) {
            if (*p == quote) in_quote = FALSE;
        } else if (*p == '"' || *p == '\'') {
            in_quote = TRUE;
            quote = *p;
        } else if (*p == '>') {
            return p;
        }
    }
    return NULL;
}

static gboolean attr_name_matches_at(const gchar *p,
                                     const gchar *end,
                                     const gchar *name)
{
    gsize n;
    if (!p || !end || !name) return FALSE;
    n = strlen(name);
    if ((gsize)(end - p) < n || strncmp(p, name, n) != 0) return FALSE;
    return p + n < end && p[n] == '=';
}

static void append_tag_without_attr(GString *out,
                                    const gchar *tag_start,
                                    const gchar *tag_end,
                                    const gchar *attr_name)
{
    const gchar *p = tag_start;
    gsize attr_len;

    if (!out || !tag_start || !tag_end || tag_end < tag_start || !attr_name) return;
    attr_len = strlen(attr_name);

    while (p < tag_end) {
        const gchar *hit = g_strstr_len(p, (gssize)(tag_end - p), attr_name);
        const gchar *attr_start;
        const gchar *attr_end;

        if (!hit) break;
        if (hit > tag_start && !g_ascii_isspace((guchar)hit[-1])) {
            p = hit + attr_len;
            continue;
        }
        if (!attr_name_matches_at(hit, tag_end, attr_name)) {
            p = hit + attr_len;
            continue;
        }

        attr_start = hit;
        while (attr_start > tag_start && g_ascii_isspace((guchar)attr_start[-1])) attr_start--;
        attr_end = hit + attr_len + 1;
        if (attr_end < tag_end && (*attr_end == '"' || *attr_end == '\'')) {
            gchar quote = *attr_end++;
            while (attr_end < tag_end && *attr_end != quote) attr_end++;
            if (attr_end < tag_end) attr_end++;
        } else {
            while (attr_end < tag_end &&
                   !g_ascii_isspace((guchar)*attr_end) &&
                   *attr_end != '>') {
                attr_end++;
            }
        }

        g_string_append_len(out, p, (gssize)(attr_start - p));
        p = attr_end;
    }
    g_string_append_len(out, p, (gssize)(tag_end - p));
}

static gchar *sanitize_svg_for_office_image_clip(const gchar *svg)
{
    GString *out;
    const gchar *p;

    if (!svg) return NULL;
    out = g_string_new(NULL);
    p = svg;

    while (*p) {
        if (g_str_has_prefix(p, "<image")) {
            const gchar *tag_end = find_svg_tag_end(p);
            if (!tag_end) break;
            append_tag_without_attr(out, p, tag_end + 1, "clip-path");
            p = tag_end + 1;
        } else if (g_str_has_prefix(p, "<g")) {
            const gchar *tag_end = find_svg_tag_end(p);
            const gchar *group_end;
            const gchar *image_in_group;

            if (!tag_end) break;
            group_end = g_strstr_len(tag_end + 1, -1, "</g>");
            image_in_group = group_end
                ? g_strstr_len(tag_end + 1, (gssize)(group_end - tag_end - 1), "<image")
                : NULL;

            if (image_in_group) {
                append_tag_without_attr(out, p, tag_end + 1, "clip-path");
            } else {
                g_string_append_len(out, p, (gssize)(tag_end + 1 - p));
            }
            p = tag_end + 1;
        } else {
            g_string_append_c(out, *p++);
        }
    }

    if (*p) g_string_append(out, p);
    return g_string_free(out, FALSE);
}

static gchar *build_current_view_svg(App *app, gsize *out_len, GError **err)
{
    gchar *src = NULL;
    gsize src_len = 0;
    gchar *close_tag;
    gchar *sanitized;
    GString *overlay;
    GString *merged;
    double doc_x, doc_y, doc_w, doc_h;
    int pixel_w, pixel_h;

    if (out_len) *out_len = 0;
    if (!read_current_svg_source(app, &src, &src_len, err)) return NULL;

    overlay = g_string_new(NULL);
    append_annotations_as_svg(app, overlay);

    close_tag = g_strrstr(src, "</svg");
    if (!close_tag) {
        merged = g_string_new_len(src, src_len);
        g_string_append(merged, overlay->str);
    } else {
        gsize prefix_len = (gsize)(close_tag - src);
        merged = g_string_new_len(src, prefix_len);
        g_string_append(merged, overlay->str);
        g_string_append(merged, close_tag);
    }

    g_string_free(overlay, TRUE);
    g_free(src);

    sanitized = sanitize_svg_for_office_image_clip(merged->str);
    if (sanitized) {
        g_string_free(merged, TRUE);
        merged = g_string_new(sanitized);
        g_free(sanitized);
    }

    if (current_visible_doc_rect(app, &doc_x, &doc_y, &doc_w, &doc_h, &pixel_w, &pixel_h)) {
        const gchar *svg_start = g_strstr_len(merged->str, (gssize)merged->len, "<svg");
        const gchar *open_end = find_svg_tag_end(svg_start);
        const gchar *svg_close = g_strrstr(merged->str, "</svg");

        if (svg_start && open_end && svg_close && svg_close > open_end) {
            const gchar *content_start = open_end + 1;
            gsize content_len = (gsize)(svg_close - content_start);
            GString *cropped = g_string_new(NULL);

            g_string_append_printf(cropped,
                                   "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                                   "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                                   "width=\"%d\" height=\"%d\" "
                                   "viewBox=\"%.12g %.12g %.12g %.12g\" "
                                   "style=\"overflow:hidden\">\n",
                                   pixel_w, pixel_h, doc_x, doc_y, doc_w, doc_h);
            g_string_append_len(cropped, content_start, content_len);
            g_string_append(cropped, "\n</svg>\n");

            g_string_free(merged, TRUE);
            if (out_len) *out_len = cropped->len;
            return g_string_free(cropped, FALSE);
        }
    }

    if (out_len) *out_len = merged->len;
    return g_string_free(merged, FALSE);
}

#ifdef G_OS_WIN32
static HGLOBAL win32_clipboard_mem_from_bytes(const void *data, SIZE_T len)
{
    HGLOBAL handle;
    void *dst;

    handle = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!handle) return NULL;

    dst = GlobalLock(handle);
    if (!dst) {
        GlobalFree(handle);
        return NULL;
    }
    memcpy(dst, data, len);
    GlobalUnlock(handle);
    return handle;
}

static HGLOBAL win32_clipboard_dib_from_current_view(App *app, GError **err)
{
    cairo_surface_t *surface;
    GdkPixbuf *pixbuf;
    HGLOBAL handle;
    BITMAPINFOHEADER *header;
    guint8 *bits;
    const guint8 *src;
    int width;
    int height;
    int src_stride;
    int channels;
    SIZE_T row_stride;
    SIZE_T image_size;
    SIZE_T total_size;

    surface = render_current_svg_area_surface(app, err);
    if (!surface) return NULL;

    pixbuf = surface_to_pixbuf(surface, FALSE);
    cairo_surface_destroy(surface);
    if (!pixbuf) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to create clipboard bitmap");
        return NULL;
    }

    width = gdk_pixbuf_get_width(pixbuf);
    height = gdk_pixbuf_get_height(pixbuf);
    src_stride = gdk_pixbuf_get_rowstride(pixbuf);
    channels = gdk_pixbuf_get_n_channels(pixbuf);
    src = gdk_pixbuf_get_pixels(pixbuf);
    if (width <= 0 || height <= 0 || !src || channels < 3) {
        g_object_unref(pixbuf);
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "invalid clipboard bitmap");
        return NULL;
    }

    row_stride = ((SIZE_T)width * 3 + 3) & ~(SIZE_T)3;
    image_size = row_stride * (SIZE_T)height;
    total_size = sizeof(BITMAPINFOHEADER) + image_size;
    handle = GlobalAlloc(GMEM_MOVEABLE, total_size);
    if (!handle) {
        g_object_unref(pixbuf);
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to allocate clipboard bitmap");
        return NULL;
    }

    header = (BITMAPINFOHEADER *)GlobalLock(handle);
    if (!header) {
        GlobalFree(handle);
        g_object_unref(pixbuf);
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to lock clipboard bitmap");
        return NULL;
    }

    memset(header, 0, total_size);
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = width;
    header->biHeight = height;
    header->biPlanes = 1;
    header->biBitCount = 24;
    header->biCompression = BI_RGB;
    header->biSizeImage = (DWORD)image_size;

    bits = (guint8 *)(header + 1);
    for (int y = 0; y < height; y++) {
        const guint8 *in = src + y * src_stride;
        guint8 *out = bits + (height - 1 - y) * row_stride;
        for (int x = 0; x < width; x++) {
            out[x * 3 + 0] = in[x * channels + 2];
            out[x * 3 + 1] = in[x * channels + 1];
            out[x * 3 + 2] = in[x * channels + 0];
        }
    }

    GlobalUnlock(handle);
    g_object_unref(pixbuf);
    return handle;
}

static gboolean win32_copy_svg_text_to_clipboard(const gchar *svg,
                                                 gsize svg_len,
                                                 const guint8 *png,
                                                 gsize png_len,
                                                 HGLOBAL dib_handle,
                                                 GError **err)
{
    gunichar2 *wide = NULL;
    glong wide_len = 0;
    HGLOBAL text_handle = NULL;
    HGLOBAL svg_handle = NULL;
    HGLOBAL png_handle = NULL;
    HGLOBAL png_mime_handle = NULL;
    UINT svg_format;
    UINT png_format;
    UINT png_mime_format;
    gboolean copied_text = FALSE;
    gboolean copied_svg = FALSE;
    gboolean copied_png = FALSE;
    gboolean copied_dib = FALSE;

    if (!svg) {
        if (dib_handle) GlobalFree(dib_handle);
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "no SVG data to copy");
        return FALSE;
    }

    wide = g_utf8_to_utf16(svg, (glong)svg_len, NULL, &wide_len, err);
    if (!wide) {
        if (dib_handle) GlobalFree(dib_handle);
        return FALSE;
    }

    text_handle = win32_clipboard_mem_from_bytes(
        wide, (SIZE_T)(wide_len + 1) * sizeof(gunichar2));
    svg_handle = win32_clipboard_mem_from_bytes(
        svg, (SIZE_T)svg_len + 1);
    if (png && png_len > 0) {
        png_handle = win32_clipboard_mem_from_bytes(png, (SIZE_T)png_len);
        png_mime_handle = win32_clipboard_mem_from_bytes(png, (SIZE_T)png_len);
    }
    g_free(wide);

    if (!text_handle && !svg_handle && !png_handle &&
        !png_mime_handle && !dib_handle) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to allocate clipboard data");
        return FALSE;
    }

    if (!OpenClipboard(NULL)) {
        if (text_handle) GlobalFree(text_handle);
        if (svg_handle) GlobalFree(svg_handle);
        if (png_handle) GlobalFree(png_handle);
        if (png_mime_handle) GlobalFree(png_mime_handle);
        if (dib_handle) GlobalFree(dib_handle);
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to open Windows clipboard");
        return FALSE;
    }

    EmptyClipboard();

    if (text_handle && SetClipboardData(CF_UNICODETEXT, text_handle)) {
        text_handle = NULL;
        copied_text = TRUE;
    }

    svg_format = RegisterClipboardFormatW(L"image/svg+xml");
    if (svg_format && svg_handle && SetClipboardData(svg_format, svg_handle)) {
        svg_handle = NULL;
        copied_svg = TRUE;
    }

    png_format = RegisterClipboardFormatW(L"PNG");
    if (png_format && png_handle && SetClipboardData(png_format, png_handle)) {
        png_handle = NULL;
        copied_png = TRUE;
    }

    png_mime_format = RegisterClipboardFormatW(L"image/png");
    if (png_mime_format && png_mime_handle &&
        SetClipboardData(png_mime_format, png_mime_handle)) {
        png_mime_handle = NULL;
        copied_png = TRUE;
    }

    if (dib_handle && SetClipboardData(CF_DIB, dib_handle)) {
        dib_handle = NULL;
        copied_dib = TRUE;
    }

    CloseClipboard();

    if (text_handle) GlobalFree(text_handle);
    if (svg_handle) GlobalFree(svg_handle);
    if (png_handle) GlobalFree(png_handle);
    if (png_mime_handle) GlobalFree(png_mime_handle);
    if (dib_handle) GlobalFree(dib_handle);

    if (!copied_text && !copied_svg && !copied_png && !copied_dib) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to set Windows clipboard data");
        return FALSE;
    }

    return TRUE;
}
#endif

static GdkTexture *make_current_view_texture(App *app, GError **err)
{
    cairo_surface_t *surface;
    GdkPixbuf *pixbuf;
    GdkTexture *texture;

    surface = render_current_svg_area_surface(app, err);
    if (!surface) return NULL;
    pixbuf = surface_to_pixbuf(surface, FALSE);
    cairo_surface_destroy(surface);
    if (!pixbuf) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to create clipboard image");
        return NULL;
    }

    texture = gdk_texture_new_for_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    return texture;
}

static gboolean copy_current_view_texture_to_clipboard(App *app, GError **err)
{
    GdkDisplay *display = gdk_display_get_default();
    GdkClipboard *clipboard = display ? gdk_display_get_clipboard(display) : NULL;
    GdkTexture *texture;

    if (!clipboard) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "clipboard is not available");
        return FALSE;
    }

    texture = make_current_view_texture(app, err);
    if (!texture) return FALSE;

    gdk_clipboard_set_texture(clipboard, texture);
    g_object_unref(texture);
    return TRUE;
}

typedef struct {
    App *app;
    GtkWidget *dialog;
} ClipboardWarnState;

static void on_clipboard_warn_response(GtkDialog *dialog,
                                       int response,
                                       gpointer user_data)
{
    ClipboardWarnState *st = user_data;
    if (st && response == GTK_RESPONSE_ACCEPT) {
        GError *err = NULL;
        if (copy_current_view_texture_to_clipboard(st->app, &err)) {
            gtk_label_set_text(GTK_LABEL(st->app->status_label),
                               "Copied current SVG area as an image.");
        } else {
            gchar *msg = g_strdup_printf("Copy failed: %s", err ? err->message : "unknown error");
            gtk_label_set_text(GTK_LABEL(st->app->status_label), msg);
            g_printerr("%s\n", msg);
            g_free(msg);
        }
        if (err) g_error_free(err);
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_free(st);
}

static void show_clipboard_size_warning(App *app)
{
    ClipboardWarnState *st;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *label;

    if (!app || !app->window) return;

    dialog = gtk_dialog_new_with_buttons("Clipboard SVG is large",
                                         GTK_WINDOW(app->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "Copy image",
                                         GTK_RESPONSE_ACCEPT,
                                         "_Cancel",
                                         GTK_RESPONSE_CANCEL,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    label = gtk_label_new("The SVG clipboard payload is large. Copy a raster image instead?");
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 12);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(content), label);

    st = g_new0(ClipboardWarnState, 1);
    st->app = app;
    st->dialog = dialog;
    g_signal_connect(dialog, "response", G_CALLBACK(on_clipboard_warn_response), st);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void copy_current_view_to_clipboard(App *app)
{
#ifdef G_OS_WIN32
    gchar *svg = NULL;
    guint8 *png = NULL;
    gsize svg_len = 0;
    gsize png_len = 0;
    HGLOBAL dib_handle = NULL;
    GError *err = NULL;

    if (!app) return;

    svg = build_current_view_svg(app, &svg_len, &err);
    if (err) {
        g_error_free(err);
        err = NULL;
    }
    if (!build_current_view_png(app, &png, &png_len, &err)) {
        g_clear_error(&err);
    }
    dib_handle = win32_clipboard_dib_from_current_view(app, &err);
    if (!dib_handle) {
        g_clear_error(&err);
    }

    if (svg && win32_copy_svg_text_to_clipboard(svg, svg_len,
                                                png, png_len,
                                                dib_handle, &err)) {
        const gchar *msg = svg_len > CLIPBOARD_SVG_WARN_BYTES
            ? "Copied large SVG plus PNG fallback to clipboard."
            : "Copied SVG plus PNG fallback to clipboard.";
        gtk_label_set_text(GTK_LABEL(app->status_label), msg);
        g_free(svg);
        g_free(png);
        if (err) g_error_free(err);
        return;
    }

    if (!svg && dib_handle) {
        GlobalFree(dib_handle);
        dib_handle = NULL;
    }
    g_free(svg);
    g_free(png);
    {
        gchar *msg = g_strdup_printf("Copy failed: %s",
                                     err ? err->message : "unknown error");
        gtk_label_set_text(GTK_LABEL(app->status_label), msg);
        g_printerr("%s\n", msg);
        g_free(msg);
    }
    if (err) g_error_free(err);
#elif defined(__APPLE__) && defined(__MACH__)
    gchar *svg = NULL;
    guint8 *png = NULL;
    gsize svg_len = 0;
    gsize png_len = 0;
    GError *err = NULL;

    if (!app) return;

    svg = build_current_view_svg(app, &svg_len, &err);
    if (err) {
        g_error_free(err);
        err = NULL;
    }
    if (!build_current_view_png(app, &png, &png_len, &err)) {
        g_clear_error(&err);
    }

    if (svg && rsfpy_macos_copy_svg_text_to_clipboard(svg, svg_len,
                                                      png, png_len,
                                                      &err)) {
        const gchar *msg = svg_len > CLIPBOARD_SVG_WARN_BYTES
            ? "Copied large SVG plus PNG fallback to clipboard."
            : "Copied SVG plus PNG fallback to clipboard.";
        gtk_label_set_text(GTK_LABEL(app->status_label), msg);
        g_free(svg);
        g_free(png);
        if (err) g_error_free(err);
        return;
    }

    g_free(svg);
    g_free(png);
    {
        gchar *msg = g_strdup_printf("Copy failed: %s",
                                     err ? err->message : "unknown error");
        gtk_label_set_text(GTK_LABEL(app->status_label), msg);
        g_printerr("%s\n", msg);
        g_free(msg);
    }
    if (err) g_error_free(err);
#else
    GdkDisplay *display = gdk_display_get_default();
    GdkClipboard *clipboard = display ? gdk_display_get_clipboard(display) : NULL;
    gchar *svg = NULL;
    gsize svg_len = 0;
    GError *err = NULL;

    if (!app || !clipboard) {
        if (app && app->status_label) {
            gtk_label_set_text(GTK_LABEL(app->status_label), "Copy failed: clipboard is not available.");
        }
        return;
    }

    svg = build_current_view_svg(app, &svg_len, &err);
    if (svg && svg_len <= CLIPBOARD_SVG_WARN_BYTES) {
        gdk_clipboard_set_text(clipboard, svg);
        gtk_label_set_text(GTK_LABEL(app->status_label),
                           "Copied SVG text with annotations to clipboard.");
        if (err) g_error_free(err);
        g_free(svg);
        return;
    } else if (svg) {
        g_free(svg);
        show_clipboard_size_warning(app);
        if (err) g_error_free(err);
        return;
    }

    g_free(svg);
    if (err) {
        g_error_free(err);
        err = NULL;
    }
    if (copy_current_view_texture_to_clipboard(app, &err)) {
        gtk_label_set_text(GTK_LABEL(app->status_label),
                           "Copied current SVG area as an image.");
    } else {
        gchar *msg = g_strdup_printf("Copy failed: %s", err ? err->message : "unknown error");
        gtk_label_set_text(GTK_LABEL(app->status_label), msg);
        g_printerr("%s\n", msg);
        g_free(msg);
    }
    if (err) g_error_free(err);
#endif
}

static void on_copy_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    copy_current_view_to_clipboard((App *)user_data);
}

static void on_quit_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    App *app = user_data;
    if (app && app->window) gtk_window_close(GTK_WINDOW(app->window));
}

static gboolean on_window_close_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    App *app = user_data;
    if (app && app->busy) {
        if (app->status_label) {
            gtk_label_set_text(GTK_LABEL(app->status_label),
                               "Export is still running; please wait.");
        }
        return TRUE;
    }
    return FALSE;
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

#if defined(__APPLE__) && defined(__MACH__)
    if ((state & GDK_META_MASK) &&
        (keyval == GDK_KEY_c || keyval == GDK_KEY_C)) {
        copy_current_view_to_clipboard(app);
        return TRUE;
    }
#endif

    if (state & GDK_CONTROL_MASK) {
#ifdef G_OS_WIN32
        if (keyval == GDK_KEY_c || keyval == GDK_KEY_C) {
            copy_current_view_to_clipboard(app);
            return TRUE;
        }
#endif
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

typedef enum {
    CTX_PREV = 0,
    CTX_NEXT,
    CTX_SLOWER,
    CTX_PLAY,
    CTX_PAUSE,
    CTX_FASTER,
    CTX_EXPORT,
    CTX_COPY,
    CTX_PAN,
    CTX_ZOOM,
    CTX_RESET,
    CTX_CLEAR,
    CTX_QUIT
} ContextAction;

static void on_context_action_clicked(GtkButton *button, gpointer user_data)
{
    App *app = user_data;
    ContextAction action = (ContextAction)GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(button), "rsfpy-context-action"));

    switch (action) {
        case CTX_PREV:
            on_prev_clicked(button, app);
            break;
        case CTX_NEXT:
            on_next_clicked(button, app);
            break;
        case CTX_SLOWER:
            on_slower_clicked(button, app);
            break;
        case CTX_PLAY:
            on_run_clicked(button, app);
            break;
        case CTX_PAUSE:
            on_pause_clicked(button, app);
            break;
        case CTX_FASTER:
            on_faster_clicked(button, app);
            break;
        case CTX_EXPORT:
            on_export_clicked(button, app);
            break;
        case CTX_COPY:
            copy_current_view_to_clipboard(app);
            break;
        case CTX_PAN:
            set_tool(app, TOOL_PAN);
            break;
        case CTX_ZOOM:
            set_tool(app, TOOL_ZOOM);
            break;
        case CTX_RESET:
            reset_view(app);
            break;
        case CTX_CLEAR:
            on_pen_clear_clicked(button, app);
            break;
        case CTX_QUIT:
            if (app && app->window) gtk_window_close(GTK_WINDOW(app->window));
            break;
    }

    GtkWidget *popover = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "rsfpy-context-popover"));
    if (popover) gtk_popover_popdown(GTK_POPOVER(popover));
}

static GtkWidget *make_context_button(App *app,
                                      GtkWidget *popover,
                                      const char *label,
                                      const char *icon,
                                      ContextAction action,
                                      gboolean sensitive)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *text = gtk_label_new(label);

    gtk_widget_set_halign(text, GTK_ALIGN_START);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_box_append(GTK_BOX(box), make_icon_child(icon, label));
    gtk_box_append(GTK_BOX(box), text);
    gtk_button_set_child(GTK_BUTTON(button), box);
    gtk_widget_set_focus_on_click(button, FALSE);
    gtk_widget_set_sensitive(button, sensitive);
    if (action == CTX_QUIT) {
        gtk_widget_add_css_class(button, "destructive-action");
        gtk_widget_add_css_class(button, "rsfpy-quit");
    }
    g_object_set_data(G_OBJECT(button), "rsfpy-context-action", GINT_TO_POINTER(action));
    g_object_set_data(G_OBJECT(button), "rsfpy-context-popover", popover);
    g_signal_connect(button, "clicked", G_CALLBACK(on_context_action_clicked), app);
    return button;
}

static void append_context_sep(GtkWidget *box)
{
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 4);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_box_append(GTK_BOX(box), sep);
}

static void show_context_menu(App *app, double x, double y)
{
    GtkWidget *popover;
    GtkWidget *box;
    GdkRectangle rect;
    gboolean multi;
    gboolean has_frame;
    gboolean has_annotations;
    gboolean can_reset;

    if (!app || !app->area) return;

    multi = app->sequence.count > 1;
    has_frame = current_frame(app) != NULL;
    has_annotations = app->annotations && app->annotations->len > 0;
    can_reset = (app->zoom_scale != 1.0 || app->pan_x != 0.0 || app->pan_y != 0.0);

    popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_parent(popover, app->area);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Previous", ICON_PREV,
                                                     CTX_PREV, multi));
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Next", ICON_NEXT,
                                                     CTX_NEXT, multi));
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Slower", ICON_SLOWER,
                                                     CTX_SLOWER, multi && app->sequence.fps > MIN_FPS));
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Play", ICON_PLAY,
                                                     CTX_PLAY, multi && !app->sequence.playing));
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Pause", ICON_PAUSE,
                                                     CTX_PAUSE, multi && app->sequence.playing));
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Faster", ICON_FASTER,
                                                     CTX_FASTER, multi && app->sequence.fps < MAX_FPS));
    append_context_sep(box);
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Export", ICON_EXPORT,
                                                     CTX_EXPORT, has_frame));
#if defined(G_OS_WIN32) || (defined(__APPLE__) && defined(__MACH__))
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Copy", ICON_COPY,
                                                     CTX_COPY, has_frame));
#endif
    append_context_sep(box);
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Pan", ICON_PAN,
                                                     CTX_PAN, has_frame));
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Zoom", ICON_ZOOM,
                                                     CTX_ZOOM, has_frame));
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Reset", ICON_RESET,
                                                     CTX_RESET, can_reset));
    append_context_sep(box);
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Clear Annotation", ICON_CLEAR,
                                                     CTX_CLEAR, has_annotations));
    append_context_sep(box);
    gtk_box_append(GTK_BOX(box), make_context_button(app, popover, "Quit", ICON_QUIT,
                                                     CTX_QUIT, TRUE));

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    rect.x = (int)x;
    rect.y = (int)y;
    rect.width = 1;
    rect.height = 1;
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void on_context_pressed(GtkGestureClick *gesture,
                               int n_press,
                               double x,
                               double y,
                               gpointer user_data)
{
    (void)n_press;
    App *app = user_data;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    show_context_menu(app, x, y);
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
    ensure_app_css();

    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), WINDOW_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(app->window), DEFAULT_WIDTH, DEFAULT_HEIGHT);
    g_signal_connect(app->window, "close-request",
                     G_CALLBACK(on_window_close_request), app);

    app->root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(app->window), app->root_box);

    app->main_toolbar = create_main_toolbar(app);
    gtk_box_append(GTK_BOX(app->root_box), make_toolbar_scroller(app->main_toolbar));

    app->pen_toolbar = create_pen_toolbar(app);
    app->pen_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(app->pen_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(GTK_REVEALER(app->pen_revealer), make_toolbar_scroller(app->pen_toolbar));
    gtk_box_append(GTK_BOX(app->root_box), app->pen_revealer);

    app->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->area, 38, 24);
    gtk_widget_set_hexpand(app->area, TRUE);
    gtk_widget_set_vexpand(app->area, TRUE);
    gtk_widget_set_focusable(app->area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->area), on_draw, app, NULL);
    gtk_box_append(GTK_BOX(app->root_box), app->area);

    app->status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(app->status_box, 3);
    gtk_widget_set_margin_bottom(app->status_box, 3);
    gtk_widget_set_margin_start(app->status_box, 8);
    gtk_widget_set_margin_end(app->status_box, 8);

    app->status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(app->status_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_width_chars(GTK_LABEL(app->status_label), 1);
    gtk_widget_set_hexpand(app->status_label, TRUE);
    gtk_box_append(GTK_BOX(app->status_box), app->status_label);

    app->status_progress = gtk_progress_bar_new();
    gtk_widget_set_size_request(app->status_progress, 120, -1);
    gtk_widget_set_valign(app->status_progress, GTK_ALIGN_CENTER);
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(app->status_progress), 0.08);
    gtk_widget_set_visible(app->status_progress, FALSE);
    gtk_box_append(GTK_BOX(app->status_box), app->status_progress);

    gtk_box_append(GTK_BOX(app->root_box), app->status_box);

    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), app);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), app);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), app);
    gtk_widget_add_controller(app->area, GTK_EVENT_CONTROLLER(drag));

    GtkGesture *context_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(context_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(context_click, "pressed", G_CALLBACK(on_context_pressed), app);
    gtk_widget_add_controller(app->area, GTK_EVENT_CONTROLLER(context_click));

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
        if (strcmp(argv[i], "--status") == 0) {
            if (i + 1 < argc) i++;
            continue;
        }
        if (g_str_has_prefix(argv[i], "--status=")) {
            continue;
        }
        if (strcmp(argv[i], "--watch") == 0) {
            if (i + 1 < argc) i++;
            continue;
        }
        if (g_str_has_prefix(argv[i], "--watch=")) {
            continue;
        }
        if (strcmp(argv[i], "--backend") == 0) {
            if (i + 1 < argc) i++;
            continue;
        }
        if (g_str_has_prefix(argv[i], "--backend=")) {
            continue;
        }
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

static gchar *parse_status_arg(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--status") == 0 && i + 1 < argc) {
            return g_strdup(argv[i + 1]);
        }
        if (g_str_has_prefix(argv[i], "--status=")) {
            return g_strdup(argv[i] + strlen("--status="));
        }
    }

    return NULL;
}

static gchar *parse_watch_arg(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--watch") == 0 && i + 1 < argc) {
            return g_strdup(argv[i + 1]);
        }
        if (g_str_has_prefix(argv[i], "--watch=")) {
            return g_strdup(argv[i] + strlen("--watch="));
        }
    }

    return NULL;
}

static void append_watch_line(App *app, gchar *line)
{
    gchar **fields;
    const gchar *svg_path;
    const gchar *display_path;
    const gchar *label;
    int old_count;

    if (!app || str_empty(line)) return;

    line = g_strstrip(line);
    if (str_empty(line) || line[0] == '#') return;

    fields = g_strsplit(line, "\t", 3);
    svg_path = fields && fields[0] ? fields[0] : NULL;
    display_path = fields && fields[1] ? fields[1] : NULL;
    label = fields && fields[2] ? fields[2] : NULL;

    if (str_empty(svg_path) || !g_file_test(svg_path, G_FILE_TEST_EXISTS)) {
        g_strfreev(fields);
        return;
    }

    old_count = app->sequence.count;
    if (svg_sequence_append_file(&app->sequence, svg_path, display_path, label)) {
        if (old_count == 0) {
            app->sequence.current_index = 0;
            g_clear_pointer(&app->load_error, g_free);
        }
        if (app->sequence.count > 1) app->sequence.playing = TRUE;
        update_ui(app);
        if (app->area) gtk_widget_queue_draw(app->area);
    }

    g_strfreev(fields);
}

static gboolean poll_watch_manifest(gpointer user_data)
{
    App *app = (App *)user_data;
    gchar *content = NULL;
    gsize len = 0;
    GError *err = NULL;
    gchar *start;
    gchar *end;

    if (!app || str_empty(app->watch_path)) return G_SOURCE_REMOVE;

    if (!g_file_get_contents(app->watch_path, &content, &len, &err)) {
        g_clear_error(&err);
        return G_SOURCE_CONTINUE;
    }

    if (len <= app->watch_offset) {
        g_free(content);
        return G_SOURCE_CONTINUE;
    }

    start = content + app->watch_offset;
    end = content + len;

    while (start < end) {
        gchar *nl = memchr(start, '\n', (gsize)(end - start));
        if (!nl) break;

        *nl = '\0';
        append_watch_line(app, start);
        app->watch_offset = (gsize)(nl + 1 - content);
        start = nl + 1;
    }

    g_free(content);
    return G_SOURCE_CONTINUE;
}

static void apply_status_file(App *app, const gchar *path)
{
    gchar *content = NULL;
    gchar **lines = NULL;
    GError *err = NULL;

    if (!app || str_empty(path) || app->sequence.count <= 0) return;

    if (!g_file_get_contents(path, &content, NULL, &err)) {
        fprintf(stderr, "Warning: failed to read status file %s: %s\n",
                path, err ? err->message : "unknown");
        g_clear_error(&err);
        return;
    }

    lines = g_strsplit(content, "\n", -1);
    for (int i = 0; lines && lines[i] && i < app->sequence.count; i++) {
        gchar *line = g_strstrip(lines[i]);
        gchar *tab;

        if (str_empty(line)) continue;

        tab = strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            tab++;
        }

        if (!str_empty(line)) {
            g_free(app->sequence.frames[i].path);
            app->sequence.frames[i].path = g_strdup(line);
        }

        if (!str_empty(tab)) {
            g_free(app->sequence.frames[i].framelabel);
            app->sequence.frames[i].framelabel = g_strdup(tab);
        }
    }

    g_strfreev(lines);
    g_free(content);
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s [--status file.status] [--backend gtk] file.svg   # Load from file\n"
            "  %s --watch frames.manifest            # Append streamed SVG frames\n"
            "  cat file.svg | %s                    # Load from stdin\n"
            "  %s - < file.svg                      # Explicit stdin\n",
            prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
    gboolean has_stdin = !isatty(fileno(stdin));
    gchar *status_path = parse_status_arg(argc, argv);
    gchar *watch_path = parse_watch_arg(argc, argv);

#ifdef G_OS_WIN32
    setup_windows_bundle_env(argc > 0 ? argv[0] : NULL);
#endif

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
    app.watch_path = watch_path;

    if (!load_inputs(&app, argc, argv)) {
        if (!str_empty(app.watch_path)) {
            app.load_error = g_strdup("Waiting for streamed SVG frames...");
        } else if (argc < 2 && !has_stdin) {
            app.load_error = g_strdup("No SVG input. Open a file or pipe SVG data to svgviewer.");
        } else {
            app.load_error = g_strdup("No displayable SVG content. Check the input file or stream.");
        }
        fprintf(stderr, "%s\n", app.load_error);
    } else {
        apply_status_file(&app, status_path);

        if (!svg_sequence_validate_current(&app.sequence)) {
            svg_sequence_free(&app.sequence);
            app.load_error = g_strdup("No displayable SVG content. Check the input file or stream.");
            fprintf(stderr, "%s\n", app.load_error);
        }
    }

    if (app.sequence.count > 1) app.sequence.playing = TRUE;
    svg_sequence_set_handle_cache_radius(&app.sequence, 1);
    svg_sequence_set_surface_cache_radius(&app.sequence, SVG_SEQUENCE_DEFAULT_SURFACE_CACHE_RADIUS);

    if (!str_empty(app.watch_path)) {
        poll_watch_manifest(&app);
        app.watch_id = g_timeout_add(200, poll_watch_manifest, &app);
    }

    GtkApplication *gtk_app = gtk_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), &app);

    char *gtk_argv[] = { argv[0], NULL };
    int status = g_application_run(G_APPLICATION(gtk_app), 1, gtk_argv);

    if (app.tick_id) g_source_remove(app.tick_id);
    if (app.watch_id) g_source_remove(app.watch_id);
    if (app.busy_pulse_id) g_source_remove(app.busy_pulse_id);
    g_object_unref(gtk_app);
    svg_sequence_free(&app.sequence);
    if (app.annotations) g_ptr_array_free(app.annotations, TRUE);
    if (app.undo_stack) g_ptr_array_free(app.undo_stack, TRUE);
    if (app.redo_stack) g_ptr_array_free(app.redo_stack, TRUE);
    g_free(app.load_error);
    g_free(app.watch_path);
    g_free(status_path);

    (void)status;
    return 0;
}
