#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <Xm/Label.h>
#include <Xm/Form.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/TextF.h>
#include <Xm/CascadeB.h>
#include <Xm/FileSB.h>
#include <X11/Xlib.h>
#include <X11/xpm.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include "tinyspline.h"
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <X11/cursorfont.h>
#include <stdint.h>
#include "PSLib.h"

// Tolerance in screen pixels for object snap
#define OSNAP_TOL_PIXELS 10
#define MAX_TEMP_POINTS 1024
static double rubber_x[MAX_TEMP_POINTS];
static double rubber_y[MAX_TEMP_POINTS];
static int rubber_count = 0;


static tsBSpline spline;
static int spline_ready = 0;
static EntityType current_entity_mode = ENTITY_NONE; // default mode

static IconAtlas toolbar_icons = {0};

Pixmap load_png_to_pixmap(Display *dpy, Window win, const char *filename,
                          int *w, int *h) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) { perror("fopen"); return None; }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (!png || !info) { fclose(fp); return None; }

    if (setjmp(png_jmpbuf(png))) { fclose(fp); png_destroy_read_struct(&png, &info, NULL); return None; }

    png_init_io(png, fp);
    png_read_info(png, info);

    int width  = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth  = png_get_bit_depth(png, info);

    // Ensure RGBA
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png, info);

    png_bytep *row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
    for (int y=0; y<height; y++) {
        row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png, info));
    }

    png_read_image(png, row_pointers);
    fclose(fp);

    // Convert to XImage
    int screen = DefaultScreen(dpy);
    Visual *visual = DefaultVisual(dpy, screen);
    int depth = DefaultDepth(dpy, screen);

    XImage *ximg = XCreateImage(dpy, visual, depth, ZPixmap, 0,
                                malloc(width * height * 4), width, height, 32, 0);

    for (int y=0; y<height; y++) {
        png_bytep row = row_pointers[y];
        for (int x=0; x<width; x++) {
            png_bytep px = &(row[x * 4]);
            unsigned long pixel = (px[0]<<16) | (px[1]<<8) | (px[2]); // RGB only
            XPutPixel(ximg, x, y, pixel);
        }
        free(row);
    }
    free(row_pointers);

    Pixmap pixmap = XCreatePixmap(dpy, win, width, height, depth);
    GC gc = XCreateGC(dpy, pixmap, 0, NULL);
    XPutImage(dpy, pixmap, gc, ximg, 0, 0, 0, 0, width, height);
    XFreeGC(dpy, gc);
    XDestroyImage(ximg);

    *w = width;
    *h = height;
    return pixmap;
}

void load_icon_atlas(Display *dpy, Window win, const char *filename, int icon_w, int icon_h) {
    int w,h;
    Pixmap pm = load_png_to_pixmap(dpy, win, filename, &w, &h);
    toolbar_icons.pixmap = pm;
    toolbar_icons.width = w;
    toolbar_icons.height = h;
    toolbar_icons.icon_w = icon_w;
    toolbar_icons.icon_h = icon_h;
}

Pixmap extract_icon(Display *dpy, Window win, int index) {
    if (!toolbar_icons.pixmap) return None;

    int cols = toolbar_icons.width / toolbar_icons.icon_w;
    int row = index / cols;
    int col = index % cols;

    int sx = col * toolbar_icons.icon_w;
    int sy = row * toolbar_icons.icon_h;

    Pixmap sub = XCreatePixmap(dpy, win,
        toolbar_icons.icon_w, toolbar_icons.icon_h,
        DefaultDepth(dpy, DefaultScreen(dpy)));

    GC gc = XCreateGC(dpy, win, 0, NULL);
    XCopyArea(dpy, toolbar_icons.pixmap, sub, gc,
              sx, sy,
              toolbar_icons.icon_w, toolbar_icons.icon_h,
              0, 0);
    XFreeGC(dpy, gc);

    return sub;
}

// 複製 spline
void ps_spline_copy_(tsBSpline *dest) {
    if (!spline_ready) return;
    ts_bspline_copy(&spline, dest, NULL);
}

// 讀取控制點
void ps_spline_get_ctrlp_(double *ctrlp_out) {
    if (!spline_ready) return;
    tsReal *cp = NULL;
    ts_bspline_control_points(&spline, &cp, NULL);
    size_t n = ts_bspline_len_control_points(&spline);
    for (size_t i = 0; i < n; i++) ctrlp_out[i] = cp[i];
}

// 使用預設 knots 自動設
void ps_spline_set_knots_(double *knots_in, int *n_knots, int *status_out) {
    if (!spline_ready) return;

    tsStatus status;
    tsError err = ts_bspline_set_knots(&spline,
                                       (const tsReal *)knots_in,
                                       &status);
    if (status_out) *status_out = (err == TS_SUCCESS) ? 0 : 1;

    if (err != TS_SUCCESS)
        fprintf(stderr, "Set knots failed: %s\n", status.message);
}

// 插入 knot
void ps_spline_insert_knot_(double *u, int *n, int *k_out) {
    if (!spline_ready) return;
    size_t k;
    ts_bspline_insert_knot(&spline, *u, (size_t)*n, &spline, &k, NULL);
    *k_out = (int)k;
}

// 拆解為 Bezier
void ps_spline_to_beziers_(tsBSpline *out) {
    if (!spline_ready) return;
    ts_bspline_to_beziers(&spline, out, NULL);
}

// Spline 插值
void ps_spline_interpolate_cubic_natural_(double *points, int *n, int *dim, int *status_out) {
    tsStatus status;
    tsError err = ts_bspline_interpolate_cubic_natural(
        (const tsReal *)points,
        (size_t)*n,
        (size_t)*dim,
        &spline,
        &status
    );

    spline_ready = (err == TS_SUCCESS);
    if (status_out)
        *status_out = (err == TS_SUCCESS) ? 0 : 1;

    if (err != TS_SUCCESS)
        fprintf(stderr, "Interpolation failed: %s\n", status.message);
}

void ps_spline_interpolate_catmull_rom_(double *points, int *n, int *dim,
                                    double *alpha, double *epsilon,
                                    int *status_out)
{
    tsStatus status;
    tsError err;

    // 你可以不提供 first/last，即可建立「非接觸」端點的自然 Catmull-Rom spline
    err = ts_bspline_interpolate_catmull_rom(
        (const tsReal *)points,
        (size_t)*n,
        (size_t)*dim,
        (tsReal)*alpha,
        NULL,   // first
        NULL,   // last
        (tsReal)*epsilon,
        &spline,
        &status
    );

    spline_ready = (err == TS_SUCCESS);
    if (status_out)
        *status_out = (err == TS_SUCCESS) ? 0 : 1;

    if (err != TS_SUCCESS)
        fprintf(stderr, "Catmull-Rom interpolation failed: %s\n", status.message);
}


// 評估單一 u
void ps_spline_eval_(double *u, double *out) {
    if (!spline_ready) return;
    tsDeBoorNet net = ts_deboornet_init();
    ts_bspline_eval(&spline, *u, &net, NULL);
    size_t d = ts_deboornet_dimension(&net);
    const tsReal *pts = ts_deboornet_points_ptr(&net);
    for (size_t i=0;i<d;i++) out[i] = pts[i];
    ts_deboornet_free(&net);
}

// 分割 splines
void ps_spline_split_(double *u, tsBSpline *out, int *k_out) {
    if (!spline_ready) return;
    size_t k;
    ts_bspline_split(&spline, *u, out, &k, NULL);
    *k_out = (int)k;
}

// 計算字元數量
void ps_spline_degree_(int *deg_out) {
    if (!spline_ready) return;
    *deg_out = (int)ts_bspline_degree(&spline);
}

// 計算 knot range
void ps_spline_domain_(double *min, double *max) {
    if (!spline_ready) return;
    ts_bspline_domain(&spline, min, max);
}

// 是否 Closed
void ps_spline_is_closed_(double *eps, int *closed_out) {
    if (!spline_ready) return;
    ts_bspline_is_closed(&spline, *eps, closed_out, NULL);
}

// 計算 chord lengths
void ps_spline_chord_lengths_(double *knots, int *num, double *lengths) {
    ts_bspline_chord_lengths(&spline, knots, (size_t)*num, lengths, NULL);
}

// 等距 knot seq
void ps_spline_equidistant_knots_(double *knots, int *num_knot_seq, int *num_samples) {
    ts_chord_lengths_equidistant_knot_seq(knots, NULL, 0, (size_t)*num_knot_seq, knots, NULL);
}

// Vector math
void ps_vec2_init_(double *out, double *x, double *y) { ts_vec2_init(out, *x, *y); }
void ps_vec3_init_(double *out, double *x, double *y, double *z) { ts_vec3_init(out, *x,*y,*z); }
void ps_vec4_init_(double *out, double *x, double *y, double *z, double *w) { ts_vec4_init(out,*x,*y,*z,*w); }
void ps_vec_add_(double *x, double *y, int *dim, double *out) { ts_vec_add(x, y, (size_t)*dim, out); }
void ps_vec_sub_(double *x, double *y, int *dim, double *out) { ts_vec_sub(x, y, (size_t)*dim, out); }
double ps_vec_dot_(double *x, double *y, int *dim) { return ts_vec_dot(x, y, (size_t)*dim); }
double ps_vec_mag_(double *x, int *dim) { return ts_vec_mag(x, (size_t)*dim); }
void ps_vec_norm_(double *x, int *dim, double *out) { ts_vec_norm(x, (size_t)*dim, out); }
void ps_vec_cross_(double *x, double *y, double *out) { ts_vec3_cross(x,y,out); }

// 設定變距容差
int ps_knots_equal_(double *x, double *y) { return ts_knots_equal(*x,*y); }

void ps_spline_new_(int *degree, int *dim, int *n_ctrlp, int *type, int *status) {
    tsStatus stat;
    tsError err = ts_bspline_new((size_t)(*n_ctrlp), (size_t)(*dim), (size_t)(*degree), (tsBSplineType)(*type), &spline, &stat);
    spline_ready = (err == TS_SUCCESS);
    *status = spline_ready ? 0 : 1;
}


void ps_spline_set_ctrlp_(double *ctrlp, int *status) {
    if (!spline_ready) { *status=1; return; }
    tsStatus stat;
    tsError err = ts_bspline_set_control_points(&spline, ctrlp, &stat);
    *status = (err == TS_SUCCESS) ? 0 : 1;
}


void ps_spline_sample_(int *sample_count, double *samples_out, int *status_out) {
    tsReal *samples = NULL;
    size_t actual = 0;
    tsStatus status;

    tsError err = ts_bspline_sample(&spline, (size_t)*sample_count, &samples, &actual, &status);
    if (err != TS_SUCCESS) {
        *status_out = status.code;
        return;
    }

    size_t dim = ts_bspline_dimension(&spline);
    for (size_t i = 0; i < actual * dim; i++) {
        samples_out[i] = samples[i];
    }

    free(samples);
    *status_out = 0;
}

void ps_spline_free_() {
    if (spline_ready) ts_bspline_free(&spline);
    spline_ready = 0;
}

static Entity *entity_list = NULL;   // head of linked list
Entity* add_line(double x1, double y1, double x2, double y2) {
    Entity *e = (Entity *)malloc(sizeof(Entity));
    e->type = ENTITY_LINE;
    e->data.line.x1 = x1;
    e->data.line.y1 = y1;
    e->data.line.x2 = x2;
    e->data.line.y2 = y2;
    e->next = entity_list;
    entity_list = e;
    return e;
}

Entity* add_arc(double cx, double cy, double r, double a1, double a2) {
    Entity *e = (Entity *)malloc(sizeof(Entity));
    e->type = ENTITY_ARC;
    e->data.arc.cx = cx;
    e->data.arc.cy = cy;
    e->data.arc.r = r;
    e->data.arc.startAng = a1;
    e->data.arc.sweepAng = a2;
    e->next = entity_list;
    entity_list = e;
    return e;
}

Entity* add_rect(double x1, double y1, double x2, double y2) {
    Entity *e = (Entity *)malloc(sizeof(Entity));
    e->type = ENTITY_RECT;
    e->data.rect.x1 = x1;
    e->data.rect.y1 = y1;
    e->data.rect.x2 = x2;
    e->data.rect.y2 = y2;
    e->next = entity_list;
    entity_list = e;
    return e;
}

Entity* add_polyline(int npts, double *x, double *y) {
    Entity *e = (Entity *)malloc(sizeof(Entity));
    e->type = ENTITY_POLYLINE;
    e->data.pline.npts = npts;
    e->data.pline.x = (double*)malloc(npts * sizeof(double));
    e->data.pline.y = (double*)malloc(npts * sizeof(double));
    memcpy(e->data.pline.x, x, npts * sizeof(double));
    memcpy(e->data.pline.y, y, npts * sizeof(double));
    e->next = entity_list;
    entity_list = e;
    return e;
}

// --- Add a new spline entity ---
Entity* add_spline(double *ctrlx, double *ctrly, int n_ctrlp, int degree) {
    if (n_ctrlp <= 0) return NULL;

    Entity *e = (Entity *)malloc(sizeof(Entity));
    if (!e) return NULL;

    e->type = ENTITY_SPLINE;  // make sure ENTITY_SPLINE is defined in EntityType enum
    e->data.spline.n_ctrlp = n_ctrlp;
    e->data.spline.degree = degree;

    // allocate memory for control points
    e->data.spline.x = (double *)malloc(sizeof(double) * n_ctrlp);
    e->data.spline.y = (double *)malloc(sizeof(double) * n_ctrlp);
    if (!e->data.spline.x || !e->data.spline.y) {
        free(e->data.spline.x);
        free(e->data.spline.y);
        free(e);
        return NULL;
    }

    // copy control points
    for (int i = 0; i < n_ctrlp; i++) {
        e->data.spline.x[i] = ctrlx[i];
        e->data.spline.y[i] = ctrly[i];
    }

    // insert at head of list
    e->next = entity_list;
    entity_list = e;

    return e;
}


static AppWidgets app;
static ViewState view = {400.0, 300.0, 1.0, 0, 0, 0, NULL};
static char current_filename[256] = "Untitled";
static int protected_len = 0; // cumulative protected text length

static int osnap_enabled = 0;     // toggled with F3
static int snap_active = 0;       // 1 if a snap point is currently under cursor
static double snap_x, snap_y;     // snapped world coordinates
static Entity *selected_entity = NULL;

void delete_entity(Entity *target) {
    if (!target) return;

    Entity **pp = &entity_list;
    while (*pp) {
        if (*pp == target) {
            Entity *to_delete = *pp;
            *pp = to_delete->next;

            // free memory depending on type
            if (to_delete->type == ENTITY_POLYLINE) {
                free(to_delete->data.pline.x);
                free(to_delete->data.pline.y);
            } else if (to_delete->type == ENTITY_SPLINE) {
                free(to_delete->data.spline.x);
                free(to_delete->data.spline.y);
            }

            free(to_delete);
            break;
        }
        pp = &(*pp)->next;
    }

    selected_entity = NULL;
}

/* --- Utility functions --- */
static char *trim(char *str) {
    while (isspace(*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    end[1] = '\0';
    return str;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (!p) { perror("malloc"); abort(); }
    memcpy(p, s, n);
    return p;
}


/* --- Coordinate transforms --- */
void screen_to_world(int x, int y, double *wx, double *wy) {
    *wx = (x - view.offsetX) / view.scale;
    *wy = -(y - view.offsetY) / view.scale;
}

void world_to_screen(double wx, double wy, int *sx, int *sy) {
    *sx = (int)(wx * view.scale + view.offsetX);
    *sy = (int)(-wy * view.scale + view.offsetY);
}

#define MAX_ACTIONS 10
static Action action_table[MAX_ACTIONS];
static int action_count = 0;

// Fortran-callable: REGISTER_ACTION(name, func)
void register_action_(char *name, ActionFunc func, int name_len) {
    if (action_count >= MAX_ACTIONS) return;

    // Copy and null-terminate name
    char cname[32];
    int len = name_len < 31 ? name_len : 31;
    strncpy(cname, name, len);
    cname[len] = '\0';

    // Trim trailing spaces (Fortran-style)
    for (int i = len - 1; i >= 0 && cname[i] == ' '; i--)
        cname[i] = '\0';

    strncpy(action_table[action_count].name, cname, 31);
    action_table[action_count].func = func;
    action_count++;
   }

void trigger_action(const char *name) {
    for (int i = 0; i < action_count; ++i) {
        if (strcmp(action_table[i].name, name) == 0) {
            if (action_table[i].func) {
                action_table[i].func();
                return;
            }
        }
    }
    printf("No action registered for '%s'\n", name);
}

void draw_arrow(Display *dpy, Drawable win, GC gc, int x1, int y1, int x2, int y2) {
    XDrawLine(dpy, win, gc, x1, y1, x2, y2);
    double angle = atan2(y2 - y1, x2 - x1);
    int size = 10;
    int ax1 = (int)(x2 - size * cos(angle - M_PI / 6));
    int ay1 = (int)(y2 - size * sin(angle - M_PI / 6));
    int ax2 = (int)(x2 - size * cos(angle + M_PI / 6));
    int ay2 = (int)(y2 - size * sin(angle + M_PI / 6));
    XDrawLine(dpy, win, gc, x2, y2, ax1, ay1);
    XDrawLine(dpy, win, gc, x2, y2, ax2, ay2);
}

// returns malloc'ed double array [x0,y0, x1,y1, ...] and sets *out_count (number of points).
// returns NULL on error (out_count set to 0).
static double *sample_spline_entity(const Entity *e, int *out_count) {
    *out_count = 0;
    if (!e || e->type != ENTITY_SPLINE || e->data.spline.n_ctrlp < 2) return NULL;

    // Estimate sample_count based on bounding box and current view.scale
    double minx = e->data.spline.x[0], maxx = e->data.spline.x[0];
    double miny = e->data.spline.y[0], maxy = e->data.spline.y[0];
    for (int i = 1; i < e->data.spline.n_ctrlp; ++i) {
        if (e->data.spline.x[i] < minx) minx = e->data.spline.x[i];
        if (e->data.spline.x[i] > maxx) maxx = e->data.spline.x[i];
        if (e->data.spline.y[i] < miny) miny = e->data.spline.y[i];
        if (e->data.spline.y[i] > maxy) maxy = e->data.spline.y[i];
    }
    double approx_len = (maxx - minx) + (maxy - miny);
    double pixel_len = approx_len * view.scale;
    int sample_count = (int)(pixel_len);
    if (sample_count < 32) sample_count = 32;
    if (sample_count > 2000) sample_count = 2000; // safety cap

    // Interleave control points into a temporary buffer
    size_t nctrl = (size_t)e->data.spline.n_ctrlp;
    double *ctrlp = malloc(sizeof(double) * 2 * nctrl);
    if (!ctrlp) return NULL;
    for (size_t i = 0; i < nctrl; ++i) {
        ctrlp[2*i]   = e->data.spline.x[i];
        ctrlp[2*i+1] = e->data.spline.y[i];
    }

    // Interpolate Catmull-Rom to a bspline and sample it
    tsStatus status;
    tsBSpline curve;
    tsError err = ts_bspline_interpolate_catmull_rom(
        ctrlp, (size_t)e->data.spline.n_ctrlp, (size_t)2,
        0.5,              // tension (Catmull-Rom)
        NULL, NULL,
        1e-6,             // epsilon
        &curve, &status);

    free(ctrlp);
    if (err != TS_SUCCESS) {
        // interpolation failed
        *out_count = 0;
        return NULL;
    }

    double *samples = NULL;
    size_t actual = 0;
    err = ts_bspline_sample(&curve, (size_t)sample_count, &samples, &actual, &status);
    // free curve resources (even on error)
    ts_bspline_free(&curve);

    if (err != TS_SUCCESS || actual == 0 || samples == NULL) {
        if (samples) free(samples);
        *out_count = 0;
        return NULL;
    }

    // Note: samples is malloc'ed by TinySpline; we return it (caller must free).
    *out_count = (int)actual;
    return samples;
}

/* --- Drawing handler --- */
void redraw(Widget w, XtPointer client_data, XtPointer call_data) {
    Window win = XtWindow(w);
    if (!win) return;

    Display *dpy = XtDisplay(w);
    GC gc = XCreateGC(dpy, win, 0, NULL);
    XClearWindow(dpy, win);

    int width = 0, height = 0;
    XtVaGetValues(w, XmNwidth, &width, XmNheight, &height, NULL);

    XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));

    int x0, y0;
    world_to_screen(0, 0, &x0, &y0);
    draw_arrow(dpy, win, gc, 0, y0, width, y0);
    draw_arrow(dpy, win, gc, x0, height, x0, 0);

    int spacing = 50;
    XFontStruct *font = XLoadQueryFont(dpy, "fixed");
    if (font) XSetFont(dpy, gc, font->fid);

    for (int i = -1000; i <= 1000; i += spacing) {
        char label[16];
        int sx, sy;
        if (i != 0) {
            world_to_screen(i, 0, &sx, &sy);
            if (sx >= 0 && sx <= width)
                XDrawLine(dpy, win, gc, sx, y0 - 4, sx, y0 + 4);
            snprintf(label, sizeof(label), "%d", i);
            if (sx >= 0 && sx <= width)
                XDrawString(dpy, win, gc, sx - 10, y0 + 15, label, strlen(label));

            world_to_screen(0, i, &sx, &sy);
            if (sy >= 0 && sy <= height)
                XDrawLine(dpy, win, gc, x0 - 4, sy, x0 + 4, sy);
            snprintf(label, sizeof(label), "%d", i);
            if (sy >= 0 && sy <= height)
                XDrawString(dpy, win, gc, x0 + 6, sy + 5, label, strlen(label));
        }
    }

    // Redraw all stored entities

    static unsigned long highlight_pixel = 0;
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
    XColor screen_def, exact_def;
    if (XAllocNamedColor(dpy, cmap, "red", &screen_def, &exact_def)) {
        highlight_pixel = screen_def.pixel;
    } else {
        /* fallback: try white, then black */
        highlight_pixel = WhitePixel(dpy, DefaultScreen(dpy));
        if (highlight_pixel == WhitePixel(dpy, DefaultScreen(dpy)))
            /* ok */;
        else
            highlight_pixel = BlackPixel(dpy, DefaultScreen(dpy));
    }

    Entity *e = entity_list;
    XGCValues values;
    values.foreground = highlight_pixel;  // e.g., red pixel
    values.line_width = 2;
    GC highlight_gc = XCreateGC(dpy, win, GCForeground|GCLineWidth, &values);
    GC normal_gc=gc;
    while (e) {
        if (e == selected_entity) {
            gc=highlight_gc;
        }else{
            gc=normal_gc;
        }

        if (e->type == ENTITY_LINE) {
            int sx1, sy1, sx2, sy2;
            world_to_screen(e->data.line.x1, e->data.line.y1, &sx1, &sy1);
            world_to_screen(e->data.line.x2, e->data.line.y2, &sx2, &sy2);
            XDrawLine(dpy, win, gc, sx1, sy1, sx2, sy2);
        } else if (e->type == ENTITY_ARC) {
            int scx, scy;
            world_to_screen(e->data.arc.cx, e->data.arc.cy, &scx, &scy);
            int sr = (int)(e->data.arc.r * view.scale);

            double a1 = e->data.arc.startAng;
            double sweep = e->data.arc.sweepAng;

            // normalize angles into [-2π, 2π] range
            while (a1 < 0) a1 += 2*M_PI;
            while (a1 >= 2*M_PI) a1 -= 2*M_PI;
            if (sweep > 2*M_PI) sweep = fmod(sweep, 2*M_PI);
            if (sweep < -2*M_PI) sweep = fmod(sweep, -2*M_PI);

            // Convert to X11 units (counterclockwise, 1/64 degrees)
            int sStartAng = (int)(a1 * 180.0 / M_PI * 64);
            int sSweepAng = (int)(sweep * 180.0 / M_PI * 64);

            int sx = scx - sr;
            int sy = scy - sr;
            int sw = sr * 2;
            int sh = sr * 2;

            XDrawArc(dpy, win, gc, sx, sy, sw, sh, sStartAng, sSweepAng);
        } else if (e->type == ENTITY_POLYLINE) {
                XPoint *pts = (XPoint*)malloc(e->data.pline.npts * sizeof(XPoint));
                for (int i=0; i<e->data.pline.npts; i++) {
                    int sx, sy;
                    world_to_screen(e->data.pline.x[i], e->data.pline.y[i], &sx, &sy);
                    pts[i].x = (short)sx;
                    pts[i].y = (short)sy;
                }
                XDrawLines(dpy, win, gc, pts, e->data.pline.npts, CoordModeOrigin);
                free(pts);
        } else if (e->type == ENTITY_RECT) {
                int sx1, sy1, sx2, sy2;
                world_to_screen(e->data.rect.x1, e->data.rect.y1, &sx1, &sy1);
                world_to_screen(e->data.rect.x2, e->data.rect.y2, &sx2, &sy2);

                int x = (sx1 < sx2) ? sx1 : sx2;
                int y = (sy1 < sy2) ? sy1 : sy2;
                int w = abs(sx2 - sx1);
                int h = abs(sy2 - sy1);

                XDrawRectangle(dpy, win, gc, x, y, w, h);
        } else if (e->type == ENTITY_SPLINE) {
            int actual_count = 0;
            double *samples = sample_spline_entity(e, &actual_count);
            if (samples && actual_count >= 2) {
                int sx_prev, sy_prev, sx, sy;
                // draw with normal GC
                for (int i = 0; i < actual_count; ++i) {
                    world_to_screen(samples[2*i], samples[2*i+1], &sx, &sy);
                    if (i > 0) XDrawLine(dpy, win, gc, sx_prev, sy_prev, sx, sy);
                    sx_prev = sx; sy_prev = sy;
                }

                // if selected, draw highlight on top using hgc (thicker / colored)
                if (e == selected_entity) {
                    for (int i = 1; i < actual_count; ++i) {
                        int sx1, sy1, sx2, sy2;
                        world_to_screen(samples[2*(i-1)], samples[2*(i-1)+1], &sx1, &sy1);
                        world_to_screen(samples[2*i], samples[2*i+1], &sx2, &sy2);
                        XDrawLine(dpy, win, gc, sx1, sy1, sx2, sy2);
                    }
                }
            }
            if (samples) free(samples);
        }
        e = e->next;
    }

    // --- Draw snap indicator if active ---
    if (osnap_enabled && snap_active) {
        int sx, sy;
        world_to_screen(snap_x, snap_y, &sx, &sy);
        int size = 6;
        XDrawLine(dpy, win, gc, sx+size, sy-size, sx+size, sy+size);
        XDrawLine(dpy, win, gc, sx+size, sy+size, sx-size, sy+size);
        XDrawLine(dpy, win, gc, sx-size, sy+size, sx-size, sy-size);
        XDrawLine(dpy, win, gc, sx-size, sy-size, sx+size, sy-size);
    }

    if (font) XFreeFont(dpy, font);
    XFreeGC(dpy, normal_gc);
    XFreeGC(dpy, highlight_gc);
}

static int snap_to_entity(double wx, double wy, double *sx, double *sy) {
    double best_dist2 = 1e30;
    double bestx = wx, besty = wy;

    for (Entity *e = entity_list; e; e = e->next) {
        if (e->type == ENTITY_LINE) {
            double px[2] = {e->data.line.x1, e->data.line.x2};
            double py[2] = {e->data.line.y1, e->data.line.y2};
            for (int i=0; i<2; i++) {
                int sxp, syp, mx, my;
                world_to_screen(px[i], py[i], &sxp, &syp);
                world_to_screen(wx, wy, &mx, &my);
                double dx = mx - sxp, dy = my - syp;
                double d2 = dx*dx + dy*dy;
                if (d2 < OSNAP_TOL_PIXELS*OSNAP_TOL_PIXELS && d2 < best_dist2) {
                    best_dist2 = d2;
                    bestx = px[i]; besty = py[i];
                }
            }
        }
        else if (e->type == ENTITY_ARC) {
            int sxp, syp, mx, my;
            world_to_screen(e->data.arc.cx, e->data.arc.cy, &sxp, &syp);
            world_to_screen(wx, wy, &mx, &my);
            double dx = mx - sxp, dy = my - syp;
            double d2 = dx*dx + dy*dy;
            if (d2 < OSNAP_TOL_PIXELS*OSNAP_TOL_PIXELS && d2 < best_dist2) {
                best_dist2 = d2;
                bestx = e->data.arc.cx; besty = e->data.arc.cy;
            }
        }
        else if (e->type == ENTITY_POLYLINE) {
            for (int i=0; i<e->data.pline.npts; i++) {
                int sxp, syp, mx, my;
                world_to_screen(e->data.pline.x[i], e->data.pline.y[i], &sxp, &syp);
                world_to_screen(wx, wy, &mx, &my);
                double dx = mx - sxp, dy = my - syp;
                double d2 = dx*dx + dy*dy;
                if (d2 < OSNAP_TOL_PIXELS*OSNAP_TOL_PIXELS && d2 < best_dist2) {
                    best_dist2 = d2;
                    bestx = e->data.pline.x[i]; besty = e->data.pline.y[i];
                }
            }
        }
        else if (e->type == ENTITY_SPLINE) {
            for (int i=0; i<e->data.spline.n_ctrlp; i++) {
                int sxp, syp, mx, my;
                world_to_screen(e->data.spline.x[i], e->data.spline.y[i], &sxp, &syp);
                world_to_screen(wx, wy, &mx, &my);
                double dx = mx - sxp, dy = my - syp;
                double d2 = dx*dx + dy*dy;
                if (d2 < OSNAP_TOL_PIXELS*OSNAP_TOL_PIXELS && d2 < best_dist2) {
                    best_dist2 = d2;
                    bestx = e->data.spline.x[i]; besty = e->data.spline.y[i];
                }
            }
        }
    }

    if (best_dist2 < 1e30) {
        *sx = bestx;
        *sy = besty;
        return 1;
    }
    return 0;
}

static double dist2(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1, dy = y2 - y1;
    return dx*dx + dy*dy;
}

static double point_seg_dist(double px, double py,
                             double x1, double y1, double x2, double y2) {
    double vx = x2 - x1, vy = y2 - y1;
    double wx = px - x1, wy = py - y1;
    double c1 = vx*wx + vy*wy;
    if (c1 <= 0) return sqrt(dist2(px,py,x1,y1));
    double c2 = vx*vx + vy*vy;
    if (c2 <= c1) return sqrt(dist2(px,py,x2,y2));
    double b = c1 / c2;
    double bx = x1 + b*vx, by = y1 + b*vy;
    return sqrt(dist2(px,py,bx,by));
}

void ps_entsel_(double *wx, double *wy, int *found) {
    double click_x = *wx;
    double click_y = *wy;
    double tol = 5.0 / view.scale;   // 5 pixels tolerance, convert to world

    Entity *best = NULL;
    double best_dist = 1e30;

    for (Entity *e = entity_list; e; e = e->next) {
        double d = 1e30;

        if (e->type == ENTITY_LINE) {
            d = point_seg_dist(click_x, click_y,
                               e->data.line.x1, e->data.line.y1,
                               e->data.line.x2, e->data.line.y2);
        } else if (e->type == ENTITY_ARC) {
            double dx = click_x - e->data.arc.cx;
            double dy = click_y - e->data.arc.cy;
            double r = sqrt(dx*dx + dy*dy);
            double ang = atan2(dy, dx);

            // Normalize angle into [0, 2π)
            if (ang < 0) ang += 2*M_PI;

            double a1 = e->data.arc.startAng;
            double sweep = e->data.arc.sweepAng;
            double a2 = a1 + sweep;

            // Normalize start and end
            while (a1 < 0) a1 += 2*M_PI;
            while (a1 >= 2*M_PI) a1 -= 2*M_PI;
            while (a2 < 0) a2 += 2*M_PI;
            while (a2 >= 2*M_PI) a2 -= 2*M_PI;

            int onArc = 0;
            if (sweep >= 0) {
                // CCW arc
                double span = sweep;
                if (span < 0) span += 2*M_PI;
                double rel = ang - a1;
                if (rel < 0) rel += 2*M_PI;
                if (rel <= span) onArc = 1;
            } else {
                // CW arc
                double span = -sweep;
                if (span < 0) span += 2*M_PI;
                double rel = a1 - ang;
                if (rel < 0) rel += 2*M_PI;
                if (rel <= span) onArc = 1;
            }

            if (onArc) {
                d = fabs(r - e->data.arc.r);
            }
        }
        else if (e->type == ENTITY_RECT) {
            // Rectangle edges
            double x1 = e->data.rect.x1;
            double y1 = e->data.rect.y1;
            double x2 = e->data.rect.x2;
            double y2 = e->data.rect.y2;

            // distance to 4 sides
            double d1 = point_seg_dist(click_x, click_y, x1, y1, x2, y1);
            double d2 = point_seg_dist(click_x, click_y, x2, y1, x2, y2);
            double d3 = point_seg_dist(click_x, click_y, x2, y2, x1, y2);
            double d4 = point_seg_dist(click_x, click_y, x1, y2, x1, y1);
            d = fmin(fmin(d1,d2), fmin(d3,d4));
        }
        else if (e->type == ENTITY_POLYLINE) {
            for (int i=0; i<e->data.pline.npts-1; i++) {
                double dd = point_seg_dist(click_x, click_y,
                                           e->data.pline.x[i], e->data.pline.y[i],
                                           e->data.pline.x[i+1], e->data.pline.y[i+1]);
                if (dd < d) d = dd;
            }
        }
        else if (e->type == ENTITY_SPLINE) {
            int actual_count = 0;
            double *samples = sample_spline_entity(e, &actual_count);
            if (samples && actual_count >= 2) {
                for (int i = 0; i < actual_count - 1; ++i) {
                    double dseg = point_seg_dist(click_x, click_y,
                                                samples[2*i], samples[2*i+1],
                                                samples[2*(i+1)], samples[2*(i+1)+1]);
                    if (dseg < d) d = dseg;
                }
            }
            if (samples) free(samples);
        }

        if (d < best_dist && d < tol) {
            best = e;
            best_dist = d;
        }
    }

    selected_entity = best;
    *found = (best != NULL);

    if (*found) {
        // Redraw everything with highlight
        redraw(app.drawArea, NULL, NULL);
    }
}

/* --- Event Handlers --- */
void mouse_event(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    if (event->type == ButtonPress) {
        if (event->xbutton.button == Button1) {   /* Left click */
            double wx, wy;
            screen_to_world(event->xbutton.x, event->xbutton.y, &wx, &wy);
            int found;
            ps_entsel_(&wx, &wy, &found);
            if (found) {
                redraw(w, NULL, NULL);  /* highlight via selected_entity */
            }
        }
        else if (event->xbutton.button == Button2) {
            Display *dpy = XtDisplay(w);
            Cursor cross = XCreateFontCursor(dpy, XC_hand1);
            Window win = XtWindow(w);
            XDefineCursor(dpy, win, cross);

            view.isPanning = 1;
            view.lastX = event->xbutton.x;
            view.lastY = event->xbutton.y;
        } else if (event->xbutton.button == Button4 || event->xbutton.button == Button5) {
            int x = event->xbutton.x, y = event->xbutton.y;
            double wx, wy;
            screen_to_world(x, y, &wx, &wy);
            double zoom = (event->xbutton.button == Button4) ? 1.1 : 0.9;
            view.scale *= zoom;
            view.offsetX = x - wx * view.scale;
            view.offsetY = y + wy * view.scale;
            redraw(w, NULL, NULL);
        }
    } else if (event->type == ButtonRelease) {
        if (event->xbutton.button == Button2){ view.isPanning = 0;
            Display *dpy = XtDisplay(w);
            Cursor cross = XCreateFontCursor(dpy, XC_crosshair);
            Window win = XtWindow(w);
            XDefineCursor(dpy, win, cross);
}
    } else if (event->type == MotionNotify) {
        double wx, wy;
        screen_to_world(event->xmotion.x, event->xmotion.y, &wx, &wy);
        char msg[128];
        snprintf(msg, sizeof(msg), "Mouse X: %.2f   Y: %.2f", wx, wy);
        if (view.status_label)
            XtVaSetValues(view.status_label, XmNlabelString, XmStringCreateLocalized(msg), NULL);

        if (view.isPanning) {
            int dx = event->xmotion.x - view.lastX;
            int dy = event->xmotion.y - view.lastY;
            view.offsetX += dx;
            view.offsetY += dy;
            view.lastX = event->xmotion.x;
            view.lastY = event->xmotion.y;
            redraw(w, NULL, NULL);
        }
    }
}

/* --- Command Handlers --- */
static void update_status(const char *msg) {
    if (app.statusBar) {
        XmString xm_msg = XmStringCreateLocalized((char *)msg);
        XtVaSetValues(app.statusBar, XmNlabelString, xm_msg, NULL);
        XmStringFree(xm_msg);
    }
}

void key_event(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    if (event->type == KeyPress) {
        KeySym keysym = XLookupKeysym(&event->xkey, 0);
        if (keysym == XK_F3) {
            osnap_enabled = !osnap_enabled;
            char msg[64];
            snprintf(msg, sizeof(msg), "OSNAP %s", osnap_enabled ? "ON" : "OFF");
            update_status(msg);
            if (!osnap_enabled) {
                snap_active = 0;
                redraw(w, NULL, NULL);
            }
        }
        else if (keysym == XK_Escape) {
            if (selected_entity) {
                selected_entity = NULL;
                redraw(w, NULL, NULL);
            }
        }else if (keysym == XK_Delete || keysym == XK_BackSpace) {
            if (selected_entity) {
                delete_entity(selected_entity);
                selected_entity = NULL;
                redraw(w, NULL, NULL);
            }
        }
    }
}

static void handle_command(const char *cmd) {
    char msg[128];
    if (strcasecmp(cmd, "exit") == 0) {
        exit(0);
    } else if (strcasecmp(cmd, "line") == 0 || strcasecmp(cmd, "arc") == 0 || strcasecmp(cmd, "rect") == 0) {
        snprintf(msg, sizeof(msg), "Command: %s", cmd);
        update_status(msg);
    } else {
        snprintf(msg, sizeof(msg), "Unknown command: %s", cmd);
        update_status(msg);
    }
}

static void command_input_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    char *text;
    XtVaGetValues(w, XmNvalue, &text, NULL);
    if (text && *text) {
        char *cmd = trim(text);
        if (*cmd) handle_command(cmd);
    }
    XmTextFieldSetString(w, "");
}

void menu_item_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    handle_command((const char *)client_data);
    const char *action = (const char *)client_data;
    trigger_action(action);

    if (strcmp(action, "EXIT") == 0) {
        XtDestroyApplicationContext(XtWidgetToApplicationContext(w));
        exit(0);  // optional: Fortran may also do STOP
    }
}

void toolbar_button_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    handle_command((const char *)client_data);
    const char *label = (const char *)client_data;
    trigger_action(label);  // Call Fortran subroutine
}

static Pixmap load_xpm(Display *dpy, Window win, const char *filename) {
    Pixmap pixmap = None;
    Pixmap mask = None;
    XpmAttributes attrs;
    attrs.valuemask = 0;
    int status = XpmReadFileToPixmap(dpy, win, (char *)filename, &pixmap, &mask, &attrs);
    if (status != XpmSuccess) {
        fprintf(stderr, "Failed to load XPM: %s\n", filename);
        return None;
    }
    if (mask != None) {
        Pixmap combined = XCreatePixmap(dpy, win, attrs.width, attrs.height, DefaultDepth(dpy, DefaultScreen(dpy)));
        GC gc = XCreateGC(dpy, combined, 0, NULL);
        XSetForeground(dpy, gc, 0xFFFFFF);
        XFillRectangle(dpy, combined, gc, 0, 0, attrs.width, attrs.height);
        XSetForeground(dpy, gc, 0x000000);
        XCopyArea(dpy, pixmap, combined, gc, 0, 0, attrs.width, attrs.height, 0, 0);
        XFreePixmap(dpy, pixmap);
        pixmap = combined;
        XFreeGC(dpy, gc);
        XFreePixmap(dpy, mask);
    }
    return pixmap;
}

static Widget create_menu_from_file(Widget parent, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Cannot open menu config file: %s\n", filename);
        exit(1);
    }

    Widget menuBar = XmCreateMenuBar(parent, "menuBar", NULL, 0);
    Widget currentMenu = NULL;
    char line[256];
    typedef struct {
        char id[64];
        Widget menu;
    } MenuEntry;
    MenuEntry menus[20];
    int menuCount = 0;

    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (strncmp(p, "MENU:", 5) == 0) {
            char *token = p + 5;
            char *id = strtok(token, ":");
            char *label = strtok(NULL, ":");
            if (id && label) {
                Widget pulldown = XmCreatePulldownMenu(parent, id, NULL, 0);
                Widget cascade = XtVaCreateManagedWidget(label, xmCascadeButtonWidgetClass, menuBar,
                    XmNsubMenuId, pulldown,
                    NULL);
                strcpy(menus[menuCount].id, id);
                menus[menuCount].menu = pulldown;
                menuCount++;
                currentMenu = pulldown;
            }
        } else if (strncmp(p, "ITEM:", 5) == 0) {
            char *token = p + 5;
            char *menu_id = strtok(token, ":");
            char *item_id = strtok(NULL, ":");
            char *label = strtok(NULL, ":");
            char *icon = strtok(NULL, ":");

            if (!menu_id || !label) continue;

            // Find menu by menu_id
            Widget menu = NULL;
            for (int i = 0; i < menuCount; i++) {
                if (strcmp(menus[i].id, menu_id) == 0) {
                    menu = menus[i].menu;
                    break;
                }
            }
            if (!menu) continue;

            Widget item;
            item = XmCreatePushButton(menu, item_id, NULL, 0);
            XtVaSetValues(item, XmNlabelString, XmStringCreateLocalized(label), NULL);
            XtManageChild(item);

            // Callback: pass command same as item_id
            XtAddCallback(item, XmNactivateCallback, menu_item_cb, strdup(item_id));
        }
    }
    fclose(f);

    XtManageChild(menuBar);
    return menuBar;
}


static Widget create_toolbar_from_file(Widget parent, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Cannot open menu config file: %s\n", filename);
        exit(1);
    }

    // Toolbar container: RowColumn horizontal
    Widget toolbar = XmCreateRowColumn(parent, "toolBar", NULL, 0);
    XtVaSetValues(toolbar, XmNorientation, XmHORIZONTAL, XmNpacking, XmPACK_TIGHT, XmNspacing, 0, NULL);

    // Load atlas once at startup
    load_icon_atlas(XtDisplay(parent), DefaultRootWindow(XtDisplay(parent)),
                "icons/icons.png", 32, 32);


    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (strncmp(p, "TOOLBAR:", 8) == 0) {
            char *token = p + 8;
            char *menu_id = strtok(token, ":");
            char *icon_path = strtok(NULL, ": \n");
            if (icon_path && strlen(icon_path) > 0) {
                //char icon_path[256];
                //snprintf(icon_path, sizeof(icon_path), "icons/%s.xpm", cmd);
                //Pixmap pix = load_xpm(XtDisplay(parent), DefaultRootWindow(XtDisplay(parent)), icon_path);
                int icon_index = atoi(icon_path);
                Pixmap pix = extract_icon(XtDisplay(parent), DefaultRootWindow(XtDisplay(parent)), icon_index);

                Arg args[3];
                XmString xm_label = XmStringCreateLocalized(menu_id);
                int n = 0;
                XtSetArg(args[n], XmNlabelString, xm_label); n++;
                if (pix != None) {
                    XtSetArg(args[n], XmNlabelPixmap, pix); n++;
                    XtSetArg(args[n], XmNlabelType, XmPIXMAP); n++;
                }
                Widget btn = XmCreatePushButton(toolbar, menu_id, args, n);
                XmStringFree(xm_label);
                XtManageChild(btn);

                XtAddCallback(btn, XmNactivateCallback, toolbar_button_cb, strdup(menu_id));
                //XtAddCallback(btn, XmNactivateCallback, toolbar_button_cb, (XtPointer)xstrdup(menu_id));

            }
        }
    }

    fclose(f);
    XtManageChild(toolbar);
    return toolbar;
}

void ps_wait_click_(int *x, int *y) {
    XEvent event;
    Display *dpy = XtDisplay(app.drawArea);
    Window win = XtWindow(app.drawArea);

    // Flush any pending events
    XFlush(dpy);

    // Block until we get a ButtonPress
    while (1) {
        XNextEvent(dpy, &event);
        if (event.type == ButtonPress && event.xbutton.button == Button1 &&
            event.xbutton.window == win) {
            *x = event.xbutton.x;
            *y = event.xbutton.y;
            break;
        }
    }
}

void ps_draw_line_(double *x1, double *y1, double *x2, double *y2) {
    add_line(*x1, *y1, *x2, *y2);
    redraw(app.drawArea, NULL, NULL);
}

void ps_draw_rect_(double *x1, double *y1, double *x2, double *y2) {
    add_rect(*x1, *y1, *x2, *y2);
    redraw(app.drawArea, NULL, NULL);
}

// Fortran callable: draw a spline from control points
// ctrlp: array of length 2*n_ctrlp (x0,y0,x1,y1,...)
// n_ctrlp: number of control points
// degree: spline degree (e.g., 3)
// dim: dimension (2 for 2D)
// type: 0=open,1=closed,2=periodic
void ps_draw_spline_(double *ctrlp, int *n_ctrlp, int *degree, int *dim, int *type, int *status)
{
    if (*dim < 2 || *n_ctrlp <= 0) {
        *status = 1;
        return;
    }

    // Split ctrlp into separate x and y arrays
    double *ctrlx = malloc(sizeof(double) * (*n_ctrlp));
    double *ctrly = malloc(sizeof(double) * (*n_ctrlp));
    if (!ctrlx || !ctrly) {
        free(ctrlx); free(ctrly);
        *status = 1;
        return;
    }

    for (int i = 0; i < *n_ctrlp; i++) {
        ctrlx[i] = ctrlp[i * (*dim)];       // x
        ctrly[i] = ctrlp[i * (*dim) + 1];   // y
    }

    // Add spline to entity list
    Entity *e = add_spline(ctrlx, ctrly, *n_ctrlp, *degree);
    free(ctrlx);
    free(ctrly);

    if (!e) {
        *status = 1;
        return;
    }

    *status = 0;

    // Trigger redraw to show the new spline
    redraw(app.drawArea, NULL, NULL);
}

static void set_window_title(const char *fname) {
    if (!app.top) return;
    char buf[512];
    snprintf(buf, sizeof(buf), "PSLib Sample App - %s", fname);
    XtVaSetValues(app.top,
        XmNtitle,    buf,
        XmNiconName, buf,
        NULL);
}

void protect_prompt_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    XmTextVerifyCallbackStruct *cbs = (XmTextVerifyCallbackStruct *) call_data;

    // Block edits before protected_len
    if (cbs->startPos < protected_len) {
        cbs->doit = False;
        XmTextFieldSetInsertionPosition(w, protected_len);
    }
}

// start_gui_
void start_gui_() {
    int argc = 0;
    char** argv = NULL;
    XtAppContext appContext;
    app.top = XtVaAppInitialize(&appContext, "ZoomApp", NULL, 0, &argc, argv, NULL, NULL);

    app.form = XtVaCreateManagedWidget("form", xmFormWidgetClass, app.top, NULL);
    XtVaSetValues(app.top,
        XmNwidth, 800,
        XmNheight, 600,
        NULL);

    // --- Menu Bar at the top ---
    app.menuBar = create_menu_from_file(app.form, "menu.txt");
    XtVaSetValues(app.menuBar,
        XmNtopAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_FORM,
        NULL);

    // --- Tool Bar under menu bar ---
    app.toolBar = create_toolbar_from_file(app.form, "menu.txt");
    XtVaSetValues(app.toolBar,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, app.menuBar,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM,
                  NULL);
    XtManageChild(app.toolBar);

    // --- Status bar at the bottom ---
    app.statusBar = XtVaCreateManagedWidget("statusBar", xmLabelWidgetClass, app.form,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_FORM,
        XmNlabelString, XmStringCreateLocalized("Ready"),
        NULL);

    // --- Command input above status bar ---
    app.cmdInput = XmCreateTextField(app.form, "cmdInput", NULL, 0);
    XtManageChild(app.cmdInput);
    XtVaSetValues(app.cmdInput,
        XmNbottomAttachment, XmATTACH_WIDGET,
        XmNbottomWidget, app.statusBar,
        XmNleftAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_FORM,
        XmNcolumns, 40,
        XmNmarginHeight, 4,
        XmNmarginWidth, 4,
        NULL);
    XtAddCallback(app.cmdInput, XmNactivateCallback, command_input_cb, NULL);
    XtAddCallback(app.cmdInput, XmNmodifyVerifyCallback,
                protect_prompt_cb, NULL);

    // --- Drawing area between toolbar and cmd input ---
    app.drawArea = XtVaCreateManagedWidget("drawingArea", xmDrawingAreaWidgetClass, app.form,
        XmNtopAttachment, XmATTACH_WIDGET,
        XmNtopWidget, app.toolBar,
        XmNleftAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_WIDGET,
        XmNbottomWidget, app.cmdInput,
        NULL);

    // Set up event handlers and callbacks
    view.status_label = app.statusBar;

    XtAddCallback(app.drawArea, XmNexposeCallback, redraw, NULL);
    XtAddEventHandler(app.drawArea, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                      False, mouse_event, NULL);
    XtAddEventHandler(app.drawArea, KeyPressMask, False, key_event, NULL);

    // Realize and run
    XtRealizeWidget(app.top);
    Display *dpy = XtDisplay(app.drawArea);
    Window win = XtWindow(app.drawArea);
    Cursor cross = XCreateFontCursor(dpy, XC_crosshair);
    XDefineCursor(dpy, win, cross);

    set_window_title(current_filename);

    XtAppMainLoop(appContext);
}

static bool has_base_point = false;
static int base_sx, base_sy;

// --- Helpers ---

static void update_prompt(char *prompt, int prompt_len) {
    char local_prompt[256];
    int len = (prompt_len < 255) ? prompt_len : 255;
    strncpy(local_prompt, prompt, len);
    local_prompt[len] = '\0';

    if (!app.cmdInput) return;

    protected_len = 0;
    XmTextFieldSetString(app.cmdInput, local_prompt);
    XmUpdateDisplay(app.cmdInput);
    XmTextFieldSetInsertionPosition(app.cmdInput, strlen(local_prompt));
    XmProcessTraversal(app.cmdInput, XmTRAVERSE_CURRENT);
    protected_len = strlen(local_prompt);
}

static void handle_zoom(XButtonEvent *bev) {
    int sx = bev->x, sy = bev->y;
    double wx, wy;
    screen_to_world(sx, sy, &wx, &wy);

    double zoom = (bev->button == Button4) ? 1.1 : 0.9;
    view.scale *= zoom;
    view.offsetX = sx - wx * view.scale;
    view.offsetY = sy + wy * view.scale;
    redraw(app.drawArea, NULL, NULL);
}

static void handle_osnap_toggle(KeySym key) {
    if (key == XK_F3) {
        osnap_enabled = !osnap_enabled;
        printf("Object Snap %s\n", osnap_enabled ? "ON" : "OFF");
    }
}

static void draw_rubberband(Display *dpy, Window win, GC gc,
                            double *px, double *py, int npoints,
                            int mx, int my)
{
    if (npoints < 1) return;

    // Convert all world coords to screen
    XPoint *pts = malloc((npoints + 1) * sizeof(XPoint));
    int sx, sy;
    for (int i = 0; i < npoints; i++) {
        world_to_screen(px[i], py[i], &sx, &sy);
    // Add the current mouse pos as the last "floating" point
        pts[i].x = (short) sx;
        pts[i].y = (short) sy;
    }

    pts[npoints].x = mx;
    pts[npoints].y = my;


    // Draw polyline
    if((current_entity_mode == ENTITY_RECT)&&(npoints==1)) {
        // Rubber-band rectangle
        int rx = (sx < mx) ? sx : mx;
        int ry = (sy < my) ? sy : my;
        int rw = abs(mx - sx);
        int rh = abs(my - sy);
        XDrawRectangle(dpy, win, gc, rx, ry, rw, rh);
    }else if ((current_entity_mode == ENTITY_LINE)||(npoints==1)){
        //XDrawLines(dpy, win, gc, pts, npoints, CoordModeOrigin);
        XDrawLine(dpy, win, gc, sx, sy, mx, my);
    } else if ((current_entity_mode == ENTITY_ARC)&& (npoints==2)){
        // Rubber band arc: 2 fixed points + current mouse point
        double x1 = px[0], y1 = py[0];
        double x2 = px[1], y2 = py[1];
        double x3, y3;
        screen_to_world(mx, my, &x3, &y3);

        double cx, cy, r, a1, a2, am;
        if (circle_from_3pts(x1, y1, x2, y2, x3, y3,
                            &cx, &cy, &r, &a1, &a2, &am)) {

            int scx, scy;
            world_to_screen(cx, cy, &scx, &scy);
            int sr = (int)(r * view.scale);

            // normalize to [0,2π)
            if (a1 < 0) a1 += 2*M_PI;
            if (a2 < 0) a2 += 2*M_PI;
            if (am < 0) am += 2*M_PI;

            double span = a2 - a1;
            if (span < 0) span += 2*M_PI;

            double rel = am - a1;
            if (rel < 0) rel += 2*M_PI;

            // If middle point not between a1→a2, reverse
            if (!(rel > 0 && rel < span)) {
                double tmp = a1;
                a1 = a2;
                a2 = tmp;
                span = 2*M_PI - span;
            }

            // Convert to X11 units (counterclockwise from 3 o'clock, degrees*64)
            int sStartAng = (int)(a1 * 180.0 / M_PI * 64);
            int sSpanAng  = (int)(span * 180.0 / M_PI * 64);

            int sx = scx - sr;
            int sy = scy - sr;
            int sw = sr * 2;
            int sh = sr * 2;

            XDrawArc(dpy, win, gc, sx, sy, sw, sh, sStartAng, sSpanAng);
        }    
    } else if (current_entity_mode == ENTITY_POLYLINE) {
    // Build a temporary Entity-like polyline
    Entity e;
    e.type = ENTITY_POLYLINE;
    e.data.pline.npts = npoints + 1;   // include floating mouse point
    e.data.pline.x = malloc(sizeof(double) * e.data.pline.npts);
    e.data.pline.y = malloc(sizeof(double) * e.data.pline.npts);

    // Copy fixed vertices
    for (int i = 0; i < npoints; i++) {
        e.data.pline.x[i] = px[i];
        e.data.pline.y[i] = py[i];
    }

    // Append floating last mouse position (convert screen → world first)
    double wx, wy;
    screen_to_world(mx, my, &wx, &wy);
    e.data.pline.x[npoints] = wx;
    e.data.pline.y[npoints] = wy;

    // Draw polyline preview
    if (e.data.pline.npts > 1) {
        XPoint *pts = malloc(e.data.pline.npts * sizeof(XPoint));
        for (int i = 0; i < e.data.pline.npts; i++) {
            int sx, sy;
            world_to_screen(e.data.pline.x[i], e.data.pline.y[i], &sx, &sy);
            pts[i].x = (short)sx;
            pts[i].y = (short)sy;
        }
        XDrawLines(dpy, win, gc, pts, e.data.pline.npts, CoordModeOrigin);
        free(pts);
    }

    free(e.data.pline.x);
    free(e.data.pline.y);
    }else if (current_entity_mode == ENTITY_SPLINE){ 
        // Build a temporary Entity-like spline
        Entity e;
        e.type = ENTITY_SPLINE;
        e.data.spline.n_ctrlp = npoints + 1; // include floating mouse point
        e.data.spline.x = malloc(sizeof(double) * e.data.spline.n_ctrlp);
        e.data.spline.y = malloc(sizeof(double) * e.data.spline.n_ctrlp);

        for (int i = 0; i < npoints; i++) {
            e.data.spline.x[i] = px[i];
            e.data.spline.y[i] = py[i];
        }

        // Append floating last mouse position (convert screen → world first)
        double wx, wy;
        screen_to_world(mx, my, &wx, &wy);
        e.data.spline.x[npoints] = wx;
        e.data.spline.y[npoints] = wy;

        // Sample spline
        int sample_count = 0;
        double *samples = sample_spline_entity(&e, &sample_count);

        if (samples && sample_count > 1) {
            XPoint *pts = malloc(sample_count * sizeof(XPoint));
            for (int i = 0; i < sample_count; i++) {
                int sx, sy;
                world_to_screen(samples[2*i], samples[2*i+1], &sx, &sy);
                pts[i].x = (short)sx;
                pts[i].y = (short)sy;
            }
            XDrawLines(dpy, win, gc, pts, sample_count, CoordModeOrigin);
            free(pts);
            free(samples);
        }

        free(e.data.spline.x);
        free(e.data.spline.y);
    }

    free(pts);
}

// --- Main point picker ---

void ps_getpoint_(char *prompt, double *x, double *y,
                  int *has_start, int prompt_len)
{
    Display *dpy = XtDisplay(app.drawArea);
    Window win = XtWindow(app.drawArea);
    GC gc = XCreateGC(dpy, win, 0, NULL);

    update_prompt(prompt, prompt_len);

    // base point (rubber band start)
    double x1 = 0, y1 = 0;
    bool has_base = (*has_start != 0);
    if (has_base) { 
        //if (rubber_count==0){
            x1 = *x; 
            y1 = *y; 
            rubber_x[rubber_count] = x1;
            rubber_y[rubber_count] = y1;
            rubber_count++;
        //}
    }

    XEvent event;
    bool done = false;

    while (!done) {
        XtAppNextEvent(XtWidgetToApplicationContext(app.drawArea), &event);

        // --- Keyboard focus ---
        if (event.type == KeyPress && app.cmdInput) {
            XmProcessTraversal(app.cmdInput, XmTRAVERSE_CURRENT);
        }

        // --- Mouse click handling ---
        if (event.type == ButtonPress) {
            if (event.xbutton.button == Button1) {        // left click
                screen_to_world(event.xbutton.x, event.xbutton.y, x, y);
                if (osnap_enabled && snap_active) {
                    snap_to_entity(*x, *y, x, y);
                }
                if (rubber_count < MAX_TEMP_POINTS) {
                    rubber_x[rubber_count] = *x;
                    rubber_y[rubber_count] = *y;
                    //rubber_count++;
                }
                *has_start = 1;
                if(((current_entity_mode == ENTITY_ARC) && (rubber_count==2))||
                    ((current_entity_mode == ENTITY_RECT) && (rubber_count==1))) {
                    // Arc needs 3 points: start, end, and a third point to define curvature
                    rubber_count=0;
                } 
                done = true;
            }
            else if (event.xbutton.button == Button3) {   // right click = cancel
                *has_start = 0;
                rubber_count=0;
                done = true;
            }
            else if (event.xbutton.button == Button4 ||
                     event.xbutton.button == Button5) {   // mouse wheel zoom
                handle_zoom(&event.xbutton);
            }
        }

        // --- Mouse motion: rubberband + snap preview ---
        else if (event.type == MotionNotify) {
            double wx, wy;
            screen_to_world(event.xmotion.x, event.xmotion.y, &wx, &wy);

            // snapping
            if (osnap_enabled) {
                double sx, sy;
                if (snap_to_entity(wx, wy, &sx, &sy)) {
                    snap_active = 1; snap_x = sx; snap_y = sy;
                } else {
                    snap_active = 0;
                }
                redraw(app.drawArea, NULL, NULL);
            }

            // rubberband
            if (has_base && rubber_count > 0) {
                XClearWindow(dpy, win);
                redraw(app.drawArea, NULL, NULL);
                draw_rubberband(dpy, win, gc, rubber_x, rubber_y, rubber_count,
                                event.xmotion.x, event.xmotion.y);
            }
        }

        // --- Keyboard input for typed coordinates ---
        else if (event.type == KeyPress && app.cmdInput) {
            KeySym keysym;
            char buf[256];
            int n = XLookupString(&event.xkey, buf, sizeof(buf) - 1, &keysym, NULL);
            buf[n] = '\0';

            if (keysym == XK_Return || keysym == XK_KP_Enter) {
                char *text = XmTextFieldGetString(app.cmdInput);
                if ((int)strlen(text) > protected_len) {
                    const char *coords = text + protected_len;
                    double tx, ty;
                    if (sscanf(coords, "%lf%*[, ]%lf", &tx, &ty) == 2) {
                        *x = tx; *y = ty;
                        *has_start = 1;
                        done = true;
                    }
                }
                XmTextFieldSetInsertionPosition(app.cmdInput, strlen(text));
                XtFree(text);
            }
            handle_osnap_toggle(keysym);
        }

        XtDispatchEvent(&event);
    }

    if (app.cmdInput)
        XmTextFieldSetInsertionPosition(app.cmdInput, protected_len);
}

static void free_entities() {
    Entity *e = entity_list;
    while (e) {
        Entity *next = e->next;
        if (e->type == ENTITY_POLYLINE) {
            free(e->data.pline.x);
            free(e->data.pline.y);
        }
        free(e);
        e = next;
    }
    entity_list = NULL;
}

// Save entities as binary
void ps_save_entities_(char *filename, int filename_len) {
    char fname[256];
    int len = (filename_len < 255) ? filename_len : 255;
    strncpy(fname, filename, len);
    fname[len] = '\0';

    FILE *f = fopen(fname, "wb");
    if (!f) { perror("fopen save"); return; }

    for (Entity *e = entity_list; e; e = e->next) {
        fwrite(&e->type, sizeof(int), 1, f);
        switch(e->type) {
            case ENTITY_LINE:
                fwrite(&e->data.line, sizeof(LineEntity), 1, f);
                break;
            case ENTITY_RECT:
                fwrite(&e->data.rect, sizeof(RectEntity), 1, f);
                break;
            case ENTITY_ARC:
                fwrite(&e->data.arc, sizeof(ArcEntity), 1, f);
                break;
            case ENTITY_POLYLINE: {
                fwrite(&e->data.pline.npts, sizeof(int), 1, f);
                fwrite(e->data.pline.x, sizeof(double), e->data.pline.npts, f);
                fwrite(e->data.pline.y, sizeof(double), e->data.pline.npts, f);
                break;
            }
            case ENTITY_SPLINE: {
                fwrite(&e->data.spline.n_ctrlp, sizeof(int), 1, f);
                fwrite(&e->data.spline.degree, sizeof(int), 1, f);
                fwrite(e->data.spline.x, sizeof(double), e->data.spline.n_ctrlp, f);
                fwrite(e->data.spline.y, sizeof(double), e->data.spline.n_ctrlp, f);
                break;
            }
        }
    }

    fclose(f);
    strncpy(current_filename, fname, sizeof(current_filename)-1);
    current_filename[sizeof(current_filename)-1] = '\0';
    set_window_title(current_filename);
}

// Load entities from binary
void ps_load_entities_(char *filename, int filename_len) {
    char fname[256];
    int len = (filename_len < 255) ? filename_len : 255;
    strncpy(fname, filename, len);
    fname[len] = '\0';

    FILE *f = fopen(fname, "rb");
    if (!f) { perror("fopen load"); return; }

    free_entities();

    while (!feof(f)) {
        int type;
        if (fread(&type, sizeof(int), 1, f) != 1) break;

        if (type == ENTITY_LINE) {
            LineEntity line;
            if (fread(&line, sizeof(LineEntity), 1, f) == 1)
                add_line(line.x1, line.y1, line.x2, line.y2);

        } else if (type == ENTITY_ARC) {
            ArcEntity arc;
            if (fread(&arc, sizeof(ArcEntity), 1, f) == 1)
                add_arc(arc.cx, arc.cy, arc.r, arc.startAng, arc.sweepAng);

        } else if (type == ENTITY_RECT) {
            RectEntity rect;
            if (fread(&rect, sizeof(RectEntity), 1, f) == 1)
                add_rect(rect.x1, rect.y1, rect.x2, rect.y2);

        } else if (type == ENTITY_POLYLINE) {
            int n;
            if (fread(&n, sizeof(int), 1, f) != 1) break;
            if (n > 1) {
                double *x = malloc(n * sizeof(double));
                double *y = malloc(n * sizeof(double));
                if (fread(x, sizeof(double), n, f) != (size_t)n) { free(x); free(y); break; }
                if (fread(y, sizeof(double), n, f) != (size_t)n) { free(x); free(y); break; }
                add_polyline(n, x, y);
                free(x); free(y);
            }

        } else if (type == ENTITY_SPLINE) {
            int n_ctrlp, degree;
            if (fread(&n_ctrlp, sizeof(int), 1, f) != 1) break;
            if (fread(&degree, sizeof(int), 1, f) != 1) break;
            if (n_ctrlp > 0) {
                double *x = malloc(n_ctrlp * sizeof(double));
                double *y = malloc(n_ctrlp * sizeof(double));
                if (fread(x, sizeof(double), n_ctrlp, f) != (size_t)n_ctrlp) { free(x); free(y); break; }
                if (fread(y, sizeof(double), n_ctrlp, f) != (size_t)n_ctrlp) { free(x); free(y); break; }
                add_spline(x, y, n_ctrlp, degree);
                free(x); free(y);
            }
        }
    }

    fclose(f);
    redraw(app.drawArea,NULL,NULL);
    strncpy(current_filename, fname, sizeof(current_filename)-1);
    current_filename[sizeof(current_filename)-1] = '\0';
    set_window_title(current_filename);
}

static void file_ok_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    XmFileSelectionBoxCallbackStruct *cbs =
        (XmFileSelectionBoxCallbackStruct*)call_data;
    char *filename = NULL;

    if (!XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &filename))
        return;

    strncpy(current_filename, filename, sizeof(current_filename)-1);
    current_filename[sizeof(current_filename)-1] = '\0';

    if ((intptr_t)client_data == 1) {
        // SAVE
        int len = strlen(filename);
        ps_save_entities_(filename, len);
    } else {
        // LOAD
        int len = strlen(filename);
        ps_load_entities_(filename, len);
    }
    XtFree(filename);
    XtUnmanageChild(w);  // close dialog
}

void popup_file_dialog(int save) {
    Widget dialog = XmCreateFileSelectionDialog(app.top, "fileDialog", NULL, 0);
    XtAddCallback(dialog, XmNokCallback, file_ok_cb, (XtPointer)(intptr_t)save);
    XtAddCallback(dialog, XmNcancelCallback, (XtCallbackProc)XtUnmanageChild, NULL);
    XtManageChild(dialog);
}

void popup_file_dialog_(int *save) {
    popup_file_dialog(*save);   // call the actual implementation
}

void ps_save_current_() {
    if (strcmp(current_filename, "Untitled") == 0) {
        // If we don't have a real file name yet → open Save As dialog
        popup_file_dialog(1);
    set_window_title(current_filename);
    } else {
        int len = strlen(current_filename);
        ps_save_entities_(current_filename, len);
    }
}

void ps_new_drawing_() {
    free_entities();
    strcpy(current_filename, "Untitled");
    redraw(app.drawArea, NULL, NULL);
    set_window_title(current_filename);
}

// Helper: compute circle through 3 points
static int circle_from_3pts(double x1, double y1,
                            double x2, double y2,
                            double x3, double y3,
                            double *cx, double *cy, double *r,
                            double *ang1, double *ang2, double *angm)
{
    double a = x1*(y2-y3) - y1*(x2-x3) + x2*y3 - x3*y2;
    if (fabs(a) < 1e-12) return 0; // points are collinear

    double b = -( (x1*x1 + y1*y1)*(y3-y2) +
                 (x2*x2 + y2*y2)*(y1-y3) +
                 (x3*x3 + y3*y3)*(y2-y1) ) / (2*a);

    double c = -( (x1*x1 + y1*y1)*(x2-x3) +
                 (x2*x2 + y2*y2)*(x3-x1) +
                 (x3*x3 + y3*y3)*(x1-x2) ) / (2*a);

    *cx = b;
    *cy = c;

    *r = hypot(x1 - *cx, y1 - *cy);

    *ang1 = atan2(y1 - *cy, x1 - *cx);
    *angm = atan2(y2 - *cy, x2 - *cx);
    *ang2 = atan2(y3 - *cy, x3 - *cx);

    return 1;
}

void ps_draw_arc_(double *x1, double *y1,
                  double *x2, double *y2,
                  double *x3, double *y3)
{
    double cx, cy, r, a1, a2, am;
    if (!circle_from_3pts(*x1, *y1, *x2, *y2, *x3, *y3,
                          &cx, &cy, &r, &a1, &a2, &am))
        return;

    // normalize into [0, 2π)
    if (a1 < 0) a1 += 2*M_PI;
    if (a2 < 0) a2 += 2*M_PI;
    if (am < 0) am += 2*M_PI;

    double sweep = a2 - a1;
    if (sweep <= -M_PI*2) sweep += 2*M_PI;
    if (sweep >=  M_PI*2) sweep -= 2*M_PI;

    // check if middle angle lies between a1 and a1+sweep (CCW case)
    double rel = am - a1;
    if (rel < 0) rel += 2*M_PI;

    double span = sweep;
    if (span < 0) span += 2*M_PI;

    int passes_mid = (rel > 0 && rel < span);

    if (!passes_mid) {
        // reverse direction
        if (sweep > 0)
            sweep = sweep - 2*M_PI; // go CW instead
        else
            sweep = sweep + 2*M_PI; // go CCW instead
    }

    // Store entity with (startAng, sweepAng)
    add_arc(cx, cy, r, a1, sweep);

    redraw(app.drawArea, NULL, NULL);
}

void ps_set_entity_mode_(char *mode, int len)
{
    char buf[32];
    int n = (len < 31) ? len : 31;
    strncpy(buf, mode, n);
    buf[n] = '\0';

    // 去掉尾端空白 (Fortran style)
    for (int i=n-1; i>=0 && buf[i]==' '; i--)
        buf[i] = '\0';

    if (strcasecmp(buf, "LINE") == 0) {
        current_entity_mode = ENTITY_LINE;
    }
    else if (strcasecmp(buf, "ARC") == 0) {
        current_entity_mode = ENTITY_ARC;
    }
    else if (strcasecmp(buf, "SPLINE") == 0) {
        current_entity_mode = ENTITY_SPLINE;
    }
    else if (strcasecmp(buf, "RECT") == 0) {
        current_entity_mode = ENTITY_RECT;
    }
    else if (strcasecmp(buf, "POLYLINE") == 0) {
        current_entity_mode = ENTITY_POLYLINE;
    }
    else {
        current_entity_mode = ENTITY_NONE;
    }

    printf("Entity mode set to %s\n", buf);
}

void ps_draw_polyline_(int *npts, double *x, double *y) {
    if (*npts < 2) return;
    add_polyline(*npts, x, y);
    redraw(app.drawArea, NULL, NULL);
}



