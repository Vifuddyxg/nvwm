/* Standalone deterministic test for nvwm's directional window picker.
 *
 * pick_directional() / interval_overlap() below are kept byte-for-byte
 * identical to the copies in ../nvwm.c. This exercises the "go left/right/
 * up/down" mechanic over realistic BSP layouts without needing X.
 *
 * Build & run:  cc -std=c99 -Wall -Wextra -o /tmp/dirtest tests/dirtest.c && /tmp/dirtest
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* minimal stand-in for nvwm's Node: only the fields the picker reads */
typedef struct Node {
    int leaf, floating;
    int x, y, w, h;
    const char *name; /* test-only label */
} Node;

enum { DIR_LEFT = 0, DIR_RIGHT, DIR_UP, DIR_DOWN };

/* ---- BEGIN copy from nvwm.c (must stay identical) ---- */
static int interval_overlap(int a1, int a2, int b1, int b2) {
    int lo = a1 > b1 ? a1 : b1;
    int hi = a2 < b2 ? a2 : b2;
    return hi > lo ? hi - lo : 0;
}

static Node *pick_directional(Node **leaves, int count, Node *from, int d) {
    Node *best = NULL;     /* nearest with perpendicular overlap */
    Node *fallback = NULL; /* nearest of any, even diagonal */
    long bestscore = 0, fbscore = 0;
    int fx1, fy1, fx2, fy2, fcx, fcy;

    if (!from) return NULL;
    fx1 = from->x;
    fy1 = from->y;
    fx2 = from->x + from->w;
    fy2 = from->y + from->h;
    fcx = from->x + from->w / 2;
    fcy = from->y + from->h / 2;

    for (int i = 0; i < count; i++) {
        Node *n = leaves[i];
        int nx1, ny1, nx2, ny2, ncx, ncy;
        int primary, secondary, overlap;
        long score;
        if (n == from || n->floating) continue;
        nx1 = n->x;
        ny1 = n->y;
        nx2 = n->x + n->w;
        ny2 = n->y + n->h;
        ncx = n->x + n->w / 2;
        ncy = n->y + n->h / 2;

        if (d == DIR_LEFT) {
            primary = fx1 - nx2;
            overlap = interval_overlap(fy1, fy2, ny1, ny2);
            secondary = abs(fcy - ncy);
        } else if (d == DIR_RIGHT) {
            primary = nx1 - fx2;
            overlap = interval_overlap(fy1, fy2, ny1, ny2);
            secondary = abs(fcy - ncy);
        } else if (d == DIR_UP) {
            primary = fy1 - ny2;
            overlap = interval_overlap(fx1, fx2, nx1, nx2);
            secondary = abs(fcx - ncx);
        } else {
            primary = ny1 - fy2;
            overlap = interval_overlap(fx1, fx2, nx1, nx2);
            secondary = abs(fcx - ncx);
        }

        if (primary < 0) continue;
        score = (long)primary * 100000L + (long)secondary;
        if (overlap > 0) {
            if (!best || score < bestscore) { best = n; bestscore = score; }
        }
        if (!fallback || score < fbscore) { fallback = n; fbscore = score; }
    }
    return best ? best : fallback;
}
/* ---- END copy from nvwm.c ---- */

static int failures = 0;
static int checks = 0;

static const char *dirname(int d) {
    return d == DIR_LEFT ? "left" : d == DIR_RIGHT ? "right"
         : d == DIR_UP ? "up" : "down";
}

/* assert that moving `d` from `from` over `leaves` lands on `expect` (NULL ok) */
static void expect(const char *layout, Node **leaves, int count,
                   Node *from, int d, Node *want) {
    Node *got = pick_directional(leaves, count, from, d);
    checks++;
    const char *gn = got ? got->name : "NULL";
    const char *wn = want ? want->name : "NULL";
    if (got != want) {
        failures++;
        printf("  FAIL [%s] %s %-5s -> got %s, want %s\n",
               layout, from->name, dirname(d), gn, wn);
    } else {
        printf("  ok   [%s] %s %-5s -> %s\n", layout, from->name, dirname(d), gn);
    }
}

static Node mk(const char *name, int x, int y, int w, int h) {
    Node n; memset(&n, 0, sizeof n);
    n.leaf = 1; n.floating = 0; n.x = x; n.y = y; n.w = w; n.h = h; n.name = name;
    return n;
}

int main(void) {
    const int W = 1920, H = 1080, HW = 960, HH = 540;

    /* A: two side-by-side columns */
    {
        Node a = mk("A0", 0, 0, HW, H), b = mk("A1", HW, 0, HW, H);
        Node *L[] = { &a, &b };
        printf("Layout A: two columns\n");
        expect("A", L, 2, &b, DIR_LEFT,  &a);
        expect("A", L, 2, &a, DIR_RIGHT, &b);
        expect("A", L, 2, &a, DIR_LEFT,  NULL);
        expect("A", L, 2, &b, DIR_RIGHT, NULL);
        expect("A", L, 2, &a, DIR_UP,    NULL);
        expect("A", L, 2, &a, DIR_DOWN,  NULL);
    }

    /* B: master left (full height) + stacked right column */
    {
        Node m  = mk("B0", 0,  0,  HW, H);
        Node r1 = mk("B1", HW, 0,  HW, HH);
        Node r2 = mk("B2", HW, HH, HW, HH);
        Node *L[] = { &m, &r1, &r2 };
        printf("Layout B: master + stack\n");
        expect("B", L, 3, &m,  DIR_RIGHT, &r1); /* tie -> first (top) */
        expect("B", L, 3, &r1, DIR_LEFT,  &m);
        expect("B", L, 3, &r2, DIR_LEFT,  &m);
        expect("B", L, 3, &r1, DIR_DOWN,  &r2);
        expect("B", L, 3, &r2, DIR_UP,    &r1);
        expect("B", L, 3, &r1, DIR_RIGHT, NULL);
        expect("B", L, 3, &m,  DIR_LEFT,  NULL);
    }

    /* C: 2x2 grid */
    {
        Node tl = mk("C-TL", 0,  0,  HW, HH);
        Node tr = mk("C-TR", HW, 0,  HW, HH);
        Node bl = mk("C-BL", 0,  HH, HW, HH);
        Node br = mk("C-BR", HW, HH, HW, HH);
        Node *L[] = { &tl, &tr, &bl, &br };
        printf("Layout C: 2x2 grid\n");
        expect("C", L, 4, &tl, DIR_RIGHT, &tr);
        expect("C", L, 4, &tl, DIR_DOWN,  &bl);
        expect("C", L, 4, &tl, DIR_LEFT,  NULL);
        expect("C", L, 4, &tl, DIR_UP,    NULL);
        expect("C", L, 4, &br, DIR_LEFT,  &bl);
        expect("C", L, 4, &br, DIR_UP,    &tr);
        expect("C", L, 4, &tr, DIR_LEFT,  &tl);
        expect("C", L, 4, &tr, DIR_DOWN,  &br);
        expect("C", L, 4, &bl, DIR_RIGHT, &br);
        expect("C", L, 4, &bl, DIR_UP,    &tl);
    }

    /* D: only two diagonal windows (no perpendicular overlap -> fallback) */
    {
        Node tl = mk("D-TL", 0,  0,  HW, HH);
        Node br = mk("D-BR", HW, HH, HW, HH);
        Node *L[] = { &tl, &br };
        printf("Layout D: diagonal pair (fallback)\n");
        expect("D", L, 2, &tl, DIR_RIGHT, &br);
        expect("D", L, 2, &tl, DIR_DOWN,  &br);
        expect("D", L, 2, &br, DIR_LEFT,  &tl);
        expect("D", L, 2, &br, DIR_UP,    &tl);
    }

    /* E: floating window must be ignored as a target */
    {
        Node a = mk("E0", 0, 0, HW, H), b = mk("E1", HW, 0, HW, H);
        Node f = mk("E-float", HW/2, H/4, HW, HH); f.floating = 1;
        Node *L[] = { &a, &f, &b };
        printf("Layout E: floating ignored\n");
        expect("E", L, 3, &b, DIR_LEFT,  &a);
        expect("E", L, 3, &a, DIR_RIGHT, &b);
    }

    /* F: three columns -> left/right hit the IMMEDIATE neighbour, not the far one */
    {
        Node c0 = mk("F0", 0,   0, 640, H);
        Node c1 = mk("F1", 640, 0, 640, H);
        Node c2 = mk("F2", 1280,0, 640, H);
        Node *L[] = { &c0, &c1, &c2 };
        printf("Layout F: three columns\n");
        expect("F", L, 3, &c0, DIR_RIGHT, &c1);
        expect("F", L, 3, &c2, DIR_LEFT,  &c1);
        expect("F", L, 3, &c1, DIR_LEFT,  &c0);
        expect("F", L, 3, &c1, DIR_RIGHT, &c2);
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    (void)W;
    return failures ? 1 : 0;
}
