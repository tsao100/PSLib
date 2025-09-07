#ifndef PSLIB_H
#define PSLIB_H

#ifdef __cplusplus
extern "C" {
#endif

// Constants
#define OSNAP_TOL_PIXELS 10
#define MAX_ACTIONS 10

typedef struct {
    Pixmap pixmap;
    int width, height;
    int icon_w, icon_h;
} IconAtlas;

// Entity Types
typedef enum {
    ENTITY_NONE,
    ENTITY_LINE,
    ENTITY_ARC,
    ENTITY_POLYLINE,
    ENTITY_SPLINE,
    ENTITY_RECT
} EntityType;

// Entity Structures
typedef struct {
    double x1, y1, x2, y2;
} LineEntity;

typedef struct {
    double cx, cy;    // center
    double r;         // radius
    double startAng;  // radians
    double sweepAng;  // sweep Angle radians
} ArcEntity;

typedef struct {
    double x1, y1, x2, y2;   // diagonal corners
} RectEntity;

typedef struct {
    int npts;
    double *x;
    double *y;
} PolylineEntity;

typedef struct {
    int n_ctrlp;       // number of control points
    double *x;         // x coordinates
    double *y;         // y coordinates
    int degree;        // spline degree
} SplineEntity;

typedef struct Entity {
    EntityType type;
    union {
        LineEntity line;
        ArcEntity arc;
        PolylineEntity pline;
        SplineEntity spline;
        RectEntity rect;
    } data;
    struct Entity *next;
} Entity;

// View State Structure
typedef struct {
    double offsetX, offsetY;
    double scale;
    int lastX, lastY;
    int isPanning;
    Widget status_label;
} ViewState;

// Application Widgets Structure
typedef struct {
    Widget top, form, menuBar, toolBar, drawArea, statusBar, cmdInput;
} AppWidgets;

// Action Function Type
typedef void (*ActionFunc)();

typedef struct {
    char name[32];
    ActionFunc func;
} Action;

// Spline Functions (Fortran-callable with trailing underscore)
void ps_spline_copy_(tsBSpline *dest);
void ps_spline_get_ctrlp_(double *ctrlp_out);
void ps_spline_set_knots_(double *knots_in, int *n_knots, int *status_out);
void ps_spline_insert_knot_(double *u, int *n, int *k_out);
void ps_spline_to_beziers_(tsBSpline *out);
void ps_spline_interpolate_cubic_natural_(double *points, int *n, int *dim, int *status_out);
void ps_spline_interpolate_catmull_rom_(double *points, int *n, int *dim,
                                        double *alpha, double *epsilon,
                                        int *status_out);
void ps_spline_eval_(double *u, double *out);
void ps_spline_split_(double *u, tsBSpline *out, int *k_out);
void ps_spline_degree_(int *deg_out);
void ps_spline_domain_(double *min, double *max);
void ps_spline_is_closed_(double *eps, int *closed_out);
void ps_spline_chord_lengths_(double *knots, int *num, double *lengths);
void ps_spline_equidistant_knots_(double *knots, int *num_knot_seq, int *num_samples);
void ps_spline_new_(int *degree, int *dim, int *n_ctrlp, int *type, int *status);
void ps_spline_set_ctrlp_(double *ctrlp, int *status);
void ps_spline_sample_(int *sample_count, double *samples_out, int *status_out);
void ps_spline_free_();

// Vector Math Functions (Fortran-callable)
void ps_vec2_init_(double *out, double *x, double *y);
void ps_vec3_init_(double *out, double *x, double *y, double *z);
void ps_vec4_init_(double *out, double *x, double *y, double *z, double *w);
void ps_vec_add_(double *x, double *y, int *dim, double *out);
void ps_vec_sub_(double *x, double *y, int *dim, double *out);
double ps_vec_dot_(double *x, double *y, int *dim);
double ps_vec_mag_(double *x, int *dim);
void ps_vec_norm_(double *x, int *dim, double *out);
void ps_vec_cross_(double *x, double *y, double *out);

// Knot Functions
int ps_knots_equal_(double *x, double *y);

// Entity Management Functions
Entity* add_line(double x1, double y1, double x2, double y2);
Entity* add_arc(double cx, double cy, double r, double a1, double a2);
Entity* add_rect(double x1, double y1, double x2, double y2);
Entity* add_polyline(int npts, double *x, double *y);
Entity* add_spline(double *ctrlx, double *ctrly, int n_ctrlp, int degree);
void ps_set_entity_mode_(char *mode, int len);
void delete_entity(Entity *target);

// Coordinate Transformation Functions
void screen_to_world(int x, int y, double *wx, double *wy);
void world_to_screen(double wx, double wy, int *sx, int *sy);

// Action Registration Functions
void register_action_(char *name, ActionFunc func, int name_len);
void trigger_action(const char *name);

// Drawing Functions
void draw_arrow(Display *dpy, Drawable win, GC gc, int x1, int y1, int x2, int y2);
void redraw(Widget w, XtPointer client_data, XtPointer call_data);

// Entity Selection
void ps_entsel_(double *wx, double *wy, int *found);

// Event Handlers
void mouse_event(Widget w, XtPointer client_data, XEvent *event, Boolean *cont);
void key_event(Widget w, XtPointer client_data, XEvent *event, Boolean *cont);

// Callback Functions
void menu_item_cb(Widget w, XtPointer client_data, XtPointer call_data);
void toolbar_button_cb(Widget w, XtPointer client_data, XtPointer call_data);
static void command_input_cb(Widget w, XtPointer client_data, XtPointer call_data);
void protect_prompt_cb(Widget w, XtPointer client_data, XtPointer call_data);

// Helper: compute circle through 3 points
static int circle_from_3pts(double x1, double y1,
                            double x2, double y2,
                            double x3, double y3,
                            double *cx, double *cy, double *r,
                            double *ang1, double *ang2, double *angm);

// GUI Functions (Fortran-callable)
void start_gui_();
void ps_wait_click_(int *x, int *y);
void ps_draw_line_(double *x1, double *y1, double *x2, double *y2);
void ps_draw_arc_(double *x1, double *y1, double *x2, double *y2, double *x3, double *y3);
void ps_draw_rect_(double *x1, double *y1, double *x2, double *y2);
void ps_draw_polyline_(int *npts, double *x, double *y);
void ps_draw_spline_(double *ctrlp, int *n_ctrlp, int *degree, int *dim, int *type, int *status);
void ps_getpoint_(char *prompt, double *x, double *y, int *has_start, int prompt_len);

// File I/O Functions (Fortran-callable)
void ps_save_entities_(char *filename, int filename_len);
void ps_load_entities_(char *filename, int filename_len);
void popup_file_dialog_(int *save);
void ps_save_current_();
void ps_new_drawing_();

// Utility Functions
void popup_file_dialog(int save);

// Internal Helper Functions (may not need to be exposed)
static char *trim(char *str);
static char *xstrdup(const char *s);
static void update_status(const char *msg);
static void handle_command(const char *cmd);
static Pixmap load_xpm(Display *dpy, Window win, const char *filename);
static Widget create_menu_from_file(Widget parent, const char *filename);
static Widget create_toolbar_from_file(Widget parent, const char *filename);
static void set_window_title(const char *fname);
static void update_prompt(char *prompt, int prompt_len);
static void handle_zoom(XButtonEvent *bev);
static void handle_osnap_toggle(KeySym key);
static void draw_rubberband(Display *dpy, Window win, GC gc,
                            double *px, double *py, int npoints, int mx, int my);
static double *sample_spline_entity(const Entity *e, int *out_count);
static int snap_to_entity(double wx, double wy, double *sx, double *sy);
static double dist2(double x1, double y1, double x2, double y2);
static double point_seg_dist(double px, double py,
                            double x1, double y1, double x2, double y2);
static void free_entities();
static void file_ok_cb(Widget w, XtPointer client_data, XtPointer call_data);

#ifdef __cplusplus
}
#endif

#endif /* PSLIB_H */