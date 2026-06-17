#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/XF86keysym.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#include <signal.h>
#include <limits.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define MOD          Mod4Mask
#define MAXBINDS     64
#define MAXMODEBINDS 24
#define MAXCMDS      16
#define MAXRULES     32
#define MAXMONS       8
#define MAXWS         9
#define MAXAUTOSTART 8
#define MAXDOCKS     16
#define CMDLINE_MAX 256

typedef struct Node Node;
struct Node {
    int leaf, floating, horiz;
    int fullscreen;
    int real_fullscreen;
    int ignore_unmap;
    float ratio;
    Node *a, *b, *par;
    Window win;
    int x, y, w, h;
    Pixmap thumb;          /* WM-owned snapshot of window contents, 0 if none */
    int thumb_w, thumb_h;
};

typedef struct {
    unsigned int mod;
    KeyCode code;
    char action[128];
} ModBind;

typedef struct {
    KeySym sym;
    char action[128];
} ModeBind;

typedef struct {
    char name[64];
    char action[128];
} CmdBind;

typedef struct {
    char class_name[64];
    char instance_name[64];
    char title[128];
    int set_floating;
    int floating;
    int workspace;
    int follow_class;
} Rule;

typedef struct {
    int x, y, w, h;
    int wx, wy, ww, wh;
    int curws;
    Node *tree[MAXWS], *focused[MAXWS];
    Window barwin;
    Pixmap barpix;
} Mon;

typedef enum {
    BAR_TOP = 0,
    BAR_BOTTOM
} BarPosition;

typedef enum {
    MODE_INSERT = 0,
    MODE_NORMAL,
    MODE_COMMAND
} InputMode;

static int gap = 8, bw = 2, barh = 24, barenabled = 1, externalbarh = 0;
static int barpadx = 10, baritemgap = 6, bartextpad = 8, barwsminw = 20;
static unsigned long cfocus = 0x5588ff, cnorm = 0x333333;
static unsigned long barbg = 0x111111, barfg = 0xeeeeee;
static unsigned long baraccentbg = 0x5588ff, baraccentfg = 0x111111;
static unsigned long barmutedfg = 0xaaaaaa;
static char term[128] = "st";
static char barfontname[128] = "9x15";
static char barleftcfg[128] = "workspaces";
static char barcentercfg[128] = "command";
static char barrightcfg[128] = "clock";
static int screen_off_minutes;
static BarPosition barpos = BAR_TOP;
static Display *dpy;
static Window root;
static int sw, sh;
static Mon mons[MAXMONS];
static int nmons = 1, curmon;
static int randr_active = 0;
static int randr_event_base = -1;
/* index of the monitor whose bar is currently being drawn (per-monitor curws) */
static int barmon = 0;
/* workspace overview (Super+Z) state */
static int overview_active = 0;
static int overview_sel = 0;
static int overview_mon = 0;
static Window overview_win = 0;
static Pixmap overview_pix = 0;
/* XComposite + XRender: present and redirect active -> live thumbnails */
static int composite_ok = 0;
/* drag-to-move inside the overview */
static int ov_drag = 0;            /* a press is in progress */
static Node *ov_drag_node = NULL;  /* leaf being dragged, NULL if empty-cell press */
static int ov_drag_ws = -1;        /* workspace the press started on */
static int ov_press_rx = 0, ov_press_ry = 0; /* press point (root coords) */
static int ov_ptr_x = 0, ov_ptr_y = 0;       /* current pointer (monitor-local) */
static int ov_moved = 0;           /* moved past click threshold */
static Window wmcheckwin;
static ModBind modbinds[MAXBINDS];
static int nmodbinds;
static ModeBind normalbinds[MAXMODEBINDS];
static int nnormalbinds;
static CmdBind cmdbinds[MAXCMDS];
static int ncmdbinds;
static Rule rules[MAXRULES];
static int nrules;
static Window dockwins[MAXDOCKS];
static int ndockwins;
static char autostart_cmds[MAXAUTOSTART][256];
static int nautostart;
static int modbind_limit = MAXBINDS;
static int normalbind_limit = MAXMODEBINDS;
static int cmd_limit = MAXCMDS;
static int autostart_limit = MAXAUTOSTART;
static GC bargc;
static XFontStruct *barfont;
static Cursor normalcursor;
static Cursor movecursor;
static Cursor resizecursor;
static InputMode mode = MODE_INSERT;
static int keyboard_grabbed;
static int running = 1;
static unsigned int numlockmask;
static Atom atom_net_active_window;
static Atom atom_net_client_list;
static Atom atom_net_current_desktop;
static Atom atom_net_number_of_desktops;
static Atom atom_net_supported;
static Atom atom_net_supporting_wm_check;
static Atom atom_utf8_string;
static Atom atom_net_wm_window_type;
static Atom atom_net_wm_window_type_dock;
static Atom atom_net_wm_window_type_dialog;
static Atom atom_net_wm_window_type_utility;
static Atom atom_net_wm_window_type_toolbar;
static Atom atom_net_wm_window_type_splash;
static Atom atom_net_wm_desktop;
static Atom atom_net_wm_state;
static Atom atom_net_wm_state_fullscreen;
static Atom atom_net_wm_name;
static Atom atom_net_wm_strut;
static Atom atom_net_wm_strut_partial;
static Atom atom_wm_protocols;
static Atom atom_wm_delete_window;
static Atom atom_net_wm_pid;
static char cached_title[256];
static char cmdline[CMDLINE_MAX];
static int cmdline_len;
static char timestr[32];
static char batterystr[64];
static int last_clock_second = -1;
static int drag_mode;
static int drag_mon;
static int drag_ox, drag_oy;
static int drag_wx, drag_wy;
static int drag_ww, drag_wh;
static Node *drag_node;

static int xerr(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }
static void drawbars(void);
static void update_clock(void);
static void update_battery(void);
static void setupbars(void);
static void enter_mode(InputMode newmode);
static Node *mon_focused(int mi);
static void showtree(Node *n);
static void hidetree(Node *n);
static void close_overview(void);
static void capture_thumb(Node *n);
static void capture_tree(Node *n);
static void free_thumb(Node *n);
static void init_composite(void);
static void spawn(const char *cmd);
static void initatoms(void);
static void apply_rules(Node *n, int *targetws, int *out_follow);
static void update_client_list(void);
static void update_current_desktop(void);
static void update_number_of_desktops(void);
static void update_active_window(void);
static void set_window_desktop(Window win, int ws);
static void update_supported_atoms(void);
static void setup_wm_check(void);
static void reloadwm(void);
static void collect_leaves(Node *n, Node **list, int *count, int maxcount);
static void free_tree(Node *n);
static int querygeom(Mon *out, int max);
static int updategeom(void);
static void retile(void);
static void setfocus(int mi, Node *n, int warp);
static void attach_to_ws(int mi, int ws, Node *leaf);
static void wait_for_x_event_or_clock_tick(void);
static void apply_screen_off_config(void);
static void close_window(Window win);
static void destroybars(void);
static void compute_dock_struts(int *out_top, int *out_bottom);
static void sync_dock_stack(void);
static Node *find_real_fullscreen_leaf(Node *n);
static unsigned long hcol(const char *s) {
    return strtoul(*s == '#' ? s + 1 : s, NULL, 16);
}

static char *ltrim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void rtrim(char *s) {
    size_t len = strlen(s);
    while (len && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                   s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = 0;
    }
}

static void copystr(char *dst, size_t dstsz, const char *src) {
    size_t n;
    if (!dstsz) return;
    if (!src) src = "";
    n = strlen(src);
    if (n >= dstsz) n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static const char *skip_command_prefix(const char *s) {
    return (s && s[0] == ':') ? s + 1 : s;
}

static KeySym parse_keysym_name(const char *name) {
    char lowered[64];
    size_t len;
    KeySym sym;

    if (!name || !*name) return NoSymbol;
    if (strcmp(name, " ") == 0) return XK_space;

    len = strlen(name);
    if (len >= sizeof lowered) len = sizeof lowered - 1;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        lowered[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    lowered[len] = 0;

    if (!strcmp(lowered, "space")) return XK_space;
    if (!strcmp(lowered, "return") || !strcmp(lowered, "enter")) return XK_Return;
    if (!strcmp(lowered, "escape") || !strcmp(lowered, "esc")) return XK_Escape;
    if (!strcmp(lowered, "page_up") || !strcmp(lowered, "pageup") || !strcmp(lowered, "prior"))
        return XK_Page_Up;
    if (!strcmp(lowered, "page_down") || !strcmp(lowered, "pagedown") || !strcmp(lowered, "next"))
        return XK_Page_Down;
    if (!strcmp(lowered, "left")) return XK_Left;
    if (!strcmp(lowered, "right")) return XK_Right;
    if (!strcmp(lowered, "up")) return XK_Up;
    if (!strcmp(lowered, "down")) return XK_Down;
    if (!strcmp(lowered, "tab")) return XK_Tab;
    if (!strcmp(lowered, "backspace")) return XK_BackSpace;
    if (!strcmp(lowered, "xf86audioraisevolume")) return XF86XK_AudioRaiseVolume;
    if (!strcmp(lowered, "xf86audiolowervolume")) return XF86XK_AudioLowerVolume;
    if (!strcmp(lowered, "xf86audiomute")) return XF86XK_AudioMute;
    if (!strcmp(lowered, "xf86audioplay")) return XF86XK_AudioPlay;
    if (!strcmp(lowered, "xf86audiopause")) return XF86XK_AudioPause;
    if (!strcmp(lowered, "xf86audionext")) return XF86XK_AudioNext;
    if (!strcmp(lowered, "xf86audioprev")) return XF86XK_AudioPrev;
    if (!strcmp(lowered, "xf86audiostop")) return XF86XK_AudioStop;
    if (!strcmp(lowered, "xf86monbrightnessup")) return XF86XK_MonBrightnessUp;
    if (!strcmp(lowered, "xf86monbrightnessdown")) return XF86XK_MonBrightnessDown;

    sym = XStringToKeysym(name);
    if (sym == NoSymbol) sym = XStringToKeysym(lowered);
    if (sym == NoSymbol && strlen(name) == 1) sym = (KeySym)(unsigned char)name[0];
    return sym;
}

static int textw(const char *s) {
    int len, w;
    if (!s) return 0;
    len = (int)strlen(s);
    if (!barfont) return len * 8;
    w = XTextWidth(barfont, s, len);
    return w ? w : len * 8;
}

static void build_mode_text(char *dst, size_t dstsz) {
    const char *label = mode == MODE_INSERT ? "INSERT" :
                        mode == MODE_NORMAL ? "NORMAL" : "COMMAND";
    snprintf(dst, dstsz, "%s", label);
}

static int clampi(int value, int minv, int maxv) {
    if (value < minv) return minv;
    if (value > maxv) return maxv;
    return value;
}

static int parse_bool_value(const char *v, int fallback) {
    if (!v) return fallback;
    if (!strcasecmp(v, "1") || !strcasecmp(v, "true") ||
        !strcasecmp(v, "yes") || !strcasecmp(v, "on")) return 1;
    if (!strcasecmp(v, "0") || !strcasecmp(v, "false") ||
        !strcasecmp(v, "no") || !strcasecmp(v, "off")) return 0;
    return fallback;
}

static int get_window_strut(Window win, int *out_top, int *out_bottom) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    long *data = NULL;

    *out_top = 0;
    *out_bottom = 0;
    if (XGetWindowProperty(dpy, win, atom_net_wm_strut_partial, 0, 12, False,
            XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after,
            (unsigned char **)&data) == Success && data && nitems >= 4) {
        *out_top = (int)data[2];
        *out_bottom = (int)data[3];
        XFree(data);
        return 1;
    }
    if (data) { XFree(data); data = NULL; }
    if (XGetWindowProperty(dpy, win, atom_net_wm_strut, 0, 4, False,
            XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after,
            (unsigned char **)&data) == Success && data && nitems >= 4) {
        *out_top = (int)data[2];
        *out_bottom = (int)data[3];
        XFree(data);
        return 1;
    }
    if (data) XFree(data);
    return 0;
}

static void compute_dock_struts(int *out_top, int *out_bottom) {
    *out_top = 0;
    *out_bottom = 0;
    for (int i = 0; i < ndockwins; i++) {
        int t = 0, b = 0;
        if (get_window_strut(dockwins[i], &t, &b)) {
            if (t > *out_top) *out_top = t;
            if (b > *out_bottom) *out_bottom = b;
            continue;
        }
        XWindowAttributes wa;
        if (!XGetWindowAttributes(dpy, dockwins[i], &wa)) continue;
        if (wa.map_state == IsUnmapped) continue;
        if (wa.y + wa.height <= sh / 2) {
            if (wa.height > *out_top) *out_top = wa.height;
        } else {
            if (wa.height > *out_bottom) *out_bottom = wa.height;
        }
    }
    *out_top = clampi(*out_top, 0, 256);
    *out_bottom = clampi(*out_bottom, 0, 256);
}

static int get_window_title(Window win, char *dst, size_t dstsz) {
    XTextProperty prop;
    char **list = NULL;
    int count = 0;
    int ok;

    dst[0] = 0;
    if (XGetTextProperty(dpy, win, &prop, atom_net_wm_name) && prop.value && prop.nitems > 0) {
        if (prop.encoding == atom_utf8_string) {
            copystr(dst, dstsz, (char *)prop.value);
            XFree(prop.value);
            return dst[0] != 0;
        }
        ok = XmbTextPropertyToTextList(dpy, &prop, &list, &count);
        if (ok >= Success && count > 0 && list && list[0])
            copystr(dst, dstsz, list[0]);
        if (ok >= Success && list) XFreeStringList(list);
        XFree(prop.value);
        list = NULL;
        if (dst[0]) return 1;
    }

    if (XGetWMName(dpy, win, &prop) && prop.value && prop.nitems > 0) {
        ok = XmbTextPropertyToTextList(dpy, &prop, &list, &count);
        if (ok >= Success && count > 0 && list && list[0])
            copystr(dst, dstsz, list[0]);
        if (ok >= Success && list) XFreeStringList(list);
        XFree(prop.value);
        if (dst[0]) return 1;
    }
    return 0;
}

static int get_window_class(Window win, char *class_name, size_t class_sz,
    char *instance_name, size_t instance_sz)
{
    XClassHint hint;
    class_name[0] = 0;
    instance_name[0] = 0;
    if (!XGetClassHint(dpy, win, &hint)) return 0;
    if (hint.res_class) {
        copystr(class_name, class_sz, hint.res_class);
        XFree(hint.res_class);
    }
    if (hint.res_name) {
        copystr(instance_name, instance_sz, hint.res_name);
        XFree(hint.res_name);
    }
    return class_name[0] || instance_name[0];
}

static int window_has_atom(Window win, Atom prop, Atom value) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    Atom *atoms = NULL;
    int found = 0;
    if (XGetWindowProperty(dpy, win, prop, 0, 32, False, XA_ATOM,
            &actual_type, &actual_format, &nitems, &bytes_after,
            (unsigned char **)&atoms) != Success) return 0;
    if (actual_type == XA_ATOM && actual_format == 32 && atoms) {
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == value) {
                found = 1;
                break;
            }
        }
    }
    if (atoms) XFree(atoms);
    return found;
}

static void refresh_cached_title(void) {
    Node *n = mon_focused(curmon);
    cached_title[0] = 0;
    if (n && n->leaf) get_window_title(n->win, cached_title, sizeof cached_title);
}

static void build_title_text(char *dst, size_t dstsz) {
    copystr(dst, dstsz, cached_title);
}

static void build_clock_text(char *dst, size_t dstsz) {
    update_clock();
    snprintf(dst, dstsz, "%s", timestr[0] ? timestr : "--:--");
}

static void build_battery_text(char *dst, size_t dstsz) {
    update_battery();
    snprintf(dst, dstsz, "%s", batterystr);
}

static void update_clock(void) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if (tm_now.tm_sec == last_clock_second) return;
    last_clock_second = tm_now.tm_sec;
    strftime(timestr, sizeof timestr, "%H:%M", &tm_now);
}

static void update_battery(void) {
    static time_t last_battery_update = 0;
    time_t now = time(NULL);
    const char *bases[] = {
        "/sys/class/power_supply/BAT0",
        "/sys/class/power_supply/BAT1",
        "/sys/class/power_supply/battery",
        NULL
    };
    char path[256], status[32], cap[32];
    FILE *f;

    if (last_battery_update && now - last_battery_update < 15) return;
    last_battery_update = now;
    batterystr[0] = 0;

    for (int i = 0; bases[i]; i++) {
        snprintf(path, sizeof path, "%s/capacity", bases[i]);
        f = fopen(path, "r");
        if (!f) continue;
        if (!fgets(cap, sizeof cap, f)) {
            fclose(f);
            continue;
        }
        fclose(f);
        rtrim(cap);

        snprintf(path, sizeof path, "%s/status", bases[i]);
        f = fopen(path, "r");
        if (f && fgets(status, sizeof status, f)) {
            fclose(f);
            rtrim(status);
        } else {
            if (f) fclose(f);
            status[0] = 0;
        }

        if (!strcasecmp(status, "Charging")) snprintf(batterystr, sizeof batterystr, "BAT+ %s%%", cap);
        else if (!strcasecmp(status, "Full")) snprintf(batterystr, sizeof batterystr, "BAT= %s%%", cap);
        else if (status[0]) snprintf(batterystr, sizeof batterystr, "BAT %s%%", cap);
        else snprintf(batterystr, sizeof batterystr, "BAT %s%%", cap);
        return;
    }
}

static int monforpt(int x, int y) {
    for (int i = 0; i < nmons; i++) {
        if (x >= mons[i].x && x < mons[i].x + mons[i].w &&
            y >= mons[i].y && y < mons[i].y + mons[i].h) return i;
    }
    return 0;
}

static Node *mon_tree(int mi) {
    return mons[mi].tree[mons[mi].curws];
}

static Node *mon_focused(int mi) {
    return mons[mi].focused[mons[mi].curws];
}

static Node *mon_tree_at(int mi, int ws) {
    return mons[mi].tree[ws];
}

static Node *mon_focused_at(int mi, int ws) {
    return mons[mi].focused[ws];
}

static void mon_setfocused(int mi, Node *n) {
    mons[mi].focused[mons[mi].curws] = n;
}

static void mon_settree_at(int mi, int ws, Node *n) {
    mons[mi].tree[ws] = n;
}

static void mon_setfocused_at(int mi, int ws, Node *n) {
    mons[mi].focused[ws] = n;
}

static Node *mkleaf(Window w) {
    Node *n = calloc(1, sizeof *n);
    n->leaf = 1;
    n->ratio = 0.5f;
    n->win = w;
    return n;
}

static int atom_in_window_property(Window win, Atom prop, Atom value) {
    return window_has_atom(win, prop, value);
}

static int window_is_dialog_like(Window win) {
    Window transient_for;
    if (XGetTransientForHint(dpy, win, &transient_for)) return 1;
    return atom_in_window_property(win, atom_net_wm_window_type, atom_net_wm_window_type_dialog) ||
           atom_in_window_property(win, atom_net_wm_window_type, atom_net_wm_window_type_utility) ||
           atom_in_window_property(win, atom_net_wm_window_type, atom_net_wm_window_type_toolbar) ||
           atom_in_window_property(win, atom_net_wm_window_type, atom_net_wm_window_type_splash);
}

static int window_is_dock(Window win) {
    return atom_in_window_property(win, atom_net_wm_window_type, atom_net_wm_window_type_dock);
}

static int dock_index(Window win) {
    for (int i = 0; i < ndockwins; i++) {
        if (dockwins[i] == win) return i;
    }
    return -1;
}

static void add_dock_window(Window win) {
    if (!win || dock_index(win) >= 0 || ndockwins >= MAXDOCKS) return;
    dockwins[ndockwins++] = win;
}

static void remove_dock_window(Window win) {
    int idx = dock_index(win);
    if (idx < 0) return;
    dockwins[idx] = dockwins[--ndockwins];
    dockwins[ndockwins] = 0;
}

static int any_real_fullscreen_visible(void) {
    for (int i = 0; i < nmons; i++) {
        if (find_real_fullscreen_leaf(mon_tree(i))) return 1;
    }
    return 0;
}

static void sync_dock_stack(void) {
    int hide_for_fullscreen = any_real_fullscreen_visible();
    for (int i = 0; i < ndockwins; i++) {
        if (hide_for_fullscreen) XLowerWindow(dpy, dockwins[i]);
        else XRaiseWindow(dpy, dockwins[i]);
    }
}

static Node *findleaf(Node *n, Window w) {
    if (!n) return NULL;
    if (n->leaf) return n->win == w ? n : NULL;
    Node *r = findleaf(n->a, w);
    return r ? r : findleaf(n->b, w);
}

static Node *findleaf_any(Window w, int *out_mi, int *out_ws) {
    for (int mi = 0; mi < nmons; mi++) {
        for (int ws = 0; ws < MAXWS; ws++) {
            Node *n = findleaf(mon_tree_at(mi, ws), w);
            if (n) {
                if (out_mi) *out_mi = mi;
                if (out_ws) *out_ws = ws;
                return n;
            }
        }
    }
    return NULL;
}

static int monforwin(Window w) {
    for (int i = 0; i < nmons; i++) {
        if (findleaf(mon_tree(i), w)) return i;
    }
    return curmon;
}

static Node *firstleaf(Node *n) {
    while (n && !n->leaf) n = n->a;
    return n;
}

static Node *find_fullscreen_leaf(Node *n) {
    Node *r;
    if (!n) return NULL;
    if (n->leaf) return (n->real_fullscreen || n->fullscreen) ? n : NULL;
    r = find_fullscreen_leaf(n->a);
    return r ? r : find_fullscreen_leaf(n->b);
}

static Node *find_real_fullscreen_leaf(Node *n) {
    Node *r;
    if (!n) return NULL;
    if (n->leaf) return n->real_fullscreen ? n : NULL;
    r = find_real_fullscreen_leaf(n->a);
    return r ? r : find_real_fullscreen_leaf(n->b);
}

static Node *nextleaf(int mi, Node *cur) {
    Node *tree = mon_tree(mi);
    if (!cur || !tree) return firstleaf(tree);
    Node *n = cur;
    while (n->par) {
        if (n->par->a == n) {
            Node *r = firstleaf(n->par->b);
            if (r) return r;
        }
        n = n->par;
    }
    return firstleaf(tree);
}

static Node *prevleaf(int mi, Node *cur) {
    Node *tree = mon_tree(mi);
    Node *prev = NULL;
    Node *n;
    if (!tree) return NULL;
    if (!cur) {
        n = firstleaf(tree);
        if (!n) return NULL;
        while ((n = nextleaf(mi, n)) && n != firstleaf(tree)) prev = n;
        return prev ? prev : firstleaf(tree);
    }
    n = firstleaf(tree);
    while (n) {
        if (n == cur) break;
        prev = n;
        n = nextleaf(mi, n);
        if (n == firstleaf(tree)) break;
    }
    if (prev) return prev;
    n = firstleaf(tree);
    prev = n;
    while ((n = nextleaf(mi, prev)) && n != firstleaf(tree)) prev = n;
    return prev;
}

static void swap_leaf_payload(Node *a, Node *b) {
    Window wtmp;
    int ftmp;
    int fstmp;
    if (!a || !b || a == b || !a->leaf || !b->leaf) return;
    wtmp = a->win;
    a->win = b->win;
    b->win = wtmp;
    ftmp = a->floating;
    a->floating = b->floating;
    b->floating = ftmp;
    fstmp = a->fullscreen;
    a->fullscreen = b->fullscreen;
    b->fullscreen = fstmp;
    fstmp = a->real_fullscreen;
    a->real_fullscreen = b->real_fullscreen;
    b->real_fullscreen = fstmp;
    /* thumbnails follow their window */
    { Pixmap ptmp = a->thumb; a->thumb = b->thumb; b->thumb = ptmp; }
    ftmp = a->thumb_w; a->thumb_w = b->thumb_w; b->thumb_w = ftmp;
    ftmp = a->thumb_h; a->thumb_h = b->thumb_h; b->thumb_h = ftmp;
}

/* ---- XComposite/XRender live thumbnails ---- */

/* largest stored thumbnail edge; windows are downscaled to this once at
   capture time so the overview only ever rescales a small pixmap */
#define THUMB_CAP 512

static void init_composite(void) {
    int evb, erb;
    if (!XCompositeQueryExtension(dpy, &evb, &erb)) return;
    int maj = 0, min = 0;
    XCompositeQueryVersion(dpy, &maj, &min);
    /* XCompositeNameWindowPixmap needs >= 0.2 */
    if (maj == 0 && min < 2) return;
    if (!XRenderQueryExtension(dpy, &evb, &erb)) return;
    /* Automatic: the server keeps painting windows normally on screen, we
       just gain access to their off-screen pixmaps for the overview */
    XCompositeRedirectSubwindows(dpy, root, CompositeRedirectAutomatic);
    composite_ok = 1;
}

static void free_thumb(Node *n) {
    if (n && n->thumb) {
        XFreePixmap(dpy, n->thumb);
        n->thumb = 0;
        n->thumb_w = n->thumb_h = 0;
    }
}

/* blit a source pixmap scaled to fit dst rect via XRender (bilinear) */
static void render_scaled(Picture dst, Pixmap srcpix, int sw_, int sh_,
                          int dx, int dy, int dw, int dh) {
    if (sw_ < 1 || sh_ < 1 || dw < 1 || dh < 1) return;
    int s = DefaultScreen(dpy);
    XRenderPictFormat *fmt = XRenderFindVisualFormat(dpy, DefaultVisual(dpy, s));
    if (!fmt) return;
    Picture src = XRenderCreatePicture(dpy, srcpix, fmt, 0, NULL);
    /* transform maps dst coords -> src coords, so the diagonal is src/dst */
    XTransform xf = {{
        { XDoubleToFixed((double)sw_ / dw), 0, 0 },
        { 0, XDoubleToFixed((double)sh_ / dh), 0 },
        { 0, 0, XDoubleToFixed(1.0) }
    }};
    XRenderSetPictureTransform(dpy, src, &xf);
    XRenderSetPictureFilter(dpy, src, FilterBilinear, NULL, 0);
    XRenderComposite(dpy, PictOpSrc, src, None, dst, 0, 0, 0, 0, dx, dy, dw, dh);
    XRenderFreePicture(dpy, src);
}

/* snapshot one viewable window into its node's cached thumb pixmap */
static void capture_thumb(Node *n) {
    if (!composite_ok || !n || !n->leaf || !n->win) return;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, n->win, &wa)) return;
    if (wa.map_state != IsViewable || wa.width < 1 || wa.height < 1) return;

    XRenderPictFormat *sfmt = XRenderFindVisualFormat(dpy, wa.visual);
    if (!sfmt) return;
    Pixmap wp = XCompositeNameWindowPixmap(dpy, n->win);
    if (!wp) return;

    /* the named pixmap spans the window plus its border */
    int srcw = wa.width + 2 * wa.border_width;
    int srch = wa.height + 2 * wa.border_width;
    double scale = 1.0;
    int mx = srcw > srch ? srcw : srch;
    if (mx > THUMB_CAP) scale = (double)THUMB_CAP / mx;
    int tw = (int)(srcw * scale); if (tw < 1) tw = 1;
    int th = (int)(srch * scale); if (th < 1) th = 1;

    int s = DefaultScreen(dpy);
    if (n->thumb && (n->thumb_w != tw || n->thumb_h != th)) free_thumb(n);
    if (!n->thumb) {
        n->thumb = XCreatePixmap(dpy, root, tw, th, DefaultDepth(dpy, s));
        n->thumb_w = tw;
        n->thumb_h = th;
    }

    Picture src = XRenderCreatePicture(dpy, wp, sfmt, 0, NULL);
    Picture dst = XRenderCreatePicture(dpy, n->thumb,
        XRenderFindVisualFormat(dpy, DefaultVisual(dpy, s)), 0, NULL);
    XTransform xf = {{
        { XDoubleToFixed(1.0 / scale), 0, 0 },
        { 0, XDoubleToFixed(1.0 / scale), 0 },
        { 0, 0, XDoubleToFixed(1.0) }
    }};
    XRenderSetPictureTransform(dpy, src, &xf);
    XRenderSetPictureFilter(dpy, src, FilterBilinear, NULL, 0);
    XRenderComposite(dpy, PictOpSrc, src, None, dst, 0, 0, 0, 0, 0, 0, tw, th);
    XRenderFreePicture(dpy, src);
    XRenderFreePicture(dpy, dst);
    XFreePixmap(dpy, wp);
}

static void capture_tree(Node *n) {
    if (!n) return;
    if (n->leaf) { capture_thumb(n); return; }
    capture_tree(n->a);
    capture_tree(n->b);
}

static void drawborder(Node *n, int focused) {
    XSetWindowBorderWidth(dpy, n->win, n->real_fullscreen ? 0 : bw);
    XSetWindowBorder(dpy, n->win, focused ? cfocus : cnorm);
}

static void raise_floating(Node *n) {
    if (!n) return;
    if (n->leaf) {
        if (n->floating) XRaiseWindow(dpy, n->win);
        return;
    }
    raise_floating(n->a);
    raise_floating(n->b);
}

static void unmap_managed(Node *n) {
    if (!n || !n->leaf) return;
    n->ignore_unmap++;
    XUnmapWindow(dpy, n->win);
}

static void show_only_leaf(Node *n, Node *target) {
    if (!n) return;
    if (n->leaf) {
        if (n == target) XMapRaised(dpy, n->win);
        else unmap_managed(n);
        return;
    }
    show_only_leaf(n->a, target);
    show_only_leaf(n->b, target);
}

static int has_tiled_leaf(Node *n) {
    if (!n) return 0;
    if (n->leaf) return !n->floating;
    return has_tiled_leaf(n->a) || has_tiled_leaf(n->b);
}

static void tilenode(Node *n, int x, int y, int w, int h, Node *foc) {
    if (!n) return;
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    if (n->leaf) {
        drawborder(n, n == foc);
        if (n->real_fullscreen) {
            XMoveResizeWindow(dpy, n->win, x, y, w, h);
        } else if (n->fullscreen) {
            XMoveResizeWindow(dpy, n->win, x, y, w - 2 * bw, h - 2 * bw);
        } else if (!n->floating) {
            int tw = w - 2 * gap - 2 * bw;
            int th = h - 2 * gap - 2 * bw;
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
            XMoveResizeWindow(dpy, n->win, x + gap, y + gap, tw, th);
        }
        return;
    }
    if (!has_tiled_leaf(n->a) && has_tiled_leaf(n->b)) {
        tilenode(n->b, x, y, w, h, foc);
        return;
    }
    if (!has_tiled_leaf(n->b) && has_tiled_leaf(n->a)) {
        tilenode(n->a, x, y, w, h, foc);
        return;
    }
    if (!has_tiled_leaf(n->a) && !has_tiled_leaf(n->b)) return;
    if (n->horiz) {
        int wa = (int)(w * n->ratio);
        tilenode(n->a, x, y, wa, h, foc);
        tilenode(n->b, x + wa, y, w - wa, h, foc);
    } else {
        int ha = (int)(h * n->ratio);
        tilenode(n->a, x, y, w, ha, foc);
        tilenode(n->b, x, y + ha, w, h - ha, foc);
    }
}

static int draw_tag_box(Window win, int x, int y, const char *text,
    unsigned long bg, unsigned long fg, int minw)
{
    int pad = bartextpad;
    int w = textw(text) + pad * 2;
    if (w < minw) w = minw;
    XSetForeground(dpy, bargc, bg);
    XFillRectangle(dpy, win, bargc, x, y, w, barh);
    XSetForeground(dpy, bargc, fg);
    XDrawString(dpy, win, bargc, x + pad, y + barh - 8, text, (int)strlen(text));
    return w;
}

typedef enum {
    BAR_ITEM_NONE = 0,
    BAR_ITEM_WORKSPACES,
    BAR_ITEM_COMMAND,
    BAR_ITEM_TITLE,
    BAR_ITEM_CLOCK,
    BAR_ITEM_BATTERY,
    BAR_ITEM_MODE
} BarItem;

static BarItem parse_bar_item(const char *name) {
    if (!strcasecmp(name, "workspaces")) return BAR_ITEM_WORKSPACES;
    if (!strcasecmp(name, "command")) return BAR_ITEM_COMMAND;
    if (!strcasecmp(name, "title")) return BAR_ITEM_TITLE;
    if (!strcasecmp(name, "clock")) return BAR_ITEM_CLOCK;
    if (!strcasecmp(name, "battery")) return BAR_ITEM_BATTERY;
    if (!strcasecmp(name, "mode")) return BAR_ITEM_MODE;
    return BAR_ITEM_NONE;
}

static void build_bar_item_text(BarItem item, char *dst, size_t dstsz) {
    dst[0] = 0;
    switch (item) {
    case BAR_ITEM_COMMAND:
    case BAR_ITEM_MODE:
        build_mode_text(dst, dstsz);
        break;
    case BAR_ITEM_TITLE:
        build_title_text(dst, dstsz);
        break;
    case BAR_ITEM_CLOCK:
        build_clock_text(dst, dstsz);
        break;
    case BAR_ITEM_BATTERY:
        build_battery_text(dst, dstsz);
        break;
    default:
        break;
    }
}

/* a workspace is shown on monitor mi's bar if that monitor holds windows
   there, or it is that monitor's current workspace */
static int mon_ws_visible_in_bar(int mi, int ws) {
    return mons[mi].tree[ws] != NULL || ws == mons[mi].curws;
}

static int workspace_section_width(void) {
    int width = 0;
    for (int i = 0; i < MAXWS; i++) {
        char part[8];
        if (!mon_ws_visible_in_bar(barmon, i)) continue;
        snprintf(part, sizeof part, "%d", i + 1);
        width += textw(part) + bartextpad * 2 + baritemgap;
    }
    return width;
}

static int draw_workspace_section(Window win, int x, int y) {
    int curx = x;
    for (int i = 0; i < MAXWS; i++) {
        char part[8];
        unsigned long bg = barbg, fg = barmutedfg;
        if (!mon_ws_visible_in_bar(barmon, i)) continue;
        snprintf(part, sizeof part, "%d", i + 1);
        if (i == mons[barmon].curws) {
            bg = baraccentbg;
            fg = baraccentfg;
        } else if (mons[barmon].tree[i]) {
            fg = barfg;
        }
        curx += draw_tag_box(win, curx, y, part, bg, fg, barwsminw);
        curx += baritemgap;
    }
    return curx;
}

static int bar_item_width(BarItem item) {
    char tmp[256];

    if (item == BAR_ITEM_WORKSPACES) return workspace_section_width();
    build_bar_item_text(item, tmp, sizeof tmp);
    if (!tmp[0]) return 0;

    switch (item) {
    case BAR_ITEM_COMMAND:
        return textw(tmp) + bartextpad * 2 +
            ((mode == MODE_COMMAND && cmdline_len > 0) ? baritemgap + textw(cmdline) + bartextpad * 2 : 0);
    case BAR_ITEM_TITLE:
    case BAR_ITEM_CLOCK:
    case BAR_ITEM_BATTERY:
    case BAR_ITEM_MODE:
        return textw(tmp) + bartextpad * 2;
    default:
        return 0;
    }
}

static int draw_bar_item(Window win, int x, int y, BarItem item) {
    char tmp[256];
    int curx = x;

    if (item == BAR_ITEM_WORKSPACES) return draw_workspace_section(win, x, y);

    build_bar_item_text(item, tmp, sizeof tmp);
    if (!tmp[0]) return curx;

    switch (item) {
    case BAR_ITEM_COMMAND:
        curx += draw_tag_box(win, curx, y, tmp, baraccentbg, baraccentfg, 0);
        if (mode == MODE_COMMAND && cmdline_len > 0) {
            curx += baritemgap;
            curx += draw_tag_box(win, curx, y, cmdline, cnorm, barfg, 0);
        }
        return curx;
    case BAR_ITEM_TITLE:
        return curx + draw_tag_box(win, curx, y, tmp, barbg, barfg, 0);
    case BAR_ITEM_CLOCK:
    case BAR_ITEM_MODE:
        return curx + draw_tag_box(win, curx, y, tmp, baraccentbg, baraccentfg, 0);
    case BAR_ITEM_BATTERY:
        return curx + draw_tag_box(win, curx, y, tmp, cnorm, barfg, 0);
    default:
        return curx;
    }
}

static int section_width(const char *cfg) {
    char buf[128], *tok, *save;
    int width = 0;
    copystr(buf, sizeof buf, cfg);
    for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char item[128];
        BarItem kind;
        copystr(item, sizeof item, ltrim(tok));
        rtrim(item);
        kind = parse_bar_item(item);
        if (kind != BAR_ITEM_NONE) width += bar_item_width(kind) + baritemgap;
    }
    return width > 0 ? width - baritemgap : 0;
}

static int draw_section(Window win, int x, int y, const char *cfg) {
    char buf[128], *tok, *save;
    int curx = x;
    copystr(buf, sizeof buf, cfg);
    for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char item[128];
        BarItem kind;
        copystr(item, sizeof item, ltrim(tok));
        rtrim(item);
        kind = parse_bar_item(item);
        if (kind == BAR_ITEM_NONE) continue;
        curx = draw_bar_item(win, curx, y, kind);
        curx += baritemgap;
    }
    return curx;
}

static void drawbar(int mi) {
    Mon *m = &mons[mi];
    if (!m->barwin) return;
    barmon = mi;

    if (!m->barpix)
        m->barpix = XCreatePixmap(dpy, m->barwin, m->w, barh, DefaultDepth(dpy, DefaultScreen(dpy)));

    int leftx = barpadx, centx, rightx, leftw;
    int centerw = section_width(barcentercfg);
    int rightw = section_width(barrightcfg);

    XSetForeground(dpy, bargc, barbg);
    XFillRectangle(dpy, m->barpix, bargc, 0, 0, m->w, barh);

    leftw = section_width(barleftcfg);
    draw_section(m->barpix, leftx, 0, barleftcfg);
    rightx = m->w - rightw - barpadx;
    if (rightw > 0) draw_section(m->barpix, rightx, 0, barrightcfg);
    centx = (m->w - centerw) / 2;
    if (centerw > 0 && centx > leftx + leftw + 12 && centx + centerw < rightx - 12)
        draw_section(m->barpix, centx, 0, barcentercfg);

    XCopyArea(dpy, m->barpix, m->barwin, bargc, 0, 0, m->w, barh, 0, 0);
}

static int bar_uses_item(const char *item) {
    return strstr(barleftcfg, item) || strstr(barcentercfg, item) || strstr(barrightcfg, item);
}

static void drawbars_if_clock_changed(void) {
    time_t now = time(NULL);
    struct tm tm_now;
    if (!bar_uses_item("clock") && !bar_uses_item("battery")) return;
    localtime_r(&now, &tm_now);
    if (tm_now.tm_sec != last_clock_second) {
        drawbars();
    }
}

static void wait_for_x_event_or_clock_tick(void) {
    int xfd = ConnectionNumber(dpy);
    fd_set readfds;

    XFlush(dpy);
    FD_ZERO(&readfds);
    FD_SET(xfd, &readfds);

    if (bar_uses_item("clock") || bar_uses_item("battery")) {
        struct timespec now;
        struct timeval tv;
        long usec;

        clock_gettime(CLOCK_REALTIME, &now);
        usec = 1000000L - now.tv_nsec / 1000L;
        if (usec <= 0) usec = 1000L;
        tv.tv_sec = usec / 1000000L;
        tv.tv_usec = usec % 1000000L;
        select(xfd + 1, &readfds, NULL, NULL, &tv);
    } else {
        select(xfd + 1, &readfds, NULL, NULL, NULL);
    }
}

static void drawbars(void) {
    if (!barenabled) return;
    for (int i = 0; i < nmons; i++) drawbar(i);
    XFlush(dpy);
}

static void retile(void) {
    for (int i = 0; i < nmons; i++) {
        Mon *m = &mons[i];
        Node *fs = find_fullscreen_leaf(mon_tree(i));
        if (fs) {
            show_only_leaf(mon_tree(i), fs);
            drawborder(fs, fs == mon_focused(i));
            if (fs->real_fullscreen) {
                if (barenabled && m->barwin) XUnmapWindow(dpy, m->barwin);
                XMoveResizeWindow(dpy, fs->win, m->x, m->y, m->w, m->h);
            } else {
                if (barenabled && m->barwin) XMapRaised(dpy, m->barwin);
                XMoveResizeWindow(dpy, fs->win, m->wx, m->wy, m->ww - 2 * bw, m->wh - 2 * bw);
            }
            XRaiseWindow(dpy, fs->win);
        } else {
            if (barenabled && m->barwin) XMapRaised(dpy, m->barwin);
            showtree(mon_tree(i));
            tilenode(mon_tree(i), m->wx, m->wy, m->ww, m->wh, mon_focused(i));
            raise_floating(mon_tree(i));
            Node *foc_i = mon_focused(i);
            if (foc_i && (foc_i->floating || foc_i->fullscreen)) XRaiseWindow(dpy, foc_i->win);
        }
        if (barenabled && m->barwin && !find_real_fullscreen_leaf(mon_tree(i))) XRaiseWindow(dpy, m->barwin);
    }
    sync_dock_stack();
    drawbars();
}

static void warpfocus(Node *n) {
    if (!n || !n->leaf) return;
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, n->x + n->w / 2, n->y + n->h / 2);
}

static void setfocus(int mi, Node *n, int warp) {
    if (!n || !n->leaf) return;
    Node *fs = find_fullscreen_leaf(mon_tree(mi));
    if (fs && fs != n) { n = fs; warp = 0; }
    Node *prev = mon_focused(mi);
    mon_setfocused(mi, n);
    curmon = mi;
    if (prev && prev != n) drawborder(prev, 0);
    if (mode == MODE_INSERT) XSetInputFocus(dpy, n->win, RevertToPointerRoot, CurrentTime);
    drawborder(n, 1);
    if (n->floating || n->fullscreen || n->real_fullscreen) XRaiseWindow(dpy, n->win);
    if (barenabled && mons[mi].barwin) XRaiseWindow(dpy, mons[mi].barwin);
    if (warp) warpfocus(n);
    update_active_window();
    refresh_cached_title();
}

static void showtree(Node *n) {
    if (!n) return;
    if (n->leaf) {
        XMapWindow(dpy, n->win);
        return;
    }
    showtree(n->a);
    showtree(n->b);
}

static void free_tree(Node *n) {
    if (!n) return;
    if (!n->leaf) {
        free_tree(n->a);
        free_tree(n->b);
    } else {
        free_thumb(n);
    }
    free(n);
}

/* free only the internal split nodes of a tree, leaving leaf nodes intact */
static void free_splits(Node *n) {
    if (!n || n->leaf) return;
    free_splits(n->a);
    free_splits(n->b);
    free(n);
}

/* fill out[] with current monitor geometry via Xinerama; returns monitor count */
static int querygeom(Mon *out, int max) {
    int n = 0;
    if (XineramaIsActive(dpy)) {
        XineramaScreenInfo *xi = XineramaQueryScreens(dpy, &n);
        if (xi) {
            if (n > max) n = max;
            for (int i = 0; i < n; i++) {
                out[i].x = xi[i].x_org;
                out[i].y = xi[i].y_org;
                out[i].w = xi[i].width;
                out[i].h = xi[i].height;
            }
            XFree(xi);
        }
    }
    if (n < 1) {
        n = 1;
        out[0].x = 0;
        out[0].y = 0;
        out[0].w = DisplayWidth(dpy, DefaultScreen(dpy));
        out[0].h = DisplayHeight(dpy, DefaultScreen(dpy));
    }
    return n;
}

/* re-read monitor layout after a hotplug/resolution change.
 * Returns 1 if the layout changed (caller should rebuild bars and retile). */
static int updategeom(void) {
    Mon ng[MAXMONS];
    int nn = querygeom(ng, MAXMONS);

    int changed = (nn != nmons);
    for (int i = 0; i < nn && !changed; i++) {
        if (ng[i].x != mons[i].x || ng[i].y != mons[i].y ||
            ng[i].w != mons[i].w || ng[i].h != mons[i].h) changed = 1;
    }
    if (!changed) return 0;
    if (overview_active) close_overview();

    /* monitors removed: migrate their windows onto the last surviving monitor */
    if (nn < nmons) {
        int target = nn - 1;
        for (int mi = nn; mi < nmons; mi++) {
            for (int ws = 0; ws < MAXWS; ws++) {
                Node *tree = mons[mi].tree[ws];
                if (!tree) continue;
                Node *leaves[256];
                int cnt = 0;
                collect_leaves(tree, leaves, &cnt, 256);
                mons[mi].tree[ws] = NULL;
                mons[mi].focused[ws] = NULL;
                free_splits(tree);
                for (int k = 0; k < cnt; k++) {
                    Node *lf = leaves[k];
                    lf->par = NULL;
                    lf->a = lf->b = NULL;
                    attach_to_ws(target, ws, lf);
                }
            }
        }
        /* hide windows migrated onto a non-current workspace of the target */
        for (int ws = 0; ws < MAXWS; ws++)
            if (ws != mons[target].curws) hidetree(mons[target].tree[ws]);
    }

    for (int i = 0; i < nn; i++) {
        mons[i].x = ng[i].x;
        mons[i].y = ng[i].y;
        mons[i].w = ng[i].w;
        mons[i].h = ng[i].h;
    }
    nmons = nn;
    if (curmon >= nmons) curmon = nmons - 1;

    sw = DisplayWidth(dpy, DefaultScreen(dpy));
    sh = DisplayHeight(dpy, DefaultScreen(dpy));
    return 1;
}

static void initatoms(void) {
    atom_net_active_window = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    atom_net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    atom_net_current_desktop = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    atom_net_number_of_desktops = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    atom_net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    atom_net_supporting_wm_check = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    atom_utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
    atom_net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    atom_net_wm_window_type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    atom_net_wm_window_type_dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    atom_net_wm_window_type_utility = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    atom_net_wm_window_type_toolbar = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
    atom_net_wm_window_type_splash = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    atom_net_wm_desktop = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    atom_net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    atom_net_wm_state_fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    atom_net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    atom_net_wm_strut = XInternAtom(dpy, "_NET_WM_STRUT", False);
    atom_net_wm_strut_partial = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
    atom_wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    atom_wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    atom_net_wm_pid = XInternAtom(dpy, "_NET_WM_PID", False);
}

static void setup_wm_check(void) {
    static const char name[] = "nvwm";
    wmcheckwin = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, wmcheckwin, atom_net_supporting_wm_check, XA_WINDOW, 32,
        PropModeReplace, (unsigned char *)&wmcheckwin, 1);
    XChangeProperty(dpy, root, atom_net_supporting_wm_check, XA_WINDOW, 32,
        PropModeReplace, (unsigned char *)&wmcheckwin, 1);
    XChangeProperty(dpy, wmcheckwin, atom_net_wm_name,
        atom_utf8_string, 8, PropModeReplace, (const unsigned char *)name, (int)strlen(name));
}

static void update_supported_atoms(void) {
    Atom supported[] = {
        atom_net_active_window,
        atom_net_client_list,
        atom_net_current_desktop,
        atom_net_number_of_desktops,
        atom_net_wm_window_type,
        atom_net_wm_window_type_dock,
        atom_net_wm_window_type_dialog,
        atom_net_wm_window_type_utility,
        atom_net_wm_window_type_toolbar,
        atom_net_wm_window_type_splash,
        atom_net_wm_desktop,
        atom_net_wm_state,
        atom_net_wm_state_fullscreen,
        atom_net_wm_strut,
        atom_net_wm_strut_partial,
    };
    XChangeProperty(dpy, root, atom_net_supported, XA_ATOM, 32, PropModeReplace,
        (unsigned char *)supported, (int)(sizeof supported / sizeof supported[0]));
}

static void collect_client_windows(Node *n, Window *list, int *count, int maxcount) {
    if (!n || *count >= maxcount) return;
    if (n->leaf) {
        list[(*count)++] = n->win;
        return;
    }
    collect_client_windows(n->a, list, count, maxcount);
    collect_client_windows(n->b, list, count, maxcount);
}

static void update_client_list(void) {
    Window list[1024];
    int count = 0;
    for (int mi = 0; mi < nmons; mi++) {
        for (int ws = 0; ws < MAXWS; ws++) {
            collect_client_windows(mon_tree_at(mi, ws), list, &count, 1024);
        }
    }
    XChangeProperty(dpy, root, atom_net_client_list, XA_WINDOW, 32, PropModeReplace,
        (unsigned char *)list, count);
}

static void update_current_desktop(void) {
    unsigned long ws = (unsigned long)mons[curmon].curws;
    XChangeProperty(dpy, root, atom_net_current_desktop, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&ws, 1);
}

static void update_number_of_desktops(void) {
    unsigned long n = MAXWS;
    XChangeProperty(dpy, root, atom_net_number_of_desktops, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&n, 1);
}

static void update_active_window(void) {
    Window win = None;
    if (mon_focused(curmon)) win = mon_focused(curmon)->win;
    XChangeProperty(dpy, root, atom_net_active_window, XA_WINDOW, 32, PropModeReplace,
        (unsigned char *)&win, 1);
}

static void set_window_desktop(Window win, int ws) {
    unsigned long val = (unsigned long)ws;
    XChangeProperty(dpy, win, atom_net_wm_desktop, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&val, 1);
}

static void apply_rules(Node *n, int *targetws, int *out_follow) {
    char class_name[64], instance_name[64], title[128];
    int want_follow = 0;
    int base_ws = targetws ? *targetws : 0;
    if (!n || !n->leaf) return;
    get_window_class(n->win, class_name, sizeof class_name, instance_name, sizeof instance_name);
    get_window_title(n->win, title, sizeof title);

    if (window_is_dialog_like(n->win)) n->floating = 1;

    for (int i = 0; i < nrules; i++) {
        Rule *r = &rules[i];
        int match = 1;
        if (r->class_name[0] && strcmp(r->class_name, class_name)) match = 0;
        if (r->instance_name[0] && strcmp(r->instance_name, instance_name)) match = 0;
        if (r->title[0] && strcmp(r->title, title)) match = 0;
        if (!match) continue;
        if (r->set_floating) n->floating = r->floating;
        if (targetws && r->workspace >= 0) *targetws = r->workspace;
        if (r->follow_class) want_follow = 1;
    }

    /* follow_class: if no explicit workspace rule fired, find existing window
       of same class on another workspace and follow it */
    if (want_follow && targetws && *targetws == base_ws && class_name[0]) {
        for (int ws = 0; ws < MAXWS; ws++) {
            if (ws == base_ws) continue;
            int found = 0;
            for (int mi = 0; mi < nmons && !found; mi++) {
                Node *leaves[256];
                int count = 0;
                collect_leaves(mon_tree_at(mi, ws), leaves, &count, 256);
                for (int li = 0; li < count; li++) {
                    char lc[64], li2[64];
                    get_window_class(leaves[li]->win, lc, sizeof lc, li2, sizeof li2);
                    if (!strcmp(lc, class_name)) { found = 1; break; }
                }
            }
            if (found) {
                *targetws = ws;
                if (out_follow) *out_follow = 1;
                break;
            }
        }
    }
}

static void hidetree(Node *n) {
    if (!n) return;
    if (n->leaf) {
        unmap_managed(n);
        return;
    }
    hidetree(n->a);
    hidetree(n->b);
}

/* switch only monitor mi to workspace ws; other monitors keep their own */
static void switch_workspace_on(int mi, int ws) {
    if (mi < 0 || mi >= nmons || ws < 0 || ws >= MAXWS || ws == mons[mi].curws) return;
    /* snapshot the windows we are about to hide so the overview can still
       show their contents while they are unmapped */
    if (composite_ok) capture_tree(mons[mi].tree[mons[mi].curws]);
    hidetree(mons[mi].tree[mons[mi].curws]);
    mons[mi].curws = ws;
    showtree(mons[mi].tree[ws]);
    retile();
    if (mi == curmon) update_current_desktop();
    if (mon_focused(mi)) setfocus(mi, mon_focused(mi), 1);
    else if (mi == curmon) update_active_window();
}

/* the monitor the user is looking at: the one under the pointer */
static int pointer_mon(void) {
    Window dw; int rx, ry, wx, wy; unsigned int msk;
    if (XQueryPointer(dpy, root, &dw, &dw, &rx, &ry, &wx, &wy, &msk))
        return monforpt(rx, ry);
    return curmon;
}

/* keyboard workspace switching acts on the monitor under the pointer, so it
   never targets a stale curmon left on another monitor (matches scroll-wheel) */
static void switch_workspace(int ws) {
    int mi = pointer_mon();
    curmon = mi;
    switch_workspace_on(mi, ws);
}

static void attach_to_ws(int mi, int ws, Node *leaf) {
    leaf->par = NULL;
    set_window_desktop(leaf->win, ws);
    if (!mon_tree_at(mi, ws)) {
        mon_settree_at(mi, ws, leaf);
        mon_setfocused_at(mi, ws, leaf);
        update_client_list();
        return;
    }
    Node *focused = mon_focused_at(mi, ws);
    Node *tree = mon_tree_at(mi, ws);
    Node *t = focused && focused->leaf ? focused : firstleaf(tree);
    int horiz = t->w > 0 ? (t->w >= t->h) : (mons[mi].ww >= mons[mi].wh);
    Node *sp = calloc(1, sizeof *sp);
    sp->ratio = 0.5f;
    sp->horiz = horiz;
    sp->par = t->par;
    sp->a = t;
    sp->b = leaf;
    if (!t->par) mon_settree_at(mi, ws, sp);
    else if (t->par->a == t) t->par->a = sp;
    else t->par->b = sp;
    t->par = sp;
    leaf->par = sp;
    mon_setfocused_at(mi, ws, leaf);
    update_client_list();
}

static void attach(int mi, Node *leaf) { attach_to_ws(mi, mons[mi].curws, leaf); }

static void detach_from_ws(int mi, int ws, Node *n) {
    if (!n->par) {
        mon_settree_at(mi, ws, NULL);
        mon_setfocused_at(mi, ws, NULL);
        update_client_list();
        return;
    }
    Node *p = n->par, *sib = p->a == n ? p->b : p->a;
    sib->par = p->par;
    if (!p->par) mon_settree_at(mi, ws, sib);
    else if (p->par->a == p) p->par->a = sib;
    else p->par->b = sib;
    if (mon_focused_at(mi, ws) == n) mon_setfocused_at(mi, ws, firstleaf(sib));
    free(p);
    n->par = NULL;
    update_client_list();
}

static void detach(int mi, Node *n) { detach_from_ws(mi, mons[mi].curws, n); }

static void removewin(Window w) {
    int mi = 0, ws = 0;
    Node *n = findleaf_any(w, &mi, &ws);
    if (!n) return;
    if (drag_node == n) {
        drag_node = NULL;
        drag_mode = 0;
    }
    if (ov_drag_node == n) { ov_drag_node = NULL; ov_drag = 0; }
    free_thumb(n);
    detach_from_ws(mi, ws, n);
    free(n);
    update_active_window();
}

static void collect_leaves(Node *n, Node **list, int *count, int maxcount) {
    if (!n || *count >= maxcount) return;
    if (n->leaf) {
        list[(*count)++] = n;
        return;
    }
    collect_leaves(n->a, list, count, maxcount);
    collect_leaves(n->b, list, count, maxcount);
}

/* tiled (non-floating) leaf on monitor mi whose tile rect holds (px,py) */
static Node *tiled_leaf_at(int mi, int px, int py) {
    Node *list[64];
    int cnt = 0;
    collect_leaves(mon_tree(mi), list, &cnt, 64);
    for (int i = 0; i < cnt; i++) {
        Node *n = list[i];
        if (n->floating) continue;
        if (px >= n->x && px < n->x + n->w && py >= n->y && py < n->y + n->h)
            return n;
    }
    return NULL;
}

static int interval_overlap(int a1, int a2, int b1, int b2) {
    int lo = a1 > b1 ? a1 : b1;
    int hi = a2 < b2 ? a2 : b2;
    return hi > lo ? hi - lo : 0;
}

static Node *find_directional_focus(int mi, Node *from, const char *dir) {
    Node *nodes[256];
    int count = 0;
    Node *best = NULL;
    long bestscore = 0;
    int fx1, fy1, fx2, fy2, fcx, fcy;

    if (!from) return NULL;
    collect_leaves(mon_tree(mi), nodes, &count, 256);
    fx1 = from->x;
    fy1 = from->y;
    fx2 = from->x + from->w;
    fy2 = from->y + from->h;
    fcx = from->x + from->w / 2;
    fcy = from->y + from->h / 2;

    for (int i = 0; i < count; i++) {
        Node *n = nodes[i];
        int nx1, ny1, nx2, ny2, ncx, ncy;
        int primary, secondary, overlap;
        long score;
        if (n == from) continue;
        nx1 = n->x;
        ny1 = n->y;
        nx2 = n->x + n->w;
        ny2 = n->y + n->h;
        ncx = n->x + n->w / 2;
        ncy = n->y + n->h / 2;

        if (!strcmp(dir, "left")) {
            primary = fx1 - nx2;
            overlap = interval_overlap(fy1, fy2, ny1, ny2);
            secondary = abs(fcy - ncy);
        } else if (!strcmp(dir, "right")) {
            primary = nx1 - fx2;
            overlap = interval_overlap(fy1, fy2, ny1, ny2);
            secondary = abs(fcy - ncy);
        } else if (!strcmp(dir, "up")) {
            primary = fy1 - ny2;
            overlap = interval_overlap(fx1, fx2, nx1, nx2);
            secondary = abs(fcx - ncx);
        } else {
            primary = ny1 - fy2;
            overlap = interval_overlap(fx1, fx2, nx1, nx2);
            secondary = abs(fcx - ncx);
        }

        if (primary <= 0) continue;
        score = (long)primary * 100000L + (long)(secondary - overlap) * 100L + secondary;
        if (!best || score < bestscore) {
            best = n;
            bestscore = score;
        }
    }
    if (!best) {
        if (!strcmp(dir, "left") || !strcmp(dir, "up")) return prevleaf(mi, from);
        return nextleaf(mi, from);
    }
    return best;
}

static int swap_in_direction(const char *dir) {
    Node *cur = mon_focused(curmon);
    Node *n;
    Window focused_win;
    if (!cur) return 1;
    n = find_directional_focus(curmon, cur, dir);
    if (!n || n == cur) return 1;
    focused_win = cur->win;
    swap_leaf_payload(cur, n);
    retile();
    setfocus(curmon, findleaf(mon_tree(curmon), focused_win), 1);
    return 1;
}

static void move_focused_to_workspace(int targetws) {
    Node *n;
    int srcmon = curmon;
    int srcws = mons[srcmon].curws;
    if (targetws < 0 || targetws >= MAXWS || targetws == srcws) return;
    n = mon_focused(srcmon);
    if (!n) return;
    detach_from_ws(srcmon, srcws, n);
    attach_to_ws(srcmon, targetws, n);
    hidetree(n);
    retile();
    if (mon_focused(curmon)) setfocus(curmon, mon_focused(curmon), 1);
}

static void move_focused_to_workspace_and_follow(int targetws) {
    if (targetws < 0 || targetws >= MAXWS || targetws == mons[curmon].curws) return;
    move_focused_to_workspace(targetws);
    switch_workspace(targetws);
}

/* ---- workspace overview (Super+Z), niri-style schematic ---- */

#define OV_COLS 3
#define OV_ROWS 3

/* mirror of tilenode's split math, drawing scaled boxes instead of moving
   real windows; used to render a miniature of a workspace in the overview */
static void overview_draw_node(Node *n, int x, int y, int w, int h, Node *foc,
                               Picture pic) {
    if (!n || w < 2 || h < 2) return;
    if (n->leaf) {
        int focused = (n == foc);
        if (pic && n->thumb) {
            /* live (or last-seen) window contents */
            render_scaled(pic, n->thumb, n->thumb_w, n->thumb_h,
                          x + 1, y + 1, w - 2, h - 2);
        } else {
            unsigned long bg = focused ? baraccentbg : cnorm;
            XSetForeground(dpy, bargc, bg);
            XFillRectangle(dpy, overview_pix, bargc, x + 1, y + 1, w - 2, h - 2);
        }
        XSetForeground(dpy, bargc, focused ? baraccentbg : barbg);
        XDrawRectangle(dpy, overview_pix, bargc, x, y, w - 1, h - 1);
        if (w > 44 && h > 18 && barfont) {
            char title[128];
            get_window_title(n->win, title, sizeof title);
            int len = (int)strlen(title);
            while (len > 0 && textw(title) > w - 10) title[--len] = 0;
            if (len > 0) {
                /* legible title bar over the thumbnail */
                XSetForeground(dpy, bargc, focused ? baraccentbg : barbg);
                XFillRectangle(dpy, overview_pix, bargc, x + 1, y + 1, w - 2, 18);
                XSetForeground(dpy, bargc, focused ? baraccentfg : barfg);
                XDrawString(dpy, overview_pix, bargc, x + 5, y + 14, title, len);
            }
        }
        return;
    }
    if (!has_tiled_leaf(n->a) && has_tiled_leaf(n->b)) {
        overview_draw_node(n->b, x, y, w, h, foc, pic);
        return;
    }
    if (!has_tiled_leaf(n->b) && has_tiled_leaf(n->a)) {
        overview_draw_node(n->a, x, y, w, h, foc, pic);
        return;
    }
    if (!has_tiled_leaf(n->a) && !has_tiled_leaf(n->b)) return;
    if (n->horiz) {
        int wa = (int)(w * n->ratio);
        overview_draw_node(n->a, x, y, wa, h, foc, pic);
        overview_draw_node(n->b, x + wa, y, w - wa, h, foc, pic);
    } else {
        int ha = (int)(h * n->ratio);
        overview_draw_node(n->a, x, y, w, ha, foc, pic);
        overview_draw_node(n->b, x, y + ha, w, h - ha, foc, pic);
    }
}

static void overview_cell_rect(int ws, int *cx, int *cy, int *cw, int *ch) {
    Mon *m = &mons[overview_mon];
    int pad = 16;
    int cellw = (m->w - pad * (OV_COLS + 1)) / OV_COLS;
    int cellh = (m->h - pad * (OV_ROWS + 1)) / OV_ROWS;
    *cx = pad + (ws % OV_COLS) * (cellw + pad);
    *cy = pad + (ws / OV_COLS) * (cellh + pad);
    *cw = cellw;
    *ch = cellh;
}

static void draw_overview(void) {
    Mon *m = &mons[overview_mon];
    if (!overview_pix) return;

    Picture pic = None;
    if (composite_ok) {
        int s = DefaultScreen(dpy);
        pic = XRenderCreatePicture(dpy, overview_pix,
            XRenderFindVisualFormat(dpy, DefaultVisual(dpy, s)), 0, NULL);
    }

    XSetForeground(dpy, bargc, barbg);
    XFillRectangle(dpy, overview_pix, bargc, 0, 0, m->w, m->h);
    for (int ws = 0; ws < MAXWS; ws++) {
        int cx, cy, cw, ch;
        overview_cell_rect(ws, &cx, &cy, &cw, &ch);
        int sel = (ws == overview_sel);
        int cur = (ws == m->curws);
        XSetForeground(dpy, bargc, 0x1a1a1a);
        XFillRectangle(dpy, overview_pix, bargc, cx, cy, cw, ch);

        char lbl[8];
        snprintf(lbl, sizeof lbl, "%d", ws + 1);
        XSetForeground(dpy, bargc, cur ? baraccentbg : barmutedfg);
        if (barfont) XDrawString(dpy, overview_pix, bargc, cx + 6, cy + 16, lbl, (int)strlen(lbl));

        int ix = cx + 6, iy = cy + 24, iw = cw - 12, ih = ch - 30;
        if (m->tree[ws]) overview_draw_node(m->tree[ws], ix, iy, iw, ih, m->focused[ws], pic);

        unsigned long border = sel ? baraccentbg : (cur ? barfg : cnorm);
        XSetForeground(dpy, bargc, border);
        XDrawRectangle(dpy, overview_pix, bargc, cx, cy, cw - 1, ch - 1);
        if (sel) XDrawRectangle(dpy, overview_pix, bargc, cx + 1, cy + 1, cw - 3, ch - 3);
    }

    /* ghost of the window being dragged, following the cursor */
    if (ov_drag && ov_drag_node && ov_moved) {
        int gw = 220, gh = 140;
        int gx = ov_ptr_x - gw / 2, gy = ov_ptr_y - gh / 2;
        if (pic && ov_drag_node->thumb)
            render_scaled(pic, ov_drag_node->thumb, ov_drag_node->thumb_w,
                          ov_drag_node->thumb_h, gx, gy, gw, gh);
        else {
            XSetForeground(dpy, bargc, baraccentbg);
            XFillRectangle(dpy, overview_pix, bargc, gx, gy, gw, gh);
        }
        XSetForeground(dpy, bargc, baraccentbg);
        XDrawRectangle(dpy, overview_pix, bargc, gx, gy, gw - 1, gh - 1);
    }

    if (pic != None) XRenderFreePicture(dpy, pic);
    XCopyArea(dpy, overview_pix, overview_win, bargc, 0, 0, m->w, m->h, 0, 0);
}

static void open_overview(void) {
    if (overview_active) return;
    /* always open on the monitor under the pointer */
    {
        Window dw; int rx, ry, wx, wy; unsigned int msk;
        if (XQueryPointer(dpy, root, &dw, &dw, &rx, &ry, &wx, &wy, &msk))
            overview_mon = monforpt(rx, ry);
        else
            overview_mon = curmon;
    }
    overview_sel = mons[overview_mon].curws;
    ov_drag = 0; ov_drag_node = NULL; ov_moved = 0;
    Mon *m = &mons[overview_mon];
    /* refresh thumbnails of every monitor's visible workspace */
    if (composite_ok)
        for (int mi = 0; mi < nmons; mi++)
            capture_tree(mons[mi].tree[mons[mi].curws]);
    XSetWindowAttributes wa;
    wa.override_redirect = True;
    wa.background_pixel = barbg;
    wa.event_mask = ExposureMask | ButtonPressMask;
    overview_win = XCreateWindow(dpy, root, m->x, m->y, m->w, m->h, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
    overview_pix = XCreatePixmap(dpy, overview_win, m->w, m->h,
        DefaultDepth(dpy, DefaultScreen(dpy)));
    XMapRaised(dpy, overview_win);
    XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    /* actively grab the pointer so we receive motion + release for dragging */
    XGrabPointer(dpy, overview_win, True,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None, normalcursor, CurrentTime);
    overview_active = 1;
    draw_overview();
}

static void close_overview(void) {
    if (!overview_active) return;
    overview_active = 0;
    ov_drag = 0; ov_drag_node = NULL; ov_moved = 0;
    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    if (overview_pix) { XFreePixmap(dpy, overview_pix); overview_pix = 0; }
    if (overview_win) { XDestroyWindow(dpy, overview_win); overview_win = 0; }
    /* restore the normal/command-mode keyboard grab if the WM held one */
    if (keyboard_grabbed)
        XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    if (mon_focused(curmon)) setfocus(curmon, mon_focused(curmon), 0);
}

static void overview_choose(int ws) {
    int mi = overview_mon;
    close_overview();
    switch_workspace_on(mi, ws);
}

static void overview_key(KeySym sym) {
    switch (sym) {
    case XK_Escape:
    case XK_q:
    case XK_z:
        close_overview();
        return;
    case XK_Return:
    case XK_KP_Enter:
    case XK_space:
        overview_choose(overview_sel);
        return;
    case XK_Left:
    case XK_h:
        if (overview_sel % OV_COLS) { overview_sel--; draw_overview(); }
        return;
    case XK_Right:
    case XK_l:
        if (overview_sel % OV_COLS != OV_COLS - 1 && overview_sel + 1 < MAXWS) {
            overview_sel++; draw_overview();
        }
        return;
    case XK_Up:
    case XK_k:
        if (overview_sel >= OV_COLS) { overview_sel -= OV_COLS; draw_overview(); }
        return;
    case XK_Down:
    case XK_j:
        if (overview_sel + OV_COLS < MAXWS) { overview_sel += OV_COLS; draw_overview(); }
        return;
    default:
        if (sym >= XK_1 && sym <= XK_9) overview_choose((int)(sym - XK_1));
        return;
    }
}

/* which leaf, if any, sits under (px,py) — mirrors overview_draw_node layout */
static Node *overview_node_at(Node *n, int x, int y, int w, int h,
                             int px, int py) {
    if (!n || w < 2 || h < 2) return NULL;
    if (n->leaf) {
        if (px >= x && px < x + w && py >= y && py < y + h) return n;
        return NULL;
    }
    if (!has_tiled_leaf(n->a) && has_tiled_leaf(n->b))
        return overview_node_at(n->b, x, y, w, h, px, py);
    if (!has_tiled_leaf(n->b) && has_tiled_leaf(n->a))
        return overview_node_at(n->a, x, y, w, h, px, py);
    if (!has_tiled_leaf(n->a) && !has_tiled_leaf(n->b)) return NULL;
    if (n->horiz) {
        int wa = (int)(w * n->ratio);
        Node *r = overview_node_at(n->a, x, y, wa, h, px, py);
        return r ? r : overview_node_at(n->b, x + wa, y, w - wa, h, px, py);
    }
    int ha = (int)(h * n->ratio);
    Node *r = overview_node_at(n->a, x, y, w, ha, px, py);
    return r ? r : overview_node_at(n->b, x, y + ha, w, h - ha, px, py);
}

/* resolve a root-coordinate point to a workspace cell and (optionally) a leaf */
static Node *overview_pick(int rx, int ry, int *out_ws) {
    Mon *m = &mons[overview_mon];
    int lx = rx - m->x, ly = ry - m->y;
    if (out_ws) *out_ws = -1;
    for (int ws = 0; ws < MAXWS; ws++) {
        int cx, cy, cw, ch;
        overview_cell_rect(ws, &cx, &cy, &cw, &ch);
        if (lx >= cx && lx < cx + cw && ly >= cy && ly < cy + ch) {
            if (out_ws) *out_ws = ws;
            int ix = cx + 6, iy = cy + 24, iw = cw - 12, ih = ch - 30;
            return overview_node_at(m->tree[ws], ix, iy, iw, ih, lx, ly);
        }
    }
    return NULL;
}

static void overview_press(int rx, int ry) {
    int ws;
    Node *leaf = overview_pick(rx, ry, &ws);
    ov_drag = (ws >= 0);
    ov_drag_node = leaf;
    ov_drag_ws = ws;
    ov_press_rx = rx;
    ov_press_ry = ry;
    ov_ptr_x = rx - mons[overview_mon].x;
    ov_ptr_y = ry - mons[overview_mon].y;
    ov_moved = 0;
    if (ws >= 0 && ws != overview_sel) { overview_sel = ws; draw_overview(); }
}

static void overview_motion(int rx, int ry) {
    if (!ov_drag) return;
    ov_ptr_x = rx - mons[overview_mon].x;
    ov_ptr_y = ry - mons[overview_mon].y;
    if (!ov_moved) {
        int dx = rx - ov_press_rx, dy = ry - ov_press_ry;
        if (dx * dx + dy * dy > 36) ov_moved = 1;   /* >6px = a drag */
    }
    if (ov_moved) draw_overview();
}

/* move a leaf to another workspace on the overview's monitor */
static void overview_move_leaf(Node *leaf, int srcws, int dstws) {
    int mi = overview_mon;
    detach_from_ws(mi, srcws, leaf);
    attach_to_ws(mi, dstws, leaf);
    if (dstws == mons[mi].curws) showtree(leaf);   /* now visible */
    else hidetree(leaf);                            /* now hidden  */
    retile();
}

static void overview_release(int rx, int ry) {
    if (!ov_drag) return;
    int was_drag = ov_moved;
    Node *node = ov_drag_node;
    int srcws = ov_drag_ws;
    ov_drag = 0; ov_drag_node = NULL; ov_moved = 0;

    if (!was_drag) {
        /* a plain click enters the workspace under the pointer */
        int ws; overview_pick(rx, ry, &ws);
        if (ws >= 0) overview_choose(ws);
        return;
    }
    if (!node) { draw_overview(); return; }

    int dstws;
    Node *target = overview_pick(rx, ry, &dstws);
    if (dstws < 0 || dstws == srcws) {
        if (target && target != node && dstws == srcws)
            swap_leaf_payload(node, target);   /* reorder within a workspace */
        retile();
        draw_overview();
        return;
    }
    overview_move_leaf(node, srcws, dstws);
    draw_overview();
}

static void toggle_overview(void) {
    if (overview_active) close_overview();
    else open_overview();
}

static void screenshot(void) {
    const char *cmd =
        "mkdir -p \"$HOME/Pictures/Screenshots\" && "
        "if command -v maim >/dev/null 2>&1; then "
        "maim \"$HOME/Pictures/Screenshots/$(date +%Y-%m-%d-%H%M%S).png\"; "
        "elif command -v scrot >/dev/null 2>&1; then "
        "scrot \"$HOME/Pictures/Screenshots/%Y-%m-%d-%H%M%S.png\"; "
        "elif command -v import >/dev/null 2>&1; then "
        "import -window root \"$HOME/Pictures/Screenshots/$(date +%Y-%m-%d-%H%M%S).png\"; "
        "fi";
    spawn(cmd);
}

static void close_window(Window win) {
    Atom *protocols = NULL;
    int nprotocols = 0;
    int supports_delete = 0;

    if (!win) return;

    if (XGetWMProtocols(dpy, win, &protocols, &nprotocols)) {
        for (int i = 0; i < nprotocols; i++) {
            if (protocols[i] == atom_wm_delete_window) {
                supports_delete = 1;
                break;
            }
        }
        if (protocols) XFree(protocols);
    }

    if (supports_delete) {
        XEvent ev;
        memset(&ev, 0, sizeof ev);
        ev.xclient.type = ClientMessage;
        ev.xclient.window = win;
        ev.xclient.message_type = atom_wm_protocols;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = atom_wm_delete_window;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, win, False, NoEventMask, &ev);
        return;
    }

    XDestroyWindow(dpy, win);
}

static pid_t window_pid(Window win) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    pid_t pid = 0;
    if (XGetWindowProperty(dpy, win, atom_net_wm_pid, 0, 1, False,
            XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after,
            &data) == Success && data && nitems >= 1) {
        pid = (pid_t)*(unsigned long *)(void *)data;
    }
    if (data) XFree(data);
    return pid;
}

/* a window we must never touch: WM internals, the bar and any dock/panel */
static int window_is_protected(Window win) {
    if (win == root || win == wmcheckwin) return 1;
    for (int i = 0; i < nmons; i++)
        if (win == mons[i].barwin) return 1;
    if (dock_index(win) >= 0 || window_is_dock(win)) return 1;
    return 0;
}

/* :clear — hard "debloat" reset, like rebooting the laptop without
   leaving the session. Kills every client application across all
   monitors/workspaces, including ones that ignore WM_DELETE_WINDOW
   (Stremio & friends) or sit minimized in a tray, by signalling their
   process (_NET_WM_PID) and force-disconnecting the rest (XKillClient).
   The WM, the bar, docks/compositor and pure background services
   (audio, dbus, ...) own no app window here, so they survive. */
static void clear_all_windows(void) {
    pid_t pids[512];
    int npids = 0;
    pid_t self = getpid();

    Window *tree = NULL;
    Window d1, d2;
    unsigned int num = 0;

    /* enumerate every top-level window, not just the tiling tree, so we
       also catch tray-minimized and otherwise untracked client windows */
    if (!XQueryTree(dpy, root, &d1, &d2, &tree, &num)) return;

    for (unsigned int i = 0; i < num; i++) {
        Window win = tree[i];
        if (window_is_protected(win)) continue;

        XWindowAttributes wa;
        if (!XGetWindowAttributes(dpy, win, &wa)) continue;
        if (wa.override_redirect) continue;       /* menus, tooltips, etc. */
        if (wa.class != InputOutput) continue;

        pid_t pid = window_pid(win);
        if (pid > 0 && pid != self) {
            int seen = 0;
            for (int j = 0; j < npids; j++)
                if (pids[j] == pid) { seen = 1; break; }
            if (!seen && npids < (int)(sizeof pids / sizeof pids[0]))
                pids[npids++] = pid;
        } else {
            /* no usable PID: ask nicely, then sever the X connection */
            close_window(win);
            XKillClient(dpy, win);
        }
    }
    if (tree) XFree(tree);

    /* give processes a brief chance to exit cleanly, then make sure */
    for (int i = 0; i < npids; i++) kill(pids[i], SIGTERM);
    if (npids) {
        struct timespec ts = { 0, 300000000L };  /* 300 ms */
        nanosleep(&ts, NULL);
        for (int i = 0; i < npids; i++) kill(pids[i], SIGKILL);
    }
    XFlush(dpy);
}

static void spawn(const char *cmd) {
    if (fork() == 0) {
        setsid();
        close(ConnectionNumber(dpy));
        execlp("sh", "sh", "-c", cmd, NULL);
        _exit(0);
    }
}

static void start_command_prompt(char prefix) {
    cmdline[0] = prefix;
    cmdline[1] = 0;
    cmdline_len = 1;
    enter_mode(MODE_COMMAND);
}

static void shell_quote_single(char *dst, size_t dstsz, const char *src) {
    size_t pos = 0;
    if (!dstsz) return;
    dst[0] = 0;
    if (pos + 1 < dstsz) dst[pos++] = '\'';
    for (; *src && pos + 1 < dstsz; src++) {
        if (*src == '\'') {
            static const char repl[] = "'\\''";
            for (size_t i = 0; repl[i] && pos + 1 < dstsz; i++) dst[pos++] = repl[i];
        } else {
            dst[pos++] = *src;
        }
    }
    if (pos + 1 < dstsz) dst[pos++] = '\'';
    dst[pos] = 0;
}

static void spawn_in_terminal(const char *cmd) {
    char quoted[CMDLINE_MAX * 4];
    char full[CMDLINE_MAX * 4 + 256];

    if (!cmd || !*cmd) {
        spawn(term);
        return;
    }

    shell_quote_single(quoted, sizeof quoted, cmd);
    snprintf(full, sizeof full, "%s -e sh -lc %s", term, quoted);
    spawn(full);
}

static int parse_mods(const char *keys) {
    int mask = 0;
    if (strstr(keys, "mod")) mask |= Mod4Mask;
    if (strstr(keys, "shift")) mask |= ShiftMask;
    if (strstr(keys, "ctrl")) mask |= ControlMask;
    if (strstr(keys, "alt")) mask |= Mod1Mask;
    return mask;
}

static void reset_config_defaults(void) {
    gap = 8;
    bw = 2;
    barh = 24;
    barenabled = 1;
    externalbarh = 0;
    barpadx = 10;
    baritemgap = 6;
    bartextpad = 8;
    barwsminw = 20;
    cfocus = 0x5588ff;
    cnorm = 0x333333;
    barbg = 0x111111;
    barfg = 0xeeeeee;
    baraccentbg = 0x5588ff;
    baraccentfg = 0x111111;
    barmutedfg = 0xaaaaaa;
    copystr(term, sizeof term, "st");
    copystr(barfontname, sizeof barfontname, "9x15");
    copystr(barleftcfg, sizeof barleftcfg, "command,title,workspaces");
    copystr(barcentercfg, sizeof barcentercfg, "command");
    copystr(barrightcfg, sizeof barrightcfg, "clock");
    screen_off_minutes = 0;
    barpos = BAR_TOP;
    nmodbinds = 0;
    nnormalbinds = 0;
    ncmdbinds = 0;
    nrules = 0;
    nautostart = 0;
    modbind_limit = MAXBINDS;
    normalbind_limit = MAXMODEBINDS;
    cmd_limit = MAXCMDS;
    autostart_limit = MAXAUTOSTART;
}

static void updatenumlockmask(void) {
    XModifierKeymap *modmap;
    numlockmask = 0;
    modmap = XGetModifierMapping(dpy);
    if (!modmap) return;
    for (int mod = 0; mod < 8; mod++) {
        for (int k = 0; k < modmap->max_keypermod; k++) {
            KeyCode code = modmap->modifiermap[mod * modmap->max_keypermod + k];
            if (!code) continue;
            if (XkbKeycodeToKeysym(dpy, code, 0, 0) == XK_Num_Lock) {
                numlockmask = (1u << mod);
            }
        }
    }
    XFreeModifiermap(modmap);
}

static void upsert_modbind(unsigned int mod, KeyCode code, const char *action) {
    for (int i = 0; i < nmodbinds; i++) {
        if (modbinds[i].mod == mod && modbinds[i].code == code) {
            copystr(modbinds[i].action, sizeof modbinds[i].action, action);
            return;
        }
    }
    if (nmodbinds >= modbind_limit) return;
    modbinds[nmodbinds].mod = mod;
    modbinds[nmodbinds].code = code;
    copystr(modbinds[nmodbinds].action, sizeof modbinds[nmodbinds].action, action);
    nmodbinds++;
}

static void upsert_normalbind(KeySym sym, const char *action) {
    for (int i = 0; i < nnormalbinds; i++) {
        if (normalbinds[i].sym == sym) {
            copystr(normalbinds[i].action, sizeof normalbinds[i].action, action);
            return;
        }
    }
    if (nnormalbinds >= normalbind_limit) return;
    normalbinds[nnormalbinds].sym = sym;
    copystr(normalbinds[nnormalbinds].action, sizeof normalbinds[nnormalbinds].action, action);
    nnormalbinds++;
}

static void upsert_cmdbind(const char *name, const char *action) {
    for (int i = 0; i < ncmdbinds; i++) {
        if (!strcmp(cmdbinds[i].name, name)) {
            copystr(cmdbinds[i].action, sizeof cmdbinds[i].action, action);
            return;
        }
    }
    if (ncmdbinds >= cmd_limit) return;
    copystr(cmdbinds[ncmdbinds].name, sizeof cmdbinds[ncmdbinds].name, name);
    copystr(cmdbinds[ncmdbinds].action, sizeof cmdbinds[ncmdbinds].action, action);
    ncmdbinds++;
}

static void add_autostart_cmd(const char *cmd) {
    if (!cmd || !*cmd || nautostart >= autostart_limit) return;
    copystr(autostart_cmds[nautostart], sizeof autostart_cmds[nautostart], cmd);
    nautostart++;
}

static int parse_bind_config(const char *line) {
    char mode_name[64], keyexpr[128], action[256];

    if (sscanf(line, "bind_%63[^ ] = %127[^=]= %127[^\n]", mode_name, keyexpr, action) != 3) return 0;
    rtrim(mode_name);
    rtrim(keyexpr);
    rtrim(action);
    if (!strcasecmp(mode_name, "insert")) {
        char *kp = strrchr(keyexpr, '+');
        KeySym sym;
        KeyCode code;

        kp = kp ? kp + 1 : keyexpr;
        kp = ltrim(kp);
        if (!*kp) return 1;
        sym = parse_keysym_name(kp);
        if (sym == NoSymbol) return 1;
        code = XKeysymToKeycode(dpy, sym);
        if (code) upsert_modbind(parse_mods(keyexpr), code, action);
        return 1;
    }
    if (!strcasecmp(mode_name, "normal")) {
        KeySym sym = parse_keysym_name(keyexpr);
        if (sym != NoSymbol) upsert_normalbind(sym, action);
        return 1;
    }
    return 1;
}

static int parse_command_config(const char *line) {
    char cmd_name[64], cmd_action[256];

    if (sscanf(line, "command = %63[^=]= %127[^\n]", cmd_name, cmd_action) != 2) return 0;
    rtrim(cmd_name);
    rtrim(cmd_action);
    upsert_cmdbind(skip_command_prefix(cmd_name), cmd_action);
    return 1;
}

static int parse_rule_config(const char *line) {
    char rule_match[128], rule_action[128];
    Rule *r;
    char *kind, *value, *save;
    char *act;

    if (sscanf(line, "rule = %127[^=]= %127[^\n]", rule_match, rule_action) != 2) return 0;
    rtrim(rule_match);
    rtrim(rule_action);
    if (nrules >= MAXRULES) return 1;

    r = &rules[nrules];
    memset(r, 0, sizeof *r);
    r->workspace = -1;
    kind = ltrim(rule_match);
    value = strchr(kind, ':');
    if (!value) return 1;
    *value++ = 0;
    kind = ltrim(kind);
    value = ltrim(value);
    if (!strcasecmp(kind, "class")) copystr(r->class_name, sizeof r->class_name, value);
    else if (!strcasecmp(kind, "instance")) copystr(r->instance_name, sizeof r->instance_name, value);
    else if (!strcasecmp(kind, "title")) copystr(r->title, sizeof r->title, value);
    else return 1;

    for (act = strtok_r(rule_action, ",", &save); act; act = strtok_r(NULL, ",", &save)) {
        act = ltrim(act);
        rtrim(act);
        if (!strcasecmp(act, "float")) {
            r->set_floating = 1;
            r->floating = 1;
        } else if (!strcasecmp(act, "tile")) {
            r->set_floating = 1;
            r->floating = 0;
        } else if (!strcasecmp(act, "follow_class")) {
            r->follow_class = 1;
        } else if (!strncasecmp(act, "workspace:", 10)) {
            int ws = atoi(act + 10);
            if (ws >= 1 && ws <= MAXWS) r->workspace = ws - 1;
        }
    }
    nrules++;
    return 1;
}

static int parse_key_value(char *line, char *k, size_t ksz, char *v, size_t vsz) {
    char *eq = strchr(line, '=');
    if (!eq) return 0;
    *eq = 0;
    copystr(k, ksz, ltrim(line));
    rtrim(k);
    copystr(v, vsz, ltrim(eq + 1));
    rtrim(v);
    return 1;
}

static int apply_limit_config(const char *k, const char *v) {
    if (!strcmp(k, "insert_bind_limit")) modbind_limit = clampi(atoi(v), 1, MAXBINDS);
    else if (!strcmp(k, "normal_bind_limit")) normalbind_limit = clampi(atoi(v), 1, MAXMODEBINDS);
    else if (!strcmp(k, "command_limit")) cmd_limit = clampi(atoi(v), 1, MAXCMDS);
    else if (!strcmp(k, "autostart_limit")) autostart_limit = clampi(atoi(v), 1, MAXAUTOSTART);
    else return 0;
    return 1;
}

static int apply_scalar_config(const char *k, const char *v) {
    if (!strcmp(k, "gap")) gap = atoi(v);
    else if (!strcmp(k, "border")) bw = atoi(v);
    else if (!strcmp(k, "bar_height")) barh = clampi(atoi(v), 0, 256);
    else if (!strcmp(k, "bar_enabled")) barenabled = parse_bool_value(v, barenabled);
    else if (!strcmp(k, "external_bar_height")) {
        if (!strcasecmp(v, "auto")) externalbarh = -1;
        else externalbarh = clampi(atoi(v), 0, 256);
    }
    else if (!strcmp(k, "bar_padding_x")) barpadx = atoi(v);
    else if (!strcmp(k, "bar_item_gap")) baritemgap = atoi(v);
    else if (!strcmp(k, "bar_text_padding")) bartextpad = atoi(v);
    else if (!strcmp(k, "bar_workspace_min_width")) barwsminw = atoi(v);
    else if (!strcmp(k, "border_focus")) cfocus = hcol(v);
    else if (!strcmp(k, "border_normal")) cnorm = hcol(v);
    else if (!strcmp(k, "bar_bg")) barbg = hcol(v);
    else if (!strcmp(k, "bar_fg")) barfg = hcol(v);
    else if (!strcmp(k, "bar_accent_bg")) baraccentbg = hcol(v);
    else if (!strcmp(k, "bar_accent_fg")) baraccentfg = hcol(v);
    else if (!strcmp(k, "bar_muted_fg")) barmutedfg = hcol(v);
    else if (!strcmp(k, "bar_font")) copystr(barfontname, sizeof barfontname, v);
    else if (!strcmp(k, "bar_left")) copystr(barleftcfg, sizeof barleftcfg, v);
    else if (!strcmp(k, "bar_center")) copystr(barcentercfg, sizeof barcentercfg, v);
    else if (!strcmp(k, "bar_right")) copystr(barrightcfg, sizeof barrightcfg, v);
    else if (!strcmp(k, "screen_off_minutes")) screen_off_minutes = clampi(atoi(v), 0, 1440);
    else if (!strcmp(k, "bar_position")) {
        if (!strcasecmp(v, "bottom")) barpos = BAR_BOTTOM;
        else barpos = BAR_TOP;
    } else if (!strcmp(k, "terminal")) {
        copystr(term, sizeof term, v);
    } else {
        return 0;
    }
    return 1;
}

static void grab_mod_binds(void) {
    for (int i = 0; i < nmodbinds; i++) {
        XGrabKey(dpy, modbinds[i].code, modbinds[i].mod, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, modbinds[i].code, modbinds[i].mod | LockMask, root, True, GrabModeAsync, GrabModeAsync);
        if (numlockmask) {
            XGrabKey(dpy, modbinds[i].code, modbinds[i].mod | numlockmask, root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(dpy, modbinds[i].code, modbinds[i].mod | numlockmask | LockMask,
                root, True, GrabModeAsync, GrabModeAsync);
        }
    }
}

static void loadcfg_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512], k[64], v[448];
    while (fgets(line, sizeof line, f)) {
        char *p = ltrim(line);
        if (!*p || *p == '#') continue;
        if (parse_bind_config(p) || parse_command_config(p) || parse_rule_config(p)) continue;
        if (!parse_key_value(p, k, sizeof k, v, sizeof v)) continue;
        if (!strcmp(k, "autostart")) {
            add_autostart_cmd(v);
            continue;
        }
        if (apply_limit_config(k, v)) continue;
        apply_scalar_config(k, v);
    }
    fclose(f);
}

static void loadcfg(void) {
    char homecfg[PATH_MAX];
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(homecfg, sizeof homecfg, "%s/.config/nvwm/config.conf", home);
    } else {
        homecfg[0] = 0;
    }

    reset_config_defaults();
    loadcfg_file("/etc/nvwm/config.conf");
    loadcfg_file("./config.conf");
    if (homecfg[0]) loadcfg_file(homecfg);
}

static void run_autostart(void) {
    for (int i = 0; i < nautostart; i++) spawn(autostart_cmds[i]);
}

static void scandocks(void) {
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    if (!XQueryTree(dpy, root, &d1, &d2, &wins, &num)) return;
    for (i = 0; i < num; i++) {
        if (!XGetWindowAttributes(dpy, wins[i], &wa)) continue;
        if (wa.map_state != IsViewable) continue;
        if (window_is_dock(wins[i])) {
            add_dock_window(wins[i]);
            XSelectInput(dpy, wins[i], PropertyChangeMask);
        }
    }
    if (wins) XFree(wins);
}

static void apply_screen_off_config(void) {
    char cmd[128];

    if (screen_off_minutes <= 0) {
        spawn("xset s off -dpms >/dev/null 2>&1");
        return;
    }

    snprintf(cmd, sizeof cmd,
        "xset s off +dpms dpms 0 0 %d >/dev/null 2>&1",
        screen_off_minutes * 60);
    spawn(cmd);
}

static void reloadwm(void) {
    if (overview_active) close_overview();
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    loadcfg();
    apply_screen_off_config();
    updatenumlockmask();
    grab_mod_binds();
    ndockwins = 0;
    setupbars();
    scandocks();
    setupbars();
    retile();
    if (mon_focused(curmon)) setfocus(curmon, mon_focused(curmon), 0);
    else update_active_window();
}

static void set_net_wm_state(Window win, int fullscreen) {
    if (fullscreen) {
        Atom atoms[1] = { atom_net_wm_state_fullscreen };
        XChangeProperty(dpy, win, atom_net_wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)atoms, 1);
    } else {
        XDeleteProperty(dpy, win, atom_net_wm_state);
    }
}

static void handle_client_fullscreen(Window win, long action, Atom a1, Atom a2) {
    int mi;
    Node *n;
    int wants = 0;
    if (a1 != atom_net_wm_state_fullscreen && a2 != atom_net_wm_state_fullscreen) return;
    mi = monforwin(win);
    n = findleaf(mon_tree(mi), win);
    if (!n) return;
    if (action == 1) wants = 1;
    else if (action == 0) wants = 0;
    else wants = !n->real_fullscreen;
    n->real_fullscreen = wants;
    if (wants) {
        n->fullscreen = 0;
        n->floating = 0;
        setfocus(mi, n, 0);
    }
    set_net_wm_state(win, wants);
    retile();
}

static void sync_window_fullscreen(Window win) {
    int mi, ws;
    Node *n = findleaf_any(win, &mi, &ws);
    int wants;
    if (!n) return;
    wants = window_has_atom(win, atom_net_wm_state, atom_net_wm_state_fullscreen);
    if (n->real_fullscreen == wants) return;
    n->real_fullscreen = wants;
    if (wants) {
        n->fullscreen = 0;
        n->floating = 0;
        if (ws == mons[mi].curws) setfocus(mi, n, 0);
    }
    retile();
}

static void enter_mode(InputMode newmode) {
    if (newmode == MODE_INSERT) {
        mode = MODE_INSERT;
        cmdline[0] = 0;
        cmdline_len = 0;
        if (keyboard_grabbed) {
            XUngrabKeyboard(dpy, CurrentTime);
            keyboard_grabbed = 0;
        }
        if (mon_focused(curmon)) setfocus(curmon, mon_focused(curmon), 0);
    } else {
        if (!keyboard_grabbed) {
            if (XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) {
                keyboard_grabbed = 1;
            }
        }
        mode = newmode;
        if (newmode == MODE_NORMAL) {
            cmdline[0] = 0;
            cmdline_len = 0;
        } else if (!cmdline_len) {
            copystr(cmdline, sizeof cmdline, ":");
            cmdline_len = 1;
        }
    }
    drawbars();
}

static int apply_wm_action(const char *action) {
    Mon *m = &mons[curmon];
    if (!strncmp(action, "spawn:", 6)) {
        spawn(action + 6);
        return 1;
    }
    if (!strncmp(action, "wm:mode:", 8)) {
        const char *name = action + 8;
        if (!strcmp(name, "insert")) enter_mode(MODE_INSERT);
        else if (!strcmp(name, "normal")) enter_mode(MODE_NORMAL);
        else if (!strcmp(name, "command")) {
            copystr(cmdline, sizeof cmdline, ":");
            cmdline_len = 1;
            enter_mode(MODE_COMMAND);
        }
        return 1;
    }
    if (!strcmp(action, "wm:quit")) {
        running = 0;
        return 1;
    }
    if (!strcmp(action, "wm:kill")) {
        if (mon_focused(curmon)) close_window(mon_focused(curmon)->win);
        return 1;
    }
    if (!strcmp(action, "wm:clear")) {
        clear_all_windows();
        return 1;
    }
    if (!strcmp(action, "wm:reload")) {
        reloadwm();
        return 1;
    }
    if (!strcmp(action, "wm:focus_next")) {
        Node *n = nextleaf(curmon, mon_focused(curmon));
        if (n && n != mon_focused(curmon)) setfocus(curmon, n, 1);
        return 1;
    }
    if (!strcmp(action, "wm:focus_prev")) {
        Node *n = prevleaf(curmon, mon_focused(curmon));
        if (n && n != mon_focused(curmon)) setfocus(curmon, n, 1);
        return 1;
    }
    if (!strcmp(action, "wm:swap_next")) {
        Node *cur = mon_focused(curmon);
        Node *n = nextleaf(curmon, cur);
        if (cur && n && n != cur) {
            Window focused_win = cur->win;
            swap_leaf_payload(cur, n);
            retile();
            setfocus(curmon, findleaf(mon_tree(curmon), focused_win), 1);
        }
        return 1;
    }
    if (!strcmp(action, "wm:swap_prev")) {
        Node *cur = mon_focused(curmon);
        Node *n = prevleaf(curmon, cur);
        if (cur && n && n != cur) {
            Window focused_win = cur->win;
            swap_leaf_payload(cur, n);
            retile();
            setfocus(curmon, findleaf(mon_tree(curmon), focused_win), 1);
        }
        return 1;
    }
    if (!strcmp(action, "wm:swap_left")) return swap_in_direction("left");
    if (!strcmp(action, "wm:swap_right")) return swap_in_direction("right");
    if (!strcmp(action, "wm:swap_up")) return swap_in_direction("up");
    if (!strcmp(action, "wm:swap_down")) return swap_in_direction("down");
    if (!strcmp(action, "wm:focus_left")) {
        Node *n = find_directional_focus(curmon, mon_focused(curmon), "left");
        if (n) setfocus(curmon, n, 1);
        return 1;
    }
    if (!strcmp(action, "wm:focus_right")) {
        Node *n = find_directional_focus(curmon, mon_focused(curmon), "right");
        if (n) setfocus(curmon, n, 1);
        return 1;
    }
    if (!strcmp(action, "wm:focus_up")) {
        Node *n = find_directional_focus(curmon, mon_focused(curmon), "up");
        if (n) setfocus(curmon, n, 1);
        return 1;
    }
    if (!strcmp(action, "wm:focus_down")) {
        Node *n = find_directional_focus(curmon, mon_focused(curmon), "down");
        if (n) setfocus(curmon, n, 1);
        return 1;
    }
    if (!strcmp(action, "wm:overview")) {
        toggle_overview();
        return 1;
    }
    if (!strncmp(action, "wm:workspace:", 13)) {
        switch_workspace(atoi(action + 13) - 1);
        return 1;
    }
    if (!strcmp(action, "wm:workspace_prev")) {
        int mi = pointer_mon();
        switch_workspace((mons[mi].curws + MAXWS - 1) % MAXWS);
        return 1;
    }
    if (!strcmp(action, "wm:workspace_next")) {
        int mi = pointer_mon();
        switch_workspace((mons[mi].curws + 1) % MAXWS);
        return 1;
    }
    if (!strncmp(action, "wm:move_to_workspace:", 21)) {
        move_focused_to_workspace(atoi(action + 21) - 1);
        return 1;
    }
    if (!strncmp(action, "wm:move_to_workspace_follow:", 28)) {
        move_focused_to_workspace_and_follow(atoi(action + 28) - 1);
        return 1;
    }
    if (!strcmp(action, "wm:move_to_workspace_prev")) {
        move_focused_to_workspace_and_follow((mons[curmon].curws + MAXWS - 1) % MAXWS);
        return 1;
    }
    if (!strcmp(action, "wm:move_to_workspace_next")) {
        move_focused_to_workspace_and_follow((mons[curmon].curws + 1) % MAXWS);
        return 1;
    }
    if (!strcmp(action, "wm:toggle_float_centered")) {
        Node *f = mon_focused(curmon);
        if (!f) return 1;
        f->floating ^= 1;
        if (f->floating) {
            int fw = m->ww * 3 / 5;
            int fh = m->wh * 3 / 5;
            int fx = m->wx + (m->ww - fw) / 2;
            int fy = m->wy + (m->wh - fh) / 2;
            if (fw < 50) fw = 50;
            if (fh < 50) fh = 50;
            XMoveResizeWindow(dpy, f->win, fx, fy, fw - 2 * bw, fh - 2 * bw);
            XRaiseWindow(dpy, f->win);
        }
        retile();
        return 1;
    }
    if (!strcmp(action, "wm:toggle_float")) {
        Node *f = mon_focused(curmon);
        if (!f) return 1;
        f->floating ^= 1;
        if (f->floating) {
            Window dw;
            int wx, wy;
            unsigned ww, wh, bw_req, depth;
            if (XGetGeometry(dpy, f->win, &dw, &wx, &wy, &ww, &wh, &bw_req, &depth)) {
                int fw = (int)ww;
                int fh = (int)wh;
                if (fw < 50) fw = m->ww / 2;
                if (fh < 50) fh = m->wh / 2;
                XMoveResizeWindow(dpy, f->win, wx, wy, fw, fh);
            } else {
                int fw = m->ww / 2;
                int fh = m->wh / 2;
                int fx = m->wx + (m->ww - fw) / 2;
                int fy = m->wy + (m->wh - fh) / 2;
                XMoveResizeWindow(dpy, f->win, fx, fy, fw - 2 * bw, fh - 2 * bw);
            }
            XRaiseWindow(dpy, f->win);
        }
        retile();
        return 1;
    }
    if (!strcmp(action, "wm:toggle_fullscreen")) {
        if (!mon_focused(curmon)) return 1;
        mon_focused(curmon)->fullscreen ^= 1;
        if (mon_focused(curmon)->fullscreen) {
            mon_focused(curmon)->floating = 0;
            mon_focused(curmon)->real_fullscreen = 0;
        }
        retile();
        XRaiseWindow(dpy, mon_focused(curmon)->win);
        return 1;
    }
    if (!strcmp(action, "wm:toggle_real_fullscreen")) {
        Node *foc = mon_focused(curmon);
        if (!foc) return 1;
        foc->real_fullscreen ^= 1;
        if (foc->real_fullscreen) {
            foc->fullscreen = 0;
            foc->floating = 0;
        }
        set_net_wm_state(foc->win, foc->real_fullscreen);
        retile();
        XRaiseWindow(dpy, foc->win);
        return 1;
    }
    if (!strcmp(action, "wm:screenshot")) {
        screenshot();
        return 1;
    }
    if (!strcmp(action, "wm:toggle_split")) {
        if (mon_focused(curmon) && mon_focused(curmon)->par) {
            mon_focused(curmon)->par->horiz ^= 1;
            retile();
        }
        return 1;
    }
    if (!strncmp(action, "wm:ratio:", 9)) {
        if (mon_focused(curmon) && mon_focused(curmon)->par) {
            float delta = strtof(action + 9, NULL);
            mon_focused(curmon)->par->ratio += delta;
            if (mon_focused(curmon)->par->ratio < 0.1f) mon_focused(curmon)->par->ratio = 0.1f;
            if (mon_focused(curmon)->par->ratio > 0.9f) mon_focused(curmon)->par->ratio = 0.9f;
            retile();
        }
        return 1;
    }
    return 0;
}

static int run_command_line(void) {
    if (cmdline_len < 2 || (cmdline[0] != ':' && cmdline[0] != '/')) {
        enter_mode(MODE_NORMAL);
        return 0;
    }

    if (cmdline[0] == '/') {
        const char *cmd = cmdline + 1;
        if (*cmd) spawn(cmd);
        enter_mode(MODE_NORMAL);
        return 1;
    }

    {
        const char *name = cmdline + 1;
        if (!strncmp(name, "t ", 2)) {
            while (*name == 't' || *name == ' ') name++;
            spawn_in_terminal(name);
            enter_mode(MODE_NORMAL);
            return 1;
        }
        if (!strcmp(name, "t")) {
            spawn_in_terminal("");
            enter_mode(MODE_NORMAL);
            return 1;
        }
        for (int i = 0; i < ncmdbinds; i++) {
            if (!strcmp(name, cmdbinds[i].name)) {
                int keep = apply_wm_action(cmdbinds[i].action);
                if (mode == MODE_COMMAND) enter_mode(MODE_NORMAL);
                return keep;
            }
        }
        if (!strcmp(name, "w!")) {
            int keep = apply_wm_action("wm:reload");
            if (mode == MODE_COMMAND) enter_mode(MODE_NORMAL);
            return keep;
        }
        if (!strcmp(name, "q!")) {
            int keep = apply_wm_action("wm:quit");
            if (mode == MODE_COMMAND) enter_mode(MODE_NORMAL);
            return keep;
        }
    }
    enter_mode(MODE_NORMAL);
    return 0;
}

static void handle_command_key(XKeyEvent *kev, KeySym sym) {
    char text[32];
    int len = XLookupString(kev, text, sizeof text, NULL, NULL);

    if (sym == XK_Return || sym == XK_KP_Enter) {
        run_command_line();
        return;
    }
    if (sym == XK_Escape) {
        enter_mode(MODE_NORMAL);
        return;
    }
    if (sym == XK_BackSpace) {
        if (cmdline_len > 1) cmdline[--cmdline_len] = 0;
        drawbars();
        return;
    }
    if (len <= 0) return;
    for (int i = 0; i < len; i++) {
        if (cmdline_len < CMDLINE_MAX - 1 && text[i] >= 32 && text[i] < 127) {
            cmdline[cmdline_len++] = text[i];
            cmdline[cmdline_len] = 0;
        }
    }
    drawbars();
}

static int handle_normal_key(XKeyEvent *kev, KeySym sym) {
    char text[32];
    int len = XLookupString(kev, text, sizeof text, NULL, NULL);

    if (len > 0 && text[0] == ':') {
        start_command_prompt(':');
        return 1;
    }
    if (len > 0 && text[0] == '/') {
        start_command_prompt('/');
        return 1;
    }
    if (sym == XK_Escape) {
        enter_mode(MODE_INSERT);
        return 1;
    }
    if (sym == XK_Left || sym == XK_KP_Left) return apply_wm_action("wm:focus_prev");
    if (sym == XK_Right || sym == XK_KP_Right) return apply_wm_action("wm:focus_next");
    if (sym == XK_Up || sym == XK_KP_Up) return apply_wm_action("wm:focus_up");
    if (sym == XK_Down || sym == XK_KP_Down) return apply_wm_action("wm:focus_down");
    for (int i = 0; i < nnormalbinds; i++) {
        if (normalbinds[i].sym == sym) {
            return apply_wm_action(normalbinds[i].action);
        }
    }
    return 0;
}

static void destroybars(void) {
    for (int i = 0; i < nmons; i++) {
        if (mons[i].barpix) {
            XFreePixmap(dpy, mons[i].barpix);
            mons[i].barpix = 0;
        }
        if (mons[i].barwin) {
            XDestroyWindow(dpy, mons[i].barwin);
            mons[i].barwin = 0;
        }
    }
}

static void setupbars(void) {
    XGCValues gcv;
    XClassHint hint;
    Atom dock_type[1] = { atom_net_wm_window_type_dock };
    int top_reserved = 0, bottom_reserved = 0;

    /* internal bar */
    if (barenabled && barh > 0) {
        if (barpos == BAR_TOP) top_reserved = barh;
        else bottom_reserved = barh;
    } else if (externalbarh > 0) {
        if (barpos == BAR_TOP) top_reserved = externalbarh;
        else bottom_reserved = externalbarh;
    }

    /* external docks (polybar etc) — checked on both sides regardless of barpos */
    {
        int dt = 0, db = 0;
        compute_dock_struts(&dt, &db);
        if (dt > top_reserved) top_reserved = dt;
        if (db > bottom_reserved) bottom_reserved = db;
    }

    if (bargc) XFreeGC(dpy, bargc);
    if (barfont) {
        XFreeFont(dpy, barfont);
        barfont = NULL;
    }
    bargc = XCreateGC(dpy, root, 0, &gcv);
    if (barenabled) {
        barfont = XLoadQueryFont(dpy, barfontname);
        if (!barfont) barfont = XLoadQueryFont(dpy, "9x15");
        if (!barfont) barfont = XLoadQueryFont(dpy, "fixed");
        if (barfont) XSetFont(dpy, bargc, barfont->fid);
    }

    for (int i = 0; i < nmons; i++) {
        Mon *m = &mons[i];
        if (m->barpix) { XFreePixmap(dpy, m->barpix); m->barpix = 0; }
        m->wx = m->x;
        m->ww = m->w;
        m->wy = m->y + top_reserved;
        m->wh = m->h - top_reserved - bottom_reserved;
        if (m->wh < 1) m->wh = 1;

        if (!barenabled || barh <= 0) {
            if (m->barwin) {
                XDestroyWindow(dpy, m->barwin);
                m->barwin = 0;
            }
            continue;
        }

        if (barpos == BAR_TOP) {
            if (!m->barwin) m->barwin = XCreateSimpleWindow(dpy, root, m->x, m->y, m->w, barh, 0, barbg, barbg);
            XMoveResizeWindow(dpy, m->barwin, m->x, m->y, m->w, barh);
        } else {
            if (!m->barwin) m->barwin = XCreateSimpleWindow(dpy, root, m->x, m->y + m->h - barh, m->w, barh, 0, barbg, barbg);
            XMoveResizeWindow(dpy, m->barwin, m->x, m->y + m->h - barh, m->w, barh);
        }
        XSetWindowBackground(dpy, m->barwin, barbg);
        XChangeProperty(dpy, m->barwin, atom_net_wm_window_type, XA_ATOM, 32,
            PropModeReplace, (unsigned char *)dock_type, 1);
        XStoreName(dpy, m->barwin, "nvwm-bar");
        hint.res_name = "nvwm-bar";
        hint.res_class = "NVWMBar";
        XSetClassHint(dpy, m->barwin, &hint);
        XDefineCursor(dpy, m->barwin, normalcursor);
        XSelectInput(dpy, m->barwin, ExposureMask);
        XMapRaised(dpy, m->barwin);
    }
    if (barenabled) drawbars();
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);
    dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;
    XSetErrorHandler(xerr);

    int scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    sw = DisplayWidth(dpy, scr);
    sh = DisplayHeight(dpy, scr);
    normalcursor = XCreateFontCursor(dpy, XC_left_ptr);
    movecursor = XCreateFontCursor(dpy, XC_fleur);
    resizecursor = XCreateFontCursor(dpy, XC_bottom_right_corner);
    XDefineCursor(dpy, root, normalcursor);
    init_composite();
    initatoms();
    setup_wm_check();
    update_supported_atoms();
    update_number_of_desktops();
    update_current_desktop();
    update_active_window();

    nmons = querygeom(mons, MAXMONS);

    /* RandR: listen for monitor hotplug / resolution changes */
    int rrerr;
    if (XRRQueryExtension(dpy, &randr_event_base, &rrerr)) {
        randr_active = 1;
        XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
    }

    XSelectInput(dpy, root, SubstructureNotifyMask | SubstructureRedirectMask | KeyPressMask);

    loadcfg();
    apply_screen_off_config();
    updatenumlockmask();
    grab_mod_binds();
    setupbars();
    scandocks();
    setupbars();
    run_autostart();

    XGrabButton(dpy, Button1, MOD, root, False,
        ButtonPressMask | ButtonReleaseMask,
        GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button3, MOD, root, False,
        ButtonPressMask | ButtonReleaseMask,
        GrabModeAsync, GrabModeAsync, None, None);

    /* scroll wheel: MOD = switch workspace, MOD+Ctrl = move focused window
       to prev/next workspace and follow it */
    {
        unsigned int locks[] = { 0, LockMask, numlockmask, numlockmask | LockMask };
        unsigned int scrollmods[] = { MOD, MOD | ControlMask };
        int scrollbtns[] = { Button4, Button5 };
        for (int b = 0; b < 2; b++)
            for (int m = 0; m < 2; m++)
                for (int l = 0; l < 4; l++)
                    XGrabButton(dpy, scrollbtns[b], scrollmods[m] | locks[l], root,
                        False, ButtonPressMask, GrabModeAsync, GrabModeAsync,
                        None, None);
    }

    while (running) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (randr_active && ev.type == randr_event_base + RRScreenChangeNotify) {
                XRRUpdateConfiguration(&ev);
                if (updategeom()) {
                    setupbars();
                    retile();
                    if (mon_focused(curmon)) setfocus(curmon, mon_focused(curmon), 0);
                }
                continue;
            }
            switch (ev.type) {
        case Expose:
            if (ev.xexpose.count != 0) break;
            if (overview_active && ev.xexpose.window == overview_win) {
                draw_overview();
                break;
            }
            for (int i = 0; i < nmons; i++) {
                if (ev.xexpose.window == mons[i].barwin) {
                    drawbar(i);
                    break;
                }
            }
            break;

        case MapRequest:
            XSelectInput(dpy, ev.xmaprequest.window, EnterWindowMask | PropertyChangeMask);
            XMapWindow(dpy, ev.xmaprequest.window);
            if (window_is_dock(ev.xmaprequest.window)) {
                add_dock_window(ev.xmaprequest.window);
                setupbars();
                retile();
                sync_dock_stack();
                break;
            }
            {
                Window dw;
                int rx, ry, wx, wy;
                unsigned msk;
                Node *n;
                int existing_mi = 0, existing_ws = 0;
                Node *existing = findleaf_any(ev.xmaprequest.window, &existing_mi, &existing_ws);
                if (existing) {
                    retile();
                    if (existing_ws == mons[existing_mi].curws) setfocus(existing_mi, existing, 1);
                    break;
                }
                XQueryPointer(dpy, root, &dw, &dw, &rx, &ry, &wx, &wy, &msk);
                int mi = monforpt(rx, ry);
                int targetws = mons[mi].curws;
                n = mkleaf(ev.xmaprequest.window);
                int follow = 0;
                apply_rules(n, &targetws, &follow);
                if (window_has_atom(ev.xmaprequest.window, atom_net_wm_state,
                        atom_net_wm_state_fullscreen)) {
                    n->real_fullscreen = 1;
                }
                attach_to_ws(mi, targetws, n);
                if (targetws != mons[mi].curws) {
                    if (follow) {
                        switch_workspace_on(mi, targetws);
                    } else {
                        unmap_managed(n);
                        retile();
                    }
                } else {
                    retile();
                    Node *fs = find_fullscreen_leaf(mon_tree(mi));
                    setfocus(mi, fs ? fs : n, fs ? 0 : 1);
                }
            }
            break;

        case MapNotify:
            if (ev.xmap.override_redirect && window_is_dock(ev.xmap.window)) {
                add_dock_window(ev.xmap.window);
                XSelectInput(dpy, ev.xmap.window, PropertyChangeMask);
                setupbars();
                retile();
                sync_dock_stack();
            }
            break;

        case DestroyNotify:
            remove_dock_window(ev.xdestroywindow.window);
            removewin(ev.xdestroywindow.window);
            retile();
            if (mon_focused(curmon)) setfocus(curmon, mon_focused(curmon), 0);
            break;

        case UnmapNotify:
            {
                int mi = 0, ws = 0;
                Node *n = findleaf_any(ev.xunmap.window, &mi, &ws);
                if (!n) remove_dock_window(ev.xunmap.window);
                if (n) {
                    if (n->ignore_unmap > 0) {
                        n->ignore_unmap--;
                    } else {
                        removewin(ev.xunmap.window);
                        retile();
                        if (mon_focused(curmon)) setfocus(curmon, mon_focused(curmon), 0);
                    }
                } else if (ev.xunmap.send_event) {
                    removewin(ev.xunmap.window);
                    retile();
                    if (mon_focused(curmon)) setfocus(curmon, mon_focused(curmon), 0);
                }
            }
            break;

        case PropertyNotify:
            if ((ev.xproperty.atom == atom_net_wm_strut ||
                    ev.xproperty.atom == atom_net_wm_strut_partial) &&
                    dock_index(ev.xproperty.window) >= 0) {
                setupbars();
                retile();
                break;
            }
            if (ev.xproperty.atom == atom_net_wm_window_type &&
                    window_is_dock(ev.xproperty.window)) {
                int mi = 0, ws = 0;
                Node *n = findleaf_any(ev.xproperty.window, &mi, &ws);
                add_dock_window(ev.xproperty.window);
                if (n) {
                    detach_from_ws(mi, ws, n);
                    free(n);
                }
                setupbars();
                retile();
                break;
            }
            if (ev.xproperty.atom == atom_net_wm_state) {
                sync_window_fullscreen(ev.xproperty.window);
            }
            if (ev.xproperty.atom == atom_net_wm_name || ev.xproperty.atom == XA_WM_NAME) {
                Node *focused = mon_focused(curmon);
                if (focused && focused->win == ev.xproperty.window)
                    refresh_cached_title();
            }
            break;

        case ClientMessage:
            if (ev.xclient.message_type == atom_net_current_desktop) {
                switch_workspace((int)ev.xclient.data.l[0]);
                break;
            }
            if (ev.xclient.message_type == atom_net_active_window) {
                int mi = 0, ws = 0;
                Node *n = findleaf_any(ev.xclient.window, &mi, &ws);
                if (n) {
                    if (ws != mons[mi].curws) switch_workspace_on(mi, ws);
                    setfocus(mi, n, 1);
                }
                break;
            }
            if (ev.xclient.message_type == atom_net_wm_state) {
                handle_client_fullscreen(ev.xclient.window, ev.xclient.data.l[0],
                    (Atom)ev.xclient.data.l[1], (Atom)ev.xclient.data.l[2]);
                break;
            }
            break;

        case ConfigureRequest: {
            int mi = 0, ws = 0;
            Node *n = findleaf_any(ev.xconfigurerequest.window, &mi, &ws);
            if (n && ws == mons[mi].curws && !n->floating && !n->fullscreen && !n->real_fullscreen) {
                retile();
                break;
            }
            XWindowChanges wc = {
                .x = ev.xconfigurerequest.x,
                .y = ev.xconfigurerequest.y,
                .width = ev.xconfigurerequest.width,
                .height = ev.xconfigurerequest.height,
                .border_width = n ? bw : ev.xconfigurerequest.border_width,
                .sibling = ev.xconfigurerequest.above,
                .stack_mode = ev.xconfigurerequest.detail,
            };
            XConfigureWindow(dpy, ev.xconfigurerequest.window,
                ev.xconfigurerequest.value_mask, &wc);
            if (dock_index(ev.xconfigurerequest.window) >= 0) {
                setupbars();
                retile();
            }
            break;
        }

        case EnterNotify:
            if (mode != MODE_INSERT) break;
            if (ev.xcrossing.mode == NotifyNormal) {
                int mi = monforwin(ev.xcrossing.window);
                Node *n = findleaf(mon_tree(mi), ev.xcrossing.window);
                if (n && n != mon_focused(mi)) setfocus(mi, n, 0);
            }
            break;

        case ButtonPress: {
            if (overview_active) {
                if (ev.xbutton.button == Button1)
                    overview_press(ev.xbutton.x_root, ev.xbutton.y_root);
                else
                    close_overview();
                break;
            }
            if (ev.xbutton.button == Button4 || ev.xbutton.button == Button5) {
                unsigned int st = ev.xbutton.state &
                    (ShiftMask | ControlMask | Mod1Mask | Mod4Mask | LockMask | numlockmask);
                st &= ~(LockMask | numlockmask);
                /* wheel up = next workspace, wheel down = previous;
                   acts on the monitor under the pointer */
                int pmi = monforpt(ev.xbutton.x_root, ev.xbutton.y_root);
                int next = (ev.xbutton.button == Button4);
                int targetws = next ? (mons[pmi].curws + 1) % MAXWS
                                    : (mons[pmi].curws + MAXWS - 1) % MAXWS;
                if (st == MOD)
                    switch_workspace_on(pmi, targetws);
                else if (st == (MOD | ControlMask))
                    move_focused_to_workspace_and_follow(targetws);
                break;
            }

            Window clicked = ev.xbutton.subwindow ? ev.xbutton.subwindow : ev.xbutton.window;
            if (clicked == root) break;

            int mi = monforpt(ev.xbutton.x_root, ev.xbutton.y_root);
            Node *n = NULL;
            for (int i = 0; i < nmons && !n; i++) {
                if ((n = findleaf(mon_tree(i), clicked))) mi = i;
            }
            if (!n || (ev.xbutton.button != Button1 && ev.xbutton.button != Button3)) break;

            int is_btn1 = (ev.xbutton.button == Button1);

            if (n != mon_focused(mi)) setfocus(mi, n, 1);
            else {
                XRaiseWindow(dpy, n->win);
                raise_floating(mon_tree(mi));
            }

            {
                Window dw;
                unsigned gw, gh, gb, gd;
                XGetGeometry(dpy, n->win, &dw, &drag_wx, &drag_wy, &gw, &gh, &gb, &gd);
                drag_ww = (int)gw;
                drag_wh = (int)gh;
            }
            drag_ox = ev.xbutton.x_root;
            drag_oy = ev.xbutton.y_root;
            drag_node = n;
            drag_mon = mi;
            /* Super+Button1 on a tiled window reorders (swaps) it in the
               direction you drag instead of detaching it to floating */
            if (is_btn1 && !n->real_fullscreen && !n->floating)
                drag_mode = 3;
            else if (is_btn1)
                drag_mode = 1;   /* floating: move */
            else
                drag_mode = 2;   /* Button3: resize floating / split ratio */

            XGrabPointer(dpy, root, False, PointerMotionMask | ButtonReleaseMask,
                GrabModeAsync, GrabModeAsync, None,
                drag_mode == 2 ? resizecursor : movecursor, CurrentTime);
            break;
        }

        case ButtonRelease:
            if (overview_active) {
                if (ev.xbutton.button == Button1)
                    overview_release(ev.xbutton.x_root, ev.xbutton.y_root);
                break;
            }
            if (drag_mode) {
                XUngrabPointer(dpy, CurrentTime);
                if (drag_node) {
                    Window dw;
                    int rx, ry, wx2, wy2;
                    unsigned msk;
                    XQueryPointer(dpy, root, &dw, &dw, &rx, &ry, &wx2, &wy2, &msk);
                    int newmon = monforpt(rx, ry);
                    if (newmon != drag_mon) {
                        int was_floating = drag_node->floating;
                        detach(drag_mon, drag_node);
                        attach(newmon, drag_node);
                        drag_node->floating = was_floating;
                        curmon = newmon;
                        retile();
                    }
                }
            }
            drag_mode = 0;
            drag_node = NULL;
            drag_ww = drag_wh = 0;
            break;

        case MotionNotify:
            {
                XEvent newer;
                while (XCheckTypedEvent(dpy, MotionNotify, &newer)) ev = newer;
            }
            if (overview_active) {
                overview_motion(ev.xmotion.x_root, ev.xmotion.y_root);
                break;
            }
            if (!drag_mode || !drag_node) break;
            {
                int dx = ev.xmotion.x_root - drag_ox;
                int dy = ev.xmotion.y_root - drag_oy;
                if (drag_mode == 3) {
                    /* swap with whatever tiled window we are hovering, then
                       follow our window into its new slot so the drag keeps
                       pushing it in the direction of travel */
                    Node *t = tiled_leaf_at(drag_mon,
                        ev.xmotion.x_root, ev.xmotion.y_root);
                    if (t && t != drag_node && !t->floating) {
                        swap_leaf_payload(drag_node, t);
                        retile();
                        drag_node = t;
                        setfocus(drag_mon, drag_node, 0);
                    }
                } else if (drag_mode == 1) {
                    int nx = drag_wx + dx;
                    int ny = drag_wy + dy;
                    Mon *dm = &mons[drag_mon];
                    int top_off = dm->wy - dm->y;
                    int bot_off = dm->h - (dm->wy - dm->y) - dm->wh;
                    if (nx < 0) nx = 0;
                    if (ny < top_off) ny = top_off;
                    if (nx + drag_ww + 2 * bw > sw) nx = sw - drag_ww - 2 * bw;
                    if (ny + drag_wh + 2 * bw > sh - bot_off) ny = sh - bot_off - drag_wh - 2 * bw;
                    XMoveWindow(dpy, drag_node->win, nx, ny);
                } else if (drag_node->floating) {
                    int nw = drag_ww + dx;
                    int nh = drag_wh + dy;
                    if (nw < 50) nw = 50;
                    if (nh < 50) nh = 50;
                    if (drag_wx + nw + 2 * bw > sw) nw = sw - drag_wx - 2 * bw;
                    if (drag_wy + nh + 2 * bw > sh) nh = sh - drag_wy - 2 * bw;
                    XResizeWindow(dpy, drag_node->win, nw, nh);
                } else if (drag_node->par) {
                    Node *p = drag_node->par;
                    int sign = (p->a == drag_node) ? 1 : -1;
                    float delta;
                    if (p->horiz)
                        delta = p->w > 0 ? (float)dx / p->w * sign : 0.0f;
                    else
                        delta = p->h > 0 ? (float)dy / p->h * sign : 0.0f;
                    p->ratio += delta;
                    if (p->ratio < 0.1f) p->ratio = 0.1f;
                    if (p->ratio > 0.9f) p->ratio = 0.9f;
                    retile();
                    drag_ox = ev.xmotion.x_root;
                    drag_oy = ev.xmotion.y_root;
                }
            }
            break;

        case KeyPress: {
            unsigned int state = ev.xkey.state &
                (ShiftMask | ControlMask | Mod1Mask | Mod4Mask | LockMask | numlockmask);
            state &= ~(LockMask | numlockmask);
            KeySym sym = XLookupKeysym(&ev.xkey, 0);

            if (overview_active) {
                overview_key(sym);
                break;
            }

            if (state == (ControlMask | Mod4Mask) && (sym == XK_Left || sym == XK_KP_Left)) {
                apply_wm_action("wm:swap_left");
                break;
            }
            if (state == (ControlMask | Mod4Mask) && (sym == XK_Right || sym == XK_KP_Right)) {
                apply_wm_action("wm:swap_right");
                break;
            }
            if (state == (ControlMask | Mod4Mask) && (sym == XK_Up || sym == XK_KP_Up)) {
                apply_wm_action("wm:swap_up");
                break;
            }
            if (state == (ControlMask | Mod4Mask) && (sym == XK_Down || sym == XK_KP_Down)) {
                apply_wm_action("wm:swap_down");
                break;
            }
            if (state == 0 && (sym == XK_Print || sym == XK_Sys_Req)) {
                apply_wm_action("wm:screenshot");
                break;
            }
            if (state == MOD && (sym == XK_Left || sym == XK_KP_Left)) {
                apply_wm_action("wm:focus_prev");
                break;
            }
            if (state == MOD && (sym == XK_Right || sym == XK_KP_Right)) {
                apply_wm_action("wm:focus_next");
                break;
            }
            if (state == MOD && (sym == XK_Up || sym == XK_KP_Up)) {
                apply_wm_action("wm:focus_up");
                break;
            }
            if (state == MOD && (sym == XK_Down || sym == XK_KP_Down)) {
                apply_wm_action("wm:focus_down");
                break;
            }

            int handled = 0;
            for (int i = 0; i < nmodbinds; i++) {
                if (ev.xkey.keycode == modbinds[i].code && state == modbinds[i].mod) {
                    handled = apply_wm_action(modbinds[i].action);
                    break;
                }
            }
            if (handled) break;

            /* built-in fallback so Super+Z opens the overview even without a
               config bind; a user bind for this key takes priority above */
            if (state == MOD && (sym == XK_z || sym == XK_Z)) {
                apply_wm_action("wm:overview");
                break;
            }

            if (mode == MODE_COMMAND) {
                handle_command_key(&ev.xkey, sym);
                break;
            }
            if (mode == MODE_NORMAL) {
                handle_normal_key(&ev.xkey, sym);
            }
            break;
        }
            }
        }
        drawbars_if_clock_changed();
        if (!running) break;
        if (!XPending(dpy)) wait_for_x_event_or_clock_tick();
    }

    if (keyboard_grabbed) XUngrabKeyboard(dpy, CurrentTime);
    for (int i = 0; i < nmons; i++) {
        for (int ws = 0; ws < MAXWS; ws++) {
            free_tree(mons[i].tree[ws]);
            mons[i].tree[ws] = NULL;
            mons[i].focused[ws] = NULL;
        }
    }
    destroybars();
    if (wmcheckwin) {
        XDestroyWindow(dpy, wmcheckwin);
        wmcheckwin = 0;
    }
    if (normalcursor) XFreeCursor(dpy, normalcursor);
    if (movecursor) XFreeCursor(dpy, movecursor);
    if (resizecursor) XFreeCursor(dpy, resizecursor);
    XCloseDisplay(dpy);
    return 0;
}
