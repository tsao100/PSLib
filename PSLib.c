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
#include <stdio.h>
#include <stdlib.h>
#include "tinyspline.h"
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <X11/cursorfont.h>
#include <stdint.h>

static tsBSpline spline;
static int spline_ready = 0;

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

typedef struct {
    double offsetX, offsetY;
    double scale;
    int lastX, lastY;
    int isPanning;
    Widget status_label;
} ViewState;

typedef struct {
    Widget top, form, menuBar, toolBar, drawArea, statusBar, cmdInput;
} AppWidgets;

typedef enum {
    ENTITY_LINE,
    ENTITY_ARC,
    ENTITY_POLYLINE
    // Extend later...
} EntityType;

typedef struct {
    double x1, y1, x2, y2;
} LineEntity;

typedef struct {
    double cx, cy;    // center
    double r;         // radius
    double startAng;  // radians
    double endAng;    // radians
} ArcEntity;

typedef struct {
    int npts;
    double *x;
    double *y;
} PolylineEntity;

typedef struct Entity {
    EntityType type;
    union {
        LineEntity line;
        ArcEntity arc;
        PolylineEntity pline;
    } data;
    struct Entity *next;
} Entity;

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
    e->data.arc.endAng = a2;
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


static AppWidgets app;
static ViewState view = {400.0, 300.0, 1.0, 0, 0, 0, NULL};
static char current_filename[256] = "Untitled";
static int protected_len = 0; // cumulative protected text length

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

typedef void (*ActionFunc)();

typedef struct {
    char name[32];
    ActionFunc func;
} Action;

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
/* Here put the entities redraw function.
    double wx = 50, wy = 50;
    int sx, sy;
    world_to_screen(wx, wy, &sx, &sy);
    int sz = (int)(30 * view.scale);
    XDrawRectangle(dpy, win, gc, sx, sy, sz, sz);
*/
    // Redraw all stored entities
    Entity *e = entity_list;
    while (e) {
        if (e->type == ENTITY_LINE) {
            int sx1, sy1, sx2, sy2;
            world_to_screen(e->data.line.x1, e->data.line.y1, &sx1, &sy1);
            world_to_screen(e->data.line.x2, e->data.line.y2, &sx2, &sy2);
            XDrawLine(dpy, win, gc, sx1, sy1, sx2, sy2);
        }
        else if (e->type == ENTITY_ARC) {
            int scx, scy;
            world_to_screen(e->data.arc.cx, e->data.arc.cy, &scx, &scy);
            int r = (int)(e->data.arc.r * view.scale);
            int ang1 = (int)(e->data.arc.startAng * 64 * 180 / M_PI);
            int ang2 = (int)((e->data.arc.endAng - e->data.arc.startAng) * 64 * 180 / M_PI);
            XDrawArc(dpy, win, gc, scx - r, scy - r, 2*r, 2*r, ang1, ang2);
        }
        else if (e->type == ENTITY_POLYLINE) {
            XPoint *pts = (XPoint*)malloc(e->data.pline.npts * sizeof(XPoint));
            for (int i=0; i<e->data.pline.npts; i++) {
                int sx, sy;
                world_to_screen(e->data.pline.x[i], e->data.pline.y[i], &sx, &sy);
                pts[i].x = (short)sx;
                pts[i].y = (short)sy;
            }
            XDrawLines(dpy, win, gc, pts, e->data.pline.npts, CoordModeOrigin);
            free(pts);
        }
        e = e->next;
    }

    if (font) XFreeFont(dpy, font);
    XFreeGC(dpy, gc);
}

/* --- Event Handlers --- */
void mouse_event(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    if (event->type == ButtonPress) {
        if (event->xbutton.button == Button2) {
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
        if (event->xbutton.button == Button2) view.isPanning = 0;
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

/*
static void menu_item_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    handle_command((const char *)client_data);
}

static void toolbar_button_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    handle_command((const char *)client_data);
}*/

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
    XtVaSetValues(toolbar, XmNorientation, XmHORIZONTAL, XmNpacking, XmPACK_TIGHT, XmNspacing, 4, NULL);

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
                Pixmap pix = load_xpm(XtDisplay(parent), DefaultRootWindow(XtDisplay(parent)), icon_path);

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

/* void ps_draw_line_(double *x1, double *y1, double *x2, double *y2) {
    Display *dpy = XtDisplay(app.drawArea);
    Window win = XtWindow(app.drawArea);
    int sx1, sy1, sx2, sy2;
    // Convert world coordinates to screen coordinates
    world_to_screen(*x1, *y1, &sx1, &sy1);
    world_to_screen(*x2, *y2, &sx2, &sy2);  

    GC gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
    XDrawLine(dpy, win, gc, sx1, sy1, sx2, sy2);
    XFreeGC(dpy, gc);
}
 */

void ps_draw_line_(double *x1, double *y1, double *x2, double *y2) {
    add_line(*x1, *y1, *x2, *y2);
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

void ps_getpoint_(char *prompt, double *x, double *y, int *has_start, int prompt_len)
{
    Display *dpy = XtDisplay(app.drawArea);
    Window win = XtWindow(app.drawArea);
    GC gc = XCreateGC(dpy, win, 0, NULL);

    // Null-terminate Fortran string
    char local_prompt[256];
    int len = (prompt_len < 255) ? prompt_len : 255;
    strncpy(local_prompt, prompt, len);
    local_prompt[len] = '\0';

    // --- Append prompt to cmdInput ---
    if (app.cmdInput) {
        protected_len =0;
        // Directly set the new prompt text (replace, not append)
        XmTextFieldSetString(app.cmdInput, local_prompt);

        // Force widget to process and display the change immediately
        XmUpdateDisplay(app.cmdInput);
        // Move cursor to end of the new prompt
        XmTextFieldSetInsertionPosition(app.cmdInput, strlen(local_prompt));

        // Ensure input focus is on the command input
        XmProcessTraversal(app.cmdInput, XmTRAVERSE_CURRENT);

        // Protect exactly this prompt text
        protected_len = strlen(local_prompt);
    }

    // --- Setup base point if provided ---
    double x1 = 0, y1 = 0;
    bool has_base = (*has_start != 0);
    if (has_base) { x1 = *x; y1 = *y; }

    XEvent event;
    bool done = false;

    while (!done) {
        XtAppNextEvent(XtWidgetToApplicationContext(app.drawArea), &event);

        // --- Auto-focus on cmdInput if any key pressed ---
        if (event.type == KeyPress && app.cmdInput) {
            XmProcessTraversal(app.cmdInput, XmTRAVERSE_CURRENT);
        }

        // --- Mouse input ---
        if (event.type == ButtonPress) {
            if (event.xbutton.button == Button1) {
                int mx = event.xbutton.x;
                int my = event.xbutton.y;
                screen_to_world(mx, my, x, y);
                done = true;
            }
            else if (event.xbutton.button == Button3) {  // right click cancels
                *has_start = 0;
                done = true;
            }
            else if (event.xbutton.button == Button4 || event.xbutton.button == Button5) {
                int sx = event.xbutton.x, sy = event.xbutton.y;
                double wx, wy;
                screen_to_world(sx, sy, &wx, &wy);
                double zoom = (event.xbutton.button == Button4) ? 1.1 : 0.9;
                view.scale *= zoom;
                view.offsetX = sx - wx * view.scale;
                view.offsetY = sy + wy * view.scale;
                redraw(app.drawArea, NULL, NULL);
            }
        }

        // --- Rubberband line preview ---
        else if (event.type == MotionNotify && has_base) {
            XClearWindow(dpy, win);
            redraw(app.drawArea, NULL, NULL);

            int mx = event.xmotion.x;
            int my = event.xmotion.y;
            world_to_screen(x1, y1, &base_sx, &base_sy);
            XDrawLine(dpy, win, gc, base_sx, base_sy, mx, my);
        }

        // --- Keyboard coordinates input ---
        else if (event.type == KeyPress && app.cmdInput) {
            KeySym keysym;
            char buf[256];
            int n = XLookupString(&event.xkey, buf, sizeof(buf) - 1, &keysym, NULL);
            buf[n] = '\0';

            if (keysym == XK_Return || keysym == XK_KP_Enter) {
                char *text = XmTextFieldGetString(app.cmdInput);

                if ((int)strlen(text) > protected_len) {
                    const char *coords = text + protected_len; // new input only
                    double tx, ty;
                    if (sscanf(coords, "%lf%*[, ]%lf", &tx, &ty) == 2) {
                        *x = tx;
                        *y = ty;
                        done = true;
                    }
                }
                XmTextFieldSetInsertionPosition(app.cmdInput, strlen(text));
                XtFree(text);
            }
        }

        XtDispatchEvent(&event);
    }

    if (app.cmdInput)
        XmTextFieldSetInsertionPosition(app.cmdInput, protected_len); // ensure cursor after prompt
}

void ps_save_entities_(char *filename, int filename_len) {
    char fname[256];
    int len = (filename_len < 255) ? filename_len : 255;
    strncpy(fname, filename, len);
    fname[len] = '\0';

    FILE *f = fopen(fname, "w");
    if (!f) {
        perror("fopen save");
        return;
    }

    for (Entity *e = entity_list; e; e = e->next) {
        if (e->type == ENTITY_LINE) {
            fprintf(f, "LINE %.15g %.15g %.15g %.15g\n",
                    e->data.line.x1, e->data.line.y1,
                    e->data.line.x2, e->data.line.y2);
        } else if (e->type == ENTITY_ARC) {
            fprintf(f, "ARC %.15g %.15g %.15g %.15g %.15g\n",
                    e->data.arc.cx, e->data.arc.cy,
                    e->data.arc.r, e->data.arc.startAng, e->data.arc.endAng);
        } else if (e->type == ENTITY_POLYLINE) {
            fprintf(f, "POLYLINE %d", e->data.pline.npts);
            for (int i=0; i<e->data.pline.npts; i++) {
                fprintf(f, " %.15g %.15g",
                        e->data.pline.x[i], e->data.pline.y[i]);
            }
            fprintf(f, "\n");
        }
    }

    fclose(f);
    strncpy(current_filename, fname, sizeof(current_filename)-1);
    current_filename[sizeof(current_filename)-1] = '\0';
    set_window_title(current_filename);
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



void ps_load_entities_(char *filename, int filename_len) {
    char fname[256];
    int len = (filename_len < 255) ? filename_len : 255;
    strncpy(fname, filename, len);
    fname[len] = '\0';

    FILE *f = fopen(fname, "r");
    if (!f) {
        perror("fopen load");
        return;
    }

    free_entities();

    char type[32];
    while (fscanf(f, "%31s", type) == 1) {
        if (strcmp(type, "LINE") == 0) {
            double x1,y1,x2,y2;
            if (fscanf(f,"%lf %lf %lf %lf",&x1,&y1,&x2,&y2)==4)
                add_line(x1,y1,x2,y2);
        } else if (strcmp(type, "ARC") == 0) {
            double cx,cy,r,a1,a2;
            if (fscanf(f,"%lf %lf %lf %lf %lf",&cx,&cy,&r,&a1,&a2)==5)
                add_arc(cx,cy,r,a1,a2);
        } else if (strcmp(type, "POLYLINE") == 0) {
            int n;
            if (fscanf(f,"%d",&n)==1 && n>1) {
                double *x=(double*)malloc(n*sizeof(double));
                double *y=(double*)malloc(n*sizeof(double));
                for(int i=0;i<n;i++)
                    fscanf(f,"%lf %lf",&x[i],&y[i]);
                add_polyline(n,x,y);
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


