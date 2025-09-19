#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <librsvg/rsvg.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "svg_sequence.h"


#include <time.h>
#include <unistd.h> // for isatty()

#define WINDOW_TITLE "RSFPY - SVG Sequence Viewer"

#define MAX_BUTTONS 8


#define NEXT_BUTTON 0
#define PREV_BUTTON 1
#define RUN_BUTTON 2
#define PAUSE_BUTTON 3
#define FASTER_BUTTON 4
#define SLOWER_BUTTON 5

#define DEFAULT_WIDTH 960
#define DEFAULT_HEIGHT 720
#define MIN_WIDTH 500
#define MIN_HEIGHT 450

#define MAX_FPS 20
#define MIN_FPS 1

#define RUNMSG "Running"
#define PAUSEMSG "Paused"


typedef struct {
    int x, y;
    int width, height;
} ButtonRect;

typedef struct {
    char label[32];
    int index;
    int x, y, width, height;
    gboolean enabled;
} Button;


typedef struct {
    Display *dpy;
    int screen;
    Window win;
    cairo_surface_t *surface;
    cairo_t *cr;
    int win_w, win_h;
    int toolbar_h;
    int hintbar_h;

    Button buttons[MAX_BUTTONS];
    int num_buttons;

    SvgSequence sequence;
    gboolean resize_pending;
    struct timespec last_resize_time;
    int resize_w, resize_h;


} App;


static char *but_labels[] = {"Prev(m)", "Next(n)", "Run(r)", "Pause(p)", "Faster(+)", "Slower(-)"};



Button draw_button(cairo_t *cr, int x, int y, const char *label, double font_size, gboolean enabled) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, font_size);

    cairo_text_extents_t ext;
    cairo_text_extents(cr, label, &ext);

    int btn_w = (int)(ext.width + 24);
    int btn_h = (int)(font_size * 1.5);

    // 背景
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.88);
    cairo_rectangle(cr, x, y, btn_w, btn_h);
    cairo_fill(cr);

    // 边框（可选）
    cairo_set_line_width(cr, 1.0);
    if (enabled)
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.35); // 深灰
    else
        cairo_set_source_rgb(cr, 0.7, 0.7, 0.75); // 浅灰
    cairo_rectangle(cr, x, y, btn_w, btn_h);
    cairo_stroke(cr);

    // 文字
    double text_x = x + (btn_w - ext.width) / 2 - ext.x_bearing;
    double text_y = y + (btn_h - ext.height) / 2 - ext.y_bearing;
    if (enabled)
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.25);
    else
        cairo_set_source_rgb(cr, 0.6, 0.6, 0.65);
    cairo_move_to(cr, text_x, text_y);
    cairo_show_text(cr, label);

    Button b;
    strncpy(b.label, label, sizeof(b.label));
    b.x = x;
    b.y = y;
    b.width = btn_w;
    b.height = btn_h;
    b.enabled = enabled;
    return b;
}


static void fatal(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}


static gboolean slurp_stdin(GBytes **out_bytes) {
    GByteArray *buf = g_byte_array_new();
    const size_t CHUNK = 64 * 1024;
    guint8 *tmp = g_malloc(CHUNK);
    size_t n;
    while ((n = fread(tmp, 1, CHUNK, stdin)) > 0) {
        g_byte_array_append(buf, tmp, (guint)n);
    }
    g_free(tmp);
    if (ferror(stdin)) {
        g_byte_array_free(buf, TRUE);
        return FALSE;
    }
    GBytes *bytes = g_bytes_new_take(buf->data, buf->len);
    // buf container freed but data owned by GBytes now
    g_free(buf); // only frees struct; safe because we used new_take
    *out_bytes = bytes;
    return TRUE;
}


static void create_cairo(App *app) {
    if (app->cr) { cairo_destroy(app->cr); app->cr = NULL; }
    if (app->surface) { cairo_surface_destroy(app->surface); app->surface = NULL; }
    app->surface = cairo_xlib_surface_create(
        app->dpy, app->win, DefaultVisual(app->dpy, app->screen), app->win_w, app->win_h);
    app->cr = cairo_create(app->surface);
}

static void measure_toolbar_height(App *app) {
    int dpi = DisplayWidth(app->dpy, app->screen) * 25.4 / DisplayWidthMM(app->dpy, app->screen);
    double scale = dpi / 96.0; // 96 是传统 DPI 标准
    double pt_size = 20.0;
    double px_size = pt_size * dpi / 72.0;
    // fprintf(stderr, "DPI: %d, height=%d, width=%d\n", dpi,  app->win_h, app->win_w);

    cairo_select_font_face(app->cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(app->cr, px_size); // 基准字体大小
    cairo_text_extents_t ext;
    cairo_text_extents(app->cr, "Run", &ext); // 任意按钮文本
    double font_h = ext.height;
    app->toolbar_h = (int)(font_h + 16); // 加一点 padding
}

void draw_hintbar(App *app, int hintbar_h) {
    cairo_save(app->cr);

    // 背景区域
    cairo_set_source_rgb(app->cr, 0.92, 0.92, 0.95); // 浅灰色
    cairo_rectangle(app->cr, 0, app->win_h - hintbar_h, app->win_w, hintbar_h);
    cairo_fill(app->cr);

    // 提示文字
    char hint[128];
    if (app->sequence.count <= 1) {
        snprintf(hint, sizeof(hint),
                 "File: %s. Single-file mode: toolbar disabled. Press q or Esc to quit.",
                    app->sequence.count == 1 ? app->sequence.frames[0].path : "N/A");
    } else {
        snprintf(hint, sizeof(hint),
                 "%s. File: %s. Press q or Esc to quit.",
                 app->sequence.frames[app->sequence.current_index].framelabel,
                 app->sequence.frames[app->sequence.current_index].path);
    }

    int padding = 10;
    cairo_set_source_rgb(app->cr, 0.25, 0.25, 0.3);
    cairo_select_font_face(app->cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(app->cr, 12);

    cairo_text_extents_t ext;
    cairo_text_extents(app->cr, hint, &ext);
    double text_x = padding;
    double text_y = app->win_h - hintbar_h + (hintbar_h - ext.height) / 2 - ext.y_bearing;

    cairo_move_to(app->cr, text_x, text_y);
    cairo_show_text(app->cr, hint);

    cairo_restore(app->cr);
}



void draw_toolbar(App *app) {
    cairo_save(app->cr);
    // toolbar background
    cairo_set_source_rgb(app->cr, 0.92, 0.92, 0.95);
    cairo_rectangle(app->cr, 0, 0, app->win_w, app->toolbar_h);
    cairo_fill(app->cr);

    // placeholder button labels (disabled look)
    int n = 6;
    int padding = 10;
    int x = padding, y = 8;
    double font_size = 12.0;


    app->num_buttons = 0;
    for (int i = 0; i < n && app->num_buttons < MAX_BUTTONS; i++) {
        gboolean enabled = TRUE;
        if (i == RUN_BUTTON) enabled = !app->sequence.playing;
        else if (i == PAUSE_BUTTON || i==FASTER_BUTTON || i==SLOWER_BUTTON) enabled = app->sequence.playing;
        else if (i == NEXT_BUTTON || i==PREV_BUTTON) enabled = TRUE;

        if (app->sequence.count <= 1) {
            enabled = FALSE;
        }
        Button b = draw_button(app->cr, x, y, but_labels[i], font_size, enabled);
        b.index = i;
        app->buttons[app->num_buttons++] = b;
        x += b.width + padding;
    }
    char fps_text[64];
    snprintf(fps_text, sizeof(fps_text), "FPS: %d frame(s)/sec. %s.", app->sequence.fps, app->sequence.playing ? RUNMSG : PAUSEMSG);

    cairo_set_source_rgb(app->cr, 0.25, 0.25, 0.3);
    cairo_select_font_face(app->cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(app->cr, font_size);

    cairo_text_extents_t ext;
    cairo_text_extents(app->cr, fps_text, &ext);

    double text_x = app->win_w - ext.width - padding;
    double text_y = y + ext.height;

    cairo_move_to(app->cr, text_x, text_y);
    cairo_show_text(app->cr, fps_text);



}


static void draw_all(App *app) {
    if (!app->cr) return;
    svg_sequence_render_frame(&app->sequence, app->cr,
                              app->win_w, app->win_h,
                              app->toolbar_h, app->hintbar_h);
    draw_toolbar(app);
    draw_hintbar(app, app->hintbar_h);

}

static void run_loop(App *app) {
    XSelectInput(app->dpy, app->win,
             ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask);

    XMapWindow(app->dpy, app->win);
    XFlush(app->dpy);

    draw_all(app);

    struct timespec last_frame_time = {0};
    clock_gettime(CLOCK_MONOTONIC, &last_frame_time);

    while (1) {
    while (XPending(app->dpy)) {
        XEvent ev;
        XPeekEvent(app->dpy, &ev);
        if (ev.type == Expose) {
            XNextEvent(app->dpy, &ev);
            continue;
        }

        XNextEvent(app->dpy, &ev);
        switch (ev.type) {
            case ConfigureNotify: {
                app->resize_pending = TRUE;
                app->resize_w = ev.xconfigure.width;
                app->resize_h = ev.xconfigure.height;
                clock_gettime(CLOCK_MONOTONIC, &app->last_resize_time);
            } break;

            case ButtonPress: {
                int x = ev.xbutton.x;
                int y = ev.xbutton.y;

                for (int i = 0; i < app->num_buttons; i++) {

                    Button *btn = &app->buttons[i];
                    if (!btn->enabled) continue;
                    if (x >= btn->x && x <= btn->x + btn->width &&
                        y >= btn->y && y <= btn->y + btn->height) {

                        if (btn->index == PAUSE_BUTTON) {
                            app->sequence.playing = FALSE;
                        }
                        else if (btn->index == RUN_BUTTON) {
                            app->sequence.playing = TRUE;
                        }
                        else if (btn->index == NEXT_BUTTON) {
                            app->sequence.playing = FALSE;
                            app->sequence.current_index =
                                (app->sequence.current_index + 1) % app->sequence.count;
                        }
                        else if (btn->index == PREV_BUTTON) {
                            app->sequence.playing = FALSE;
                            app->sequence.current_index =
                                (app->sequence.current_index - 1 + app->sequence.count) % app->sequence.count;
                        }
                        else if (btn->index == FASTER_BUTTON) {
                            if (app->sequence.fps < MAX_FPS)
                            {
                                app->sequence.fps++;
                            }
                        }
                        else if (btn->index == SLOWER_BUTTON) {
                            if (app->sequence.fps > MIN_FPS)
                            {
                                app->sequence.fps--;
                            }
                        }
                        draw_all(app);
                    }
                }
            } break;

            case KeyPress: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                if (ks == XK_q || ks == XK_Escape) return;
                if (ks == XK_Left || ks == XK_n) {
                    app->sequence.playing = FALSE;
                    app->sequence.current_index =
                        (app->sequence.current_index - 1 + app->sequence.count) % app->sequence.count;
                    draw_all(app);
                }
                if (ks == XK_Right || ks == XK_m) {
                    app->sequence.playing = FALSE;
                    app->sequence.current_index =
                        (app->sequence.current_index + 1) % app->sequence.count;
                    draw_all(app);
                }
                if (ks == XK_space) app->sequence.playing = !app->sequence.playing;
                if (ks == XK_p) app->sequence.playing = FALSE;
                if (ks == XK_r) app->sequence.playing = TRUE;

                if (ks == XK_plus || ks == XK_equal) {
                    if (app->sequence.fps < MAX_FPS)
                        {
                            app->sequence.fps++;
                            draw_toolbar(app);
                        }
                }
                if (ks == XK_minus || ks == XK_underscore) {
                    if (app->sequence.fps > MIN_FPS)
                        {
                            app->sequence.fps--;
                            draw_toolbar(app);
                        }
                }
            } break;
            default: break;
        }
    }

    // 播放控制
    if (app->sequence.playing && app->sequence.fps > 0) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - last_frame_time.tv_sec) * 1000 +
                          (now.tv_nsec - last_frame_time.tv_nsec) / 1000000;
        int interval_ms = 1000 / app->sequence.fps;
        if (elapsed_ms >= interval_ms) {
            svg_sequence_advance(&app->sequence);
            draw_all(app);
            last_frame_time = now;
        }
    }

    if (app->resize_pending) {
        // Limit size
        if (app->resize_w < MIN_WIDTH) {
            app->resize_w = MIN_WIDTH;
        }
        if (app->resize_h < MIN_HEIGHT) {
            app->resize_h = MIN_HEIGHT;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - app->last_resize_time.tv_sec) * 1000 +
                        (now.tv_nsec - app->last_resize_time.tv_nsec) / 1000000;
        if (elapsed_ms >= 200) {

            if (app->win_w != app->resize_w || app->win_h != app->resize_h) {
                XResizeWindow(app->dpy, app->win, app->resize_w, app->resize_h);
                app->win_w = app->resize_w;
                app->win_h = app->resize_h;
                create_cairo(app);
                measure_toolbar_height(app);
                app->hintbar_h = 32;
                draw_all(app);
            }
            app->resize_pending = FALSE;
        }
    }

    nanosleep(&(struct timespec){0, 10000000}, NULL);

}

}


int main(int argc, char **argv) {
    gboolean has_stdin = !isatty(fileno(stdin));
    if (argc < 2 && !has_stdin) {
        fprintf(stderr,
            "Usage:\n"
            "  %s file.svg             # Load from file\n"
            "  cat file.svg | %s       # Load from stdin\n"
            "  %s - < file.svg         # Explicit stdin\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    // Init X
    App app;
    memset(&app, 0, sizeof(app));
    app.toolbar_h = 48;
    app.hintbar_h = 32;
    app.win_w = DEFAULT_WIDTH;
    app.win_h = DEFAULT_HEIGHT;
    app.resize_w = app.win_w;
    app.resize_h = app.win_h;


    app.dpy = XOpenDisplay(NULL);
    if (!app.dpy) fatal("Cannot open X display");
    app.screen = DefaultScreen(app.dpy);
    app.win = XCreateSimpleWindow(app.dpy, RootWindow(app.dpy, app.screen),
                                  0, 0, app.win_w, app.win_h, 0,
                                  BlackPixel(app.dpy, app.screen),
                                  WhitePixel(app.dpy, app.screen));

    // Load SVGs: from args or stdin
    gboolean ok = FALSE;

    if (argc >= 2 && strcmp(argv[1], "-") != 0) {
        // 从文件路径加载
        ok = svg_sequence_load_files(&app.sequence, &argv[1], argc - 1);
    } else {
        // 从标准输入加载
        GError *err = NULL;
        gchar *content = NULL;
        gsize len = 0;

        if (!g_file_get_contents("/dev/stdin", &content, &len, &err)) {
            fprintf(stderr, "Failed to read from stdin: %s\n", err->message);
            g_clear_error(&err);
            return 1;
        }

        ok = svg_sequence_load_from_stream(&app.sequence, content, len);
        g_free(content);
    }

    if (!ok || app.sequence.count == 0) {
        fprintf(stderr, "Failed to load SVG sequence. Exiting.\n");
        return 1;
    }



    create_cairo(&app);
    measure_toolbar_height(&app);
    XStoreName(app.dpy, app.win, WINDOW_TITLE);


    run_loop(&app);

    // cleanup
    if (app.cr) cairo_destroy(app.cr);
    if (app.surface) cairo_surface_destroy(app.surface);
    XDestroyWindow(app.dpy, app.win);
    XCloseDisplay(app.dpy);
    svg_sequence_free(&app.sequence);

    exit (0);
}
