#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <librsvg/rsvg.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "svgsequence.h"


#include <time.h>
#include <unistd.h> // for isatty()



typedef struct {
    int x, y;
    int width, height;
} ButtonRect;

typedef struct {
    char label[32];
    int index;
    int x, y, width, height;
    gboolean enabled;
    gboolean pressed;
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

    double pan_x, pan_y;
    int drag_start_x, drag_start_y;
    gboolean dragging;
    struct timespec last_drag_time;

    double zoom_scale;

    gboolean drag_mode;
    gboolean zoom_mode;

    struct timespec last_zoom_time;
    gboolean zooming;

    


} App;


// static char *but_labels[] = {"Prev(m)", "Next(n)", "Run(r)", "Pause(p)", "Slower(-)", "Faster(+)", "Reset(h)" /* Home button */};
static char *but_labels[] = {
    /* Prev */
    "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64' transform='scale(-1, 1) translate(-64, 0)' >"
    "<rect x='1' y='1' width='62' height='62' rx='8' ry='8' "
    "fill='%s' stroke='%s' stroke-width='2'/>"
    "<rect x='40' y='16' width='8' height='32' fill='%s'/>"
    "<polygon points='14,48 30,32 14,16' fill='%s'/>"
    "<polygon points='28,48 44,32 28,16' fill='%s'/>"
    "</svg>",

    /* Next */
    "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'>"
    "<rect x='1' y='1' width='62' height='62' rx='8' ry='8' "
    "fill='%s' stroke='%s' stroke-width='2'/>"
    "<rect x='40' y='16' width='8' height='32' fill='%s'/>"
    "<polygon points='14,48 30,32 14,16' fill='%s'/>"
    "<polygon points='28,48 44,32 28,16' fill='%s'/>"
    "</svg>",

    /* Run */
    "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'>"
    "<rect x='1' y='1' width='62' height='62' rx='8' ry='8' "
    "fill='%s' stroke='%s' stroke-width='2'/>"
    "<polygon points='20,16 52,32 20,48' fill='%s'/>"
    "</svg>",

    /* Pause */
    "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'>"
    "<rect x='1' y='1' width='62' height='62' rx='8' ry='8' "
    "fill='%s' stroke='%s' stroke-width='2'/>"
    "<rect x='18' y='16' width='10' height='32' fill='%s'/>"
    "<rect x='36' y='16' width='10' height='32' fill='%s'/>"
    "</svg>",

    /* Slower */
    "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64' transform='scale(-1, 1) translate(-64, 0)' >"
    "<rect x='1' y='1' width='62' height='62' rx='8' ry='8' "
    "fill='%s' stroke='%s' stroke-width='2'/>"
    "<polygon points='12,48 31,32 12,16' fill='%s'/>"
    "<polygon points='32,48 52,32 30,16' fill='%s'/>"
    "</svg>",

    /* Faster */
    "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'  >"
    "<rect x='1' y='1' width='62' height='62' rx='8' ry='8' "
    "fill='%s' stroke='%s' stroke-width='2'/>"
    "<polygon points='12,48 31,32 12,16' fill='%s'/>"
    "<polygon points='32,48 52,32 30,16' fill='%s'/>"
    "</svg>",

    /* Drag (Cross Arrows) */
    "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'>"
    "<rect x='1' y='1' width='62' height='62' rx='8' ry='8' "
            "fill='%s' stroke='%s' stroke-width='2'/>"
    "<polygon points='32,6 24,18 40,18' fill='%s'/>"
    "<polygon points='32,58 24,46 40,46' fill='%s'/>"
    "<polygon points='6,32 18,24 18,40' fill='%s'/>"
    "<polygon points='58,32 46,24 46,40' fill='%s'/>"
    "<rect x='28' y='22' width='8' height='20' fill='%s'/>"
    "<rect x='22' y='28' width='20' height='8' fill='%s'/>"
    "</svg>",

    /* Zoom (Magnifier) */
    "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'>"
    "<rect x='1' y='1' width='62' height='62' rx='8' ry='8' "
            "fill='%s' stroke='%s' stroke-width='2'/>"
    "<circle cx='28' cy='28' r='16' fill='none' stroke='%s' stroke-width='4'/>"
    "<rect x='40' y='36' width='24' height='6' rx='3' ry='3' fill='%s' "
            "transform='rotate(45 42 37)'/>"
    "<rect x='16' y='25' width='24' height='6' rx='3' ry='3' fill='%s' />"
    "<rect x='25' y='16' width='6' height='24' rx='3' ry='3' fill='%s' />"

    "</svg>",

    /* Reset */
    "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64' >"
    "<rect x='1' y='1' width='62' height='62' rx='8' ry='8' "
            "fill='%s' stroke='%s' stroke-width='2'/>"
    "<path d='M44 20 L52 20 L46 26 "
            "C42 22 37 20 32 20 "
            "C22 20 14 28 14 38 "
            "C14 48 22 56 32 56 "
            "C37 56 42 54 46 50 "
            "L42 46 "
            "C39 49 36 50 32 50 "
            "C25 50 20 45 20 38 "
            "C20 31 25 26 32 26 "
            "C35 26 38 27 40 29 "
            "L36 33 L52 33 Z' "
            "fill='%s'  transform='translate(0, -5)'/>"
    "</svg>"
};


/* static unsigned char *clipboard_png_data;
static size_t clipboard_png_size;
static cairo_status_t
write_png_to_memory(void *closure, const unsigned char *data, unsigned int length) {
    size_t old_size = clipboard_png_size;
    clipboard_png_size += length;
    clipboard_png_data = realloc(clipboard_png_data, clipboard_png_size);
    memcpy(clipboard_png_data + old_size, data, length);
    return CAIRO_STATUS_SUCCESS;
}

void copy_cairo_region_to_clipboard(App *app, Display *dpy, Window win,
                                    cairo_surface_t *src) {
    free(clipboard_png_data);
    clipboard_png_data = NULL;
    clipboard_png_size = 0;
    cairo_surface_write_to_png_stream(src, write_png_to_memory, NULL);

    Atom XA_CLIPBOARD = XInternAtom(dpy, "CLIPBOARD", False);
    XSetSelectionOwner(dpy, XA_CLIPBOARD, win, CurrentTime);
    XFlush(dpy);
    fprintf(stderr, "Copied %zu bytes PNG to clipboard.\n", clipboard_png_size);
}

void handle_selection_request(Display *dpy, XEvent *e) {
    XSelectionRequestEvent *req = &e->xselectionrequest;

    XSelectionEvent sev;
    memset(&sev, 0, sizeof(sev));
    sev.type      = SelectionNotify;
    sev.display   = req->display;
    sev.requestor = req->requestor;
    sev.selection = req->selection;
    sev.target    = req->target;
    sev.property  = req->property;
    sev.time      = req->time;

    Atom XA_TARGETS = XInternAtom(dpy, "TARGETS", False);
    Atom XA_UTF8    = XInternAtom(dpy, "UTF8_STRING", False);
    Atom atom_STRING  = XInternAtom(dpy, "STRING", False);
    Atom XA_PNG     = XInternAtom(dpy, "image/png", False);

    if (req->target == XA_TARGETS) {
        Atom targets[] = { XA_UTF8, atom_STRING, XA_PNG };
        XChangeProperty(dpy, req->requestor, req->property,
                        XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)targets, 3);
    }
    else if (req->target == XA_UTF8 || req->target == atom_STRING) {
        const char *text = "Hello from WSLg + X11!";
        XChangeProperty(dpy, req->requestor, req->property,
                        req->target, 8, PropModeReplace,
                        (unsigned char*)text, strlen(text));
    }
    else if (req->target == XA_PNG && clipboard_png_data && clipboard_png_size > 0) {
        XChangeProperty(dpy, req->requestor, req->property,
                        XA_PNG, 8, PropModeReplace,
                        clipboard_png_data, clipboard_png_size);
    }
    else {
        sev.property = None;
    }

    XSendEvent(dpy, req->requestor, True, 0, (XEvent*)&sev);
    XFlush(dpy);
} */


Button draw_button(cairo_t *cr, int x, int y, const char *svg_label,
                   double height, gboolean enabled, gboolean pressed) {
    int btn_w = 0;
    int btn_h = (int)(0.8 * height);
    char svg_buf[1024];

    if (svg_label) {
        GError *err = NULL;
        snprintf(svg_buf, sizeof(svg_buf), svg_label,
                /* background color */ pressed? PRESSED_COLOR:WHITE,
                /* stroke color */ enabled? ENABLED_COLOR:DISABLED_COLOR,
                /* fill color */ enabled? ENABLED_COLOR:DISABLED_COLOR,
                /* fill color */ enabled? ENABLED_COLOR:DISABLED_COLOR,
                /* fill color */ enabled? ENABLED_COLOR:DISABLED_COLOR,
                /* fill color */ enabled? ENABLED_COLOR:DISABLED_COLOR,
                /* fill color */ enabled? ENABLED_COLOR:DISABLED_COLOR,
                /* fill color */ enabled? ENABLED_COLOR:DISABLED_COLOR,
                /* fill color */ enabled? ENABLED_COLOR:DISABLED_COLOR);
        RsvgHandle *handle = rsvg_handle_new_from_data(
            (const guint8 *)svg_buf,
            strlen(svg_buf),
            &err
        );
        if (handle) {
            RsvgDimensionData dim;
            double svg_w, svg_h;

        #if LIBRSVG_CHECK_VERSION(2, 46, 0)
            if (!rsvg_handle_get_intrinsic_size_in_pixels(handle, &svg_w, &svg_h)) {
                svg_w = 64;
                svg_h = 64;
            }
        #else
            RsvgDimensionData dim;
            rsvg_handle_get_dimensions(handle, &dim);
            svg_w = dim.width;
            svg_h = dim.height;
        #endif


            double scale = 0.8 * height / (double)svg_h;
            btn_h = (int)(svg_h * scale);
            btn_w = (int)(svg_w * scale);
            double ox = x + 0;
            double oy = 0.1 * height;

            cairo_save(cr);
            cairo_translate(cr, ox, oy);
            cairo_scale(cr, scale, scale);

#if LIBRSVG_CHECK_VERSION(2,52,0)
            RsvgRectangle viewport = {0, 0, svg_w, svg_h};
            rsvg_handle_render_document(handle, cr, &viewport, NULL);
#else
            rsvg_handle_render_cairo(handle, cr);
#endif
            cairo_restore(cr);

            g_object_unref(handle);
        } else {
            fprintf(stderr, "SVG load error: %s\n", err->message);
            g_error_free(err);
        }
    }

    Button b;
    b.x = x;
    b.y = y;
    b.width = btn_w;
    b.height = btn_h;
    b.enabled = enabled;
    b.pressed = pressed;
    strncpy(b.label, "[svg]", sizeof(b.label));
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

static void adjust_bar(App *app){
    app->toolbar_h = (int)(BAR_HEIGHT_RATIO * app->win_h);
    app->hintbar_h = (int)(BAR_HEIGHT_RATIO * app->win_h);

    if (app->toolbar_h < MIN_BAR_HEIGHT) app->toolbar_h = MIN_BAR_HEIGHT;
    if (app->hintbar_h < MIN_BAR_HEIGHT) app->hintbar_h = MIN_BAR_HEIGHT;
}

void draw_hintbar(App *app, int hintbar_h) {
    cairo_save(app->cr);

    cairo_set_source_rgba_string(app->cr, BAR_COLOR);
    cairo_rectangle(app->cr, 0, app->win_h - hintbar_h, app->win_w, hintbar_h);
    cairo_fill(app->cr);

    char hint[128];
    if (app->sequence.count <= 1) {
        snprintf(hint, sizeof(hint),
                 "File: %s. Single-file mode. Press q or Esc to quit.",
                    app->sequence.count == 1 ? app->sequence.frames[0].path : "N/A");
    } else {
        snprintf(hint, sizeof(hint),
                 "%s. File: %s. Press q or Esc to quit.",
                 app->sequence.frames[app->sequence.current_index].framelabel,
                 app->sequence.frames[app->sequence.current_index].path);
    }

    int padding = 10;
    cairo_set_source_rgba_string(app->cr, ENABLED_COLOR);
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
    cairo_set_source_rgba_string(app->cr, BAR_COLOR);
    cairo_rectangle(app->cr, 0, 0, app->win_w, app->toolbar_h);
    cairo_fill(app->cr);

    // placeholder button labels (disabled look)
    int padding = 10;
    int x = padding, y = 0;
    double button_height = 0.9 * app->toolbar_h;


    app->num_buttons = 0;
    for (int i = 0; i < MAX_BUTTONS; i++) {
        gboolean enabled = TRUE;
        gboolean pressed = (i == MOVE_BUTTON);

        switch(i) {
            case RUN_BUTTON:
                enabled = !app->sequence.playing;
                if (app->sequence.count <= 1 ) enabled = FALSE;
                break;
            case NEXT_BUTTON:
            case PREV_BUTTON:
                enabled = TRUE;
                if (app->sequence.count <= 1 ) enabled = FALSE;
                break;
            case PAUSE_BUTTON:
            case FASTER_BUTTON:
            case SLOWER_BUTTON:
                enabled = app->sequence.playing;
                if (app->sequence.count <= 1 ) enabled = FALSE;
                break;
            case HOME_BUTTON:
                if (app->zoom_scale != 1.0 || app->pan_x !=0 || app->pan_y !=0) {
                    enabled = TRUE;
                }else{
                    enabled = FALSE;
                }
                break;
            case MOVE_BUTTON:
                enabled = TRUE;
                pressed = app->drag_mode;
                break;
            case ZOOM_BUTTON:
                enabled = TRUE;
                pressed = app->zoom_mode;
                break;
            default:
                break;
        }

        Button b = draw_button(app->cr, x, y, but_labels[i], button_height, enabled, pressed);
        b.index = i;
        app->buttons[app->num_buttons++] = b;
        x += b.width + padding;
    }
    char fps_text[64];
    if (app->sequence.count <= 1)
    snprintf(fps_text, sizeof(fps_text), "%s%2.0f%%.",
                                        app->zooming? "Zooming... " : "",
                                        app->zoom_scale*100.);
    else
    snprintf(fps_text, sizeof(fps_text), "%d frame(s)/sec. %s%2.0f%%. %s.", app->sequence.fps,
                                        app->zooming? "Zooming... " : "",
                                        app->zoom_scale * 100.,
                                        app->sequence.playing ? RUNMSG : PAUSEMSG);

    cairo_set_source_rgba_string(app->cr, ENABLED_COLOR);
    cairo_select_font_face(app->cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(app->cr, DEFAULT_FONT_HEIGHT);

    cairo_text_extents_t ext;
    char test_text[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    cairo_text_extents(app->cr, test_text, &ext);
    double text_y = app->toolbar_h/ 2 ;
    cairo_text_extents(app->cr, fps_text, &ext);

    double text_x = app->win_w - ext.width - padding;

    if (text_x < x + padding) {
        text_x = x + padding;
    }

    cairo_move_to(app->cr, text_x, text_y);
    cairo_show_text(app->cr, fps_text);
}


static void draw_all(App *app) {
    if (!app->cr) return;
    svg_sequence_render_frame(&app->sequence, app->cr,
                              app->win_w >= MIN_WIDTH ? app->win_w : MIN_WIDTH,
                              app->win_h >= MIN_HEIGHT ? app->win_h: MIN_HEIGHT,
                              app->pan_x, app->pan_y,
                              app->zoom_scale,
                              app->toolbar_h, app->hintbar_h);
    draw_toolbar(app);
    draw_hintbar(app, app->hintbar_h);

}

static void run_loop(App *app) {
    struct timespec now;
    long elapsed_ms = 0;
    XSelectInput(app->dpy, app->win,
             ExposureMask | StructureNotifyMask | KeyPressMask |
             ButtonPressMask | ButtonReleaseMask | PointerMotionMask
    );

    XMapWindow(app->dpy, app->win);
    XEvent ev;
    XFlush(app->dpy);
    while (1) {
        XNextEvent(app->dpy, &ev);
        if (ev.type == Expose) break;
    }
    draw_all(app);

    struct timespec last_frame_time = {0};
    clock_gettime(CLOCK_MONOTONIC, &last_frame_time);

    while (1) {
    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed_ms = (now.tv_sec - last_frame_time.tv_sec) * 1000 +
                 (now.tv_nsec - last_frame_time.tv_nsec) / 1000000;

    /* Time to finish zooming */
    if (app->zooming){
        if (elapsed_ms < (3 * WAIT_TIME_MS)) {
        }else{
            app->last_zoom_time = now;
            app->zooming = FALSE;
            draw_all(app);
        }
    }

    while (XPending(app->dpy)) {
        XPeekEvent(app->dpy, &ev);
        if (ev.type == Expose) {
            XNextEvent(app->dpy, &ev);
            continue;
        }

        XNextEvent(app->dpy, &ev);
        switch (ev.type) {
            case Expose: {
                draw_all(app);
            } break;
/*             case SelectionRequest:
                {
                    handle_selection_request(app->dpy, &ev);
                    fprintf(stderr, "handled!");
                    break;
                } */
            case ConfigureNotify: {
                app->resize_pending = TRUE;
                app->resize_w = ev.xconfigure.width;
                app->resize_h = ev.xconfigure.height;
                clock_gettime(CLOCK_MONOTONIC, &app->last_resize_time);
            } break;
            case ButtonRelease: {
                if (app->drag_mode && ev.xbutton.button == Button1) {
                    app->dragging = FALSE;
                    draw_all(app);
                }
            } break;

            case MotionNotify: {
                if (app->drag_mode && app->dragging) {
                    int dx = ev.xmotion.x - app->drag_start_x;
                    int dy = ev.xmotion.y - app->drag_start_y;
                    if (elapsed_ms >= WAIT_TIME_MS && (abs(dx) > 5 || abs(dy) > 5)) {
                        app->pan_x += dx;
                        app->pan_y += dy;
                        app->drag_start_x = ev.xmotion.x;
                        app->drag_start_y = ev.xmotion.y;
                        draw_all(app);
                        app->last_drag_time = now;
                    }else if (!(abs(dx) > 5 || abs(dy) > 5)){
                        // Reset timer
                        app->last_drag_time = now;
                    }
                }
            } break;

            case ButtonPress: {
                int x = ev.xbutton.x;
                int y = ev.xbutton.y;
                if (app->drag_mode && ev.xbutton.button == Button1) {
                    app->dragging = TRUE;
                    app->drag_start_x = ev.xbutton.x;
                    app->drag_start_y = ev.xbutton.y;
                    clock_gettime(CLOCK_MONOTONIC, &app->last_drag_time);
                }

                if (app->zoom_mode && ev.xbutton.button == Button4) {
                    if (elapsed_ms < (3 * WAIT_TIME_MS)) {
                        app->zoom_scale += 0.05;
                        if (app->zoom_scale > MAX_ZOOM_SCALE) app->zoom_scale = MAX_ZOOM_SCALE;
                        app->zooming = TRUE;
                        app->last_zoom_time = now;
                        draw_toolbar(app);
                        break;
                    }
                    app->zoom_scale += 0.05;
                    if (app->zoom_scale > MAX_ZOOM_SCALE) app->zoom_scale = MAX_ZOOM_SCALE;
                    app->last_zoom_time = now;
                    app->zooming = FALSE;
                    draw_all(app);
                }
                else if (app->zoom_mode && ev.xbutton.button == Button5) {
                    if (elapsed_ms < (3 * WAIT_TIME_MS)) {
                        app->zoom_scale -= 0.05;
                        if (app->zoom_scale < MIN_ZOOM_SCALE) app->zoom_scale = MIN_ZOOM_SCALE;
                        app->zooming = TRUE;
                        app->last_zoom_time = now;
                        draw_toolbar(app);
                        break;
                    }
                    app->zoom_scale -= 0.05;
                    if (app->zoom_scale < MIN_ZOOM_SCALE) app->zoom_scale = MIN_ZOOM_SCALE;
                    app->last_zoom_time = now;
                    app->zooming = FALSE;
                    draw_all(app);
                }

                for (int i = 0; i < app->num_buttons; i++) {

                    Button *btn = &app->buttons[i];
                    if (!btn->enabled) continue;
                    if (x >= btn->x && x <= btn->x + btn->width &&
                        y >= btn->y && y <= btn->y + btn->height) {

                        if (btn->index == PAUSE_BUTTON) {
                            app->sequence.playing = FALSE;
                            draw_all(app);
                        }
                        else if (btn->index == RUN_BUTTON) {
                            app->sequence.playing = TRUE;
                            draw_all(app);
                        }
                        else if (btn->index == NEXT_BUTTON) {
                            app->sequence.playing = FALSE;
                            app->sequence.current_index =
                                (app->sequence.current_index + 1) % app->sequence.count;
                            draw_all(app);
                        }
                        else if (btn->index == PREV_BUTTON) {
                            app->sequence.playing = FALSE;
                            app->sequence.current_index =
                                (app->sequence.current_index - 1 + app->sequence.count) % app->sequence.count;
                            draw_all(app);
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
                        else if (btn->index == HOME_BUTTON) {
                            app->zoom_scale = 1.0;
                            app->pan_x = 0;
                            app->pan_y = 0;
                            draw_all(app);
                        }
                        else if (btn->index == MOVE_BUTTON) {
                            app->drag_mode = !app->drag_mode;
                            draw_toolbar(app);
                        }
                        else if (btn->index == ZOOM_BUTTON) {
                            app->zoom_mode = !app->zoom_mode;
                            draw_toolbar(app);
                        }
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
                if (ks == XK_space) {
                    app->sequence.playing = !app->sequence.playing;
                    draw_toolbar(app);
                }
                if (ks == XK_p) {
                    app->sequence.playing = FALSE;
                    draw_toolbar(app);
                }
                if (ks == XK_r) {
                    app->sequence.playing = TRUE;
                    draw_toolbar(app);
                }

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
                if (app->zoom_mode && ks == XK_Up) {
                    if (elapsed_ms < (3 * WAIT_TIME_MS)) {
                        app->zoom_scale += 0.05;
                        if (app->zoom_scale > MAX_ZOOM_SCALE) app->zoom_scale = MAX_ZOOM_SCALE;
                        app->zooming = TRUE;
                        app->last_zoom_time = now;
                        draw_toolbar(app);
                        break;
                    }
                    app->zoom_scale += 0.05;
                    if (app->zoom_scale < MIN_ZOOM_SCALE) app->zoom_scale = MIN_ZOOM_SCALE;
                    app->last_zoom_time = now;
                    app->zooming = FALSE;
                    draw_all(app);
                }
                if (app->zoom_mode && ks == XK_Down) {
                    if (elapsed_ms < (3 * WAIT_TIME_MS)) {
                        app->zoom_scale -= 0.05;
                        if (app->zoom_scale < MIN_ZOOM_SCALE) app->zoom_scale = MIN_ZOOM_SCALE;
                        app->zooming = TRUE;
                        app->last_zoom_time = now;
                        draw_toolbar(app);
                        break;
                    }
                    app->zoom_scale -= 0.05;
                    if (app->zoom_scale < MIN_ZOOM_SCALE) app->zoom_scale = MIN_ZOOM_SCALE;
                    app->last_zoom_time = now;
                    app->zooming = FALSE;
                    draw_all(app);
                }
                if (ks == XK_h || ks == XK_Home) {
                    app->zoom_scale = 1.0;
                    app->pan_x = 0;
                    app->pan_y = 0;
                    draw_all(app);
                }
            } break;
            default: break;
        }
    }

    if (app->sequence.playing && app->sequence.fps > 0) {
        int interval_ms = 1000 / app->sequence.fps;
        if (elapsed_ms >= interval_ms) {
            svg_sequence_advance(&app->sequence);
            // draw_all(app);
            svg_sequence_render_frame(&app->sequence, app->cr,
                              app->win_w >= MIN_WIDTH ? app->win_w : MIN_WIDTH,
                              app->win_h >= MIN_HEIGHT ? app->win_h: MIN_HEIGHT,
                              app->pan_x, app->pan_y,
                              app->zoom_scale,
                              app->toolbar_h, app->hintbar_h);
            last_frame_time = now;
            draw_hintbar(app, app->hintbar_h);
        }
    }

    if (app->resize_pending) {
        long elapsed_ms = (now.tv_sec - app->last_resize_time.tv_sec) * 1000 +
                        (now.tv_nsec - app->last_resize_time.tv_nsec) / 1000000;
        if (elapsed_ms >= 200) {

            if (app->win_w != app->resize_w || app->win_h != app->resize_h) {
                XResizeWindow(app->dpy, app->win, app->resize_w, app->resize_h);
                app->win_w = app->resize_w;
                app->win_h = app->resize_h;
                create_cairo(app);
                adjust_bar(app);
                app->hintbar_h = 32;
                draw_all(app);
                // copy_cairo_region_to_clipboard(app, app->dpy, app->win, app->sequence.frames[app->sequence.current_index].surface);
                /* Copy test */
            }else if (app->win_w < MIN_WIDTH || app->win_h < MIN_HEIGHT) {
                int new_w = app->win_w < MIN_WIDTH ? MIN_WIDTH : app->win_w;
                int new_h = app->win_h < MIN_HEIGHT ? MIN_HEIGHT : app->win_h;
                XResizeWindow(app->dpy, app->win, new_w, new_h);
                app->win_w = new_w;
                app->win_h = new_h;
                create_cairo(app);
                adjust_bar(app);
                app->hintbar_h = 32;
                draw_all(app);
            }
            app->resize_pending = FALSE;
        }
    }
    /* half of WAIT_TIME_MS */
    nanosleep(&(struct timespec){0, WAIT_TIME_MS * 500000L}, NULL);

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
    app.toolbar_h = MIN_BAR_HEIGHT;
    app.hintbar_h = MIN_BAR_HEIGHT;
    app.win_w = DEFAULT_WIDTH;
    app.win_h = DEFAULT_HEIGHT;
    app.resize_w = app.win_w;
    app.resize_h = app.win_h;
    app.pan_x = 0;
    app.pan_y = 0;
    app.dragging = FALSE;
    app.zoom_scale = 1.0;
    app.drag_mode = FALSE;
    app.zoom_mode = FALSE;
    clock_gettime(CLOCK_MONOTONIC, &app.last_zoom_time);
    app.last_drag_time = app.last_zoom_time;
    app.last_resize_time = app.last_zoom_time;
    app.zooming = FALSE;

    app.sequence.count = 0;
    app.sequence.current_index = 0;



    app.dpy = XOpenDisplay(NULL);
    if (!app.dpy) fatal("Cannot open X display");
    app.screen = DefaultScreen(app.dpy);
    app.win = XCreateSimpleWindow(app.dpy, RootWindow(app.dpy, app.screen),
                                  0, 0, app.win_w, app.win_h, 0,
                                  BlackPixel(app.dpy, app.screen),
                                  WhitePixel(app.dpy, app.screen));

    // Load SVGs: from args or stdin
    gboolean check_input = FALSE;


    // Read from stdin
    GError *err = NULL;
    gchar *content = NULL;
    gsize len = 0;
    if(has_stdin){
        if (!g_file_get_contents("/dev/stdin", &content, &len, &err)) {
        fprintf(stderr, "Ignore stdin: %s\n", err->message);
        g_clear_error(&err);
        }else{
        check_input = svg_sequence_load_from_stream(&app.sequence, content, len);
        g_free(content);
        }
    }
    if (argc >= 2 && strcmp(argv[1], "-") != 0) {
        // Read from file paths
        check_input = svg_sequence_load_files(&app.sequence, &argv[1], argc - 1);
    }
    app.sequence.current_index = 0;


    if (!check_input || app.sequence.count == 0) {
        fprintf(stderr, "Failed to load SVG sequence. Exiting.\n");
        return 1;
    }

    create_cairo(&app);
    adjust_bar(&app);
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
