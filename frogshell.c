#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <hcuapi/dis.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

extern unsigned char fontdata8x8[64 * 16];

#define ROOT "/mnt/sdcard"
#define DEVICE_FILE "/tmp/tfdevice.env"
#define THEME_FILE "/mnt/sdcard/cubegm/skin/skin.txt"
#define KEYMAP_FILE "/mnt/sdcard/frogui/keymap.txt"
#define MAX_PATH 1024
#define MAX_ENTRIES 512
#define MAX_MARKED 128

enum { BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_A, BTN_B, BTN_L1, BTN_R1,
       BTN_X, BTN_Y, BTN_SELECT, BTN_START, BTN_COUNT };
static int key_bits[BTN_COUNT] = { 7, 5, 2, 3, 13, 14, 10, 11, 12, 15, 0, 1 };
static const char *key_names[BTN_COUNT] = {
    "LEFT", "RIGHT", "UP", "DOWN", "A", "B", "L1", "R1", "X", "Y", "SELECT", "START"
};

typedef struct { int fd, w, h, pitch, bytespp; size_t len; unsigned char *mem; struct fb_var_screeninfo vi; uint32_t *canvas; } Screen;
typedef struct { char name[256]; int dir; off_t size; } Entry;
typedef struct { uint32_t text, accent, selected; } Theme;
typedef enum { MODE_NORMAL, MODE_ACTIONS, MODE_CONFIRM, MODE_CONFLICT, MODE_KEYBOARD, MODE_INFO } Mode;
typedef enum { OP_NONE, OP_COPY, OP_CUT } Op;

static volatile sig_atomic_t quit_requested;
static volatile uint32_t *raw_keys;
static uint32_t previous_keys;
static Screen screen;
static Theme theme = { 0xF4F4F4, 0x4A90D9, 0x101010 };
static stbtt_fontinfo font_info;
static unsigned char *font_buffer;
static float font_scale;
static int font_loaded;
/* Keep rasterized glyphs resident, just like the FrogUI/picoarch font path.
 * Reallocating a bitmap for every character on every frame caused both
 * flicker and missing/partially drawn glyphs on the target. */
struct glyph_cache { int valid, w, h, xoff, yoff; unsigned char *bmp; };
static struct glyph_cache glyphs[2][128];
static float glyph_scale[2] = { -1.0f, -1.0f };
static int glyph_baseline[2];
static Entry entries[MAX_ENTRIES];
static int entry_count, selected, scroll;
static char current[MAX_PATH] = ROOT;
static char marked[MAX_MARKED][MAX_PATH];
static int marked_count;
static char clipboard[MAX_PATH];
static char clipboard_paths[MAX_MARKED][MAX_PATH];
static int clipboard_count;
static Op clipboard_op;
static Mode mode;
static int menu_item, confirm_kind;
static int conflict_index, conflict_choice;
static char prompt[MAX_PATH], prompt_original[MAX_PATH];
static int keyboard_row, keyboard_col;
static char status_text[160];
static int status_frames;

static void die_signal(int sig) { (void)sig; quit_requested = 1; }
static int64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static uint32_t parse_rgb(const char *s, uint32_t fallback) {
    char *end = NULL; unsigned long v = strtoul(s, &end, 0);
    return end != s ? (uint32_t)v & 0xFFFFFFu : fallback;
}

static void load_theme(void) {
    FILE *f = fopen(THEME_FILE, "r"); char line[128], k[64], v[64];
    if (!f) return;
    while (fgets(line, sizeof line, f) && sscanf(line, "%63[^=]=%63s", k, v) == 2) {
        if (!strcmp(k, "text_color")) theme.text = parse_rgb(v, theme.text);
        else if (!strcmp(k, "selection_color")) theme.accent = parse_rgb(v, theme.accent);
        else if (!strcmp(k, "sel_text_color")) theme.selected = parse_rgb(v, theme.selected);
    }
    fclose(f);
}

static int load_font_file(const char *name) {
    char path[MAX_PATH];
    const char *dirs[] = { "/mnt/sdcard/cubegm/fonts", "/mnt/sdcard/frogui/fonts" };
    FILE *f = NULL;
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0] && !f; i++) {
        snprintf(path, sizeof path, "%s/%s", dirs[i], name);
        f = fopen(path, "rb");
    }
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 4 * 1024 * 1024) { fclose(f); return 0; }
    unsigned char *buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    fclose(f);
    if (!stbtt_InitFont(&font_info, buf, stbtt_GetFontOffsetForIndex(buf, 0))) { free(buf); return 0; }
    free(font_buffer); font_buffer = buf; font_loaded = 1; font_scale = stbtt_ScaleForPixelHeight(&font_info, 18.0f); return 1;
}

static void load_selected_font(void) {
    char selected[128] = "";
    FILE *f = fopen("/mnt/sdcard/frogui/settings.txt", "r");
    char line[256], key[80], value[160];
    if (f) {
        while (fgets(line, sizeof line, f) && sscanf(line, "%79[^=]=%159s", key, value) == 2)
            if (!strcmp(key, "font")) { strncpy(selected, value, sizeof selected - 1); break; }
        fclose(f);
    }
    if ((!selected[0] || !load_font_file(selected)) && !load_font_file("GamePocket-Regular-ZeroKern.ttf"))
        load_font_file("monogram.ttf");
}

static void load_keymap(void) {
    FILE *f = fopen(KEYMAP_FILE, "r"); char line[96], name[32]; int bit;
    if (!f) return;
    while (fgets(line, sizeof line, f) && sscanf(line, "%31[^=]=%d", name, &bit) == 2)
        for (int i = 0; i < BTN_COUNT; i++) if (!strcmp(name, key_names[i]) && bit >= 0 && bit < 32) key_bits[i] = bit;
    fclose(f);
}

static volatile uint32_t *open_keys(void) {
    key_t k = ftok("/tmp/joy_key", 'a'); if (k == (key_t)-1) return NULL;
    int id = shmget(k, 4, 0666); if (id < 0) return NULL;
    void *p = shmat(id, NULL, 0); return p == (void *)-1 ? NULL : (volatile uint32_t *)p;
}

static uint32_t keys_now(void) { uint32_t out = 0, raw = raw_keys ? (*raw_keys & 0xFFFFu) : 0; for (int i = 0; i < BTN_COUNT; i++) if (raw & (1u << key_bits[i])) out |= 1u << i; return out; }
static bool pressed(uint32_t keys, int b) { return (keys & (1u << b)) && !(previous_keys & (1u << b)); }
static void set_status(const char *s) { strncpy(status_text, s, sizeof status_text - 1); status_text[sizeof status_text - 1] = 0; status_frames = 150; }
static bool path_contains(const char *parent, const char *child) { size_t n = strlen(parent); return !strcmp(parent, child) || (!strncmp(parent, child, n) && child[n] == '/'); }

static void configure_layer(void) {
    int fd = open("/dev/dis", O_RDWR);
    if (fd < 0) return;
    struct dis_layer_blend_order order;
    memset(&order, 0, sizeof order);
    order.distype = DIS_TYPE_HD;
    order.main_layer = 2;
    order.auxp_layer = 0;
    order.gmas_layer = 3;
    order.gmaf_layer = 1;
    ioctl(fd, DIS_SET_LAYER_ORDER, &order);
    close(fd);
}

static uint32_t channel(uint32_t c, const struct fb_bitfield *f) { if (!f->length) return 0; return (((c * ((1u << f->length) - 1u) + 127u) / 255u) << f->offset); }
static uint32_t pack(const Screen *s, uint32_t rgb) { return channel(rgb >> 16 & 255, &s->vi.red) | channel(rgb >> 8 & 255, &s->vi.green) | channel(rgb & 255, &s->vi.blue); }

static int read_geometry(int *w, int *h) {
    *w = 640; *h = 480; FILE *f = fopen(DEVICE_FILE, "r"); char l[128], k[64], v[64];
    if (!f) return 0;
    while (fgets(l, sizeof l, f) && sscanf(l, "%63[^=]=%63s", k, v) == 2) { if (!strcmp(k, "TF_PANEL_W")) *w = atoi(v); else if (!strcmp(k, "TF_PANEL_H")) *h = atoi(v); }
    fclose(f); if (*w < 320 || *w > 1920) *w = 640; if (*h < 240 || *h > 1080) *h = 480; return 0;
}

static int screen_open(void) {
    int logical_w, logical_h; read_geometry(&logical_w, &logical_h);
    /* Standalone apps own the main framebuffer after picoarch hands off.
     * fb1 is the optional battery/volume overlay and is not a drawable panel
     * on every target (including R36SX). */
    memset(&screen, 0, sizeof screen); screen.fd = open("/dev/fb0", O_RDWR); if (screen.fd < 0) return -1;
    struct fb_fix_screeninfo fix;
    if (ioctl(screen.fd, FBIOGET_VSCREENINFO, &screen.vi) < 0 || ioctl(screen.fd, FBIOGET_FSCREENINFO, &fix) < 0 || !fix.smem_len) return -1;
    screen.w = screen.vi.xres; screen.h = screen.vi.yres; screen.pitch = fix.line_length;
    screen.bytespp = screen.vi.bits_per_pixel / 8; if (screen.bytespp != 2 && screen.bytespp != 4) return -1;
    screen.len = fix.smem_len; screen.mem = mmap(NULL, screen.len, PROT_READ | PROT_WRITE, MAP_SHARED, screen.fd, 0);
    if (screen.mem == MAP_FAILED) { screen.mem = NULL; return -1; }
    screen.canvas = calloc((size_t)logical_w * logical_h, sizeof(uint32_t));
    if (!screen.canvas) return -1;
    screen.w = logical_w; screen.h = logical_h; return 0;
}

static void screen_close(void) { if (screen.mem) munmap(screen.mem, screen.len); if (screen.fd >= 0) close(screen.fd); free(screen.canvas); memset(&screen, 0, sizeof screen); screen.fd = -1; }
static void clear(uint32_t c) { for (int y = 0; y < screen.h; y++) for (int x = 0; x < screen.w; x++) screen.canvas[(size_t)y * screen.w + x] = c; }
static void rect(int x, int y, int w, int h, uint32_t c) { if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; } if (x + w > screen.w) w = screen.w - x; if (y + h > screen.h) h = screen.h - y; if (w <= 0 || h <= 0) return; for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) screen.canvas[(size_t)yy * screen.w + xx] = c; }
static void glyph_cache_reset(int slot) {
    for (int i = 0; i < 128; i++) { free(glyphs[slot][i].bmp); glyphs[slot][i].bmp = NULL; glyphs[slot][i].valid = 0; }
}

static void text(int x, int y, const char *s, int scale, uint32_t c, int max) {
    int start = x;
    if (font_loaded) {
        int slot = scale > 1 ? 1 : 0;
        float sc = font_scale * (slot + 1);
        if (glyph_scale[slot] != sc) {
            glyph_cache_reset(slot); glyph_scale[slot] = sc;
            int ascent, descent, gap; stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &gap);
            glyph_baseline[slot] = (int)(ascent * sc);
        }
        int baseline = y + glyph_baseline[slot];
        for (; *s && x < start + max; s++) {
            unsigned char ch = (unsigned char)*s; if (ch >= 128) ch = '?';
            struct glyph_cache *g = &glyphs[slot][ch];
            if (!g->valid) {
                int gi = stbtt_FindGlyphIndex(&font_info, ch), x0, y0, x1, y1;
                if (!gi) { g->valid = 1; continue; }
                stbtt_GetGlyphBitmapBox(&font_info, gi, sc, sc, &x0, &y0, &x1, &y1);
                g->w = x1 - x0; g->h = y1 - y0; g->xoff = x0; g->yoff = y0;
                if (g->w > 0 && g->h > 0 && g->w <= 96 && g->h <= 96) {
                    g->bmp = malloc((size_t)g->w * g->h);
                    if (g->bmp) stbtt_MakeGlyphBitmap(&font_info, g->bmp, g->w, g->h, g->w, sc, sc, gi);
                }
                g->valid = 1;
            }
            if (g->bmp) for (int yy = 0; yy < g->h; yy++) for (int xx = 0; xx < g->w; xx++)
                if (g->bmp[yy * g->w + xx] > 60) rect(x + g->xoff + xx, baseline + g->yoff + yy, 1, 1, c);
            int ax, lsb; stbtt_GetCodepointHMetrics(&font_info, ch, &ax, &lsb); x += (int)(ax * sc) + scale;
        }
        return;
    }
    for (; *s && x + 8 * scale <= start + max; s++, x += 8 * scale) { unsigned char ch = (unsigned char)*s; if (ch >= 128) ch = '?'; for (int r = 0; r < 8; r++) for (int col = 0; col < 8; col++) if (fontdata8x8[ch * 8 + r] & (0x80u >> col)) rect(x + col * scale, y + r * scale, scale, scale, c); }
}

static void present(void) {
    for (int y = 0; y < screen.vi.yres; y++) {
        unsigned char *row = screen.mem + (size_t)(y + screen.vi.yoffset) * screen.pitch;
        for (int x = 0; x < screen.vi.xres; x++) {
            int lx = x * screen.w / screen.vi.xres, ly = y * screen.h / screen.vi.yres;
            uint32_t p = pack(&screen, screen.canvas[(size_t)ly * screen.w + lx]);
            if (screen.bytespp == 4) ((uint32_t *)row)[x + screen.vi.xoffset] = p; else ((uint16_t *)row)[x + screen.vi.xoffset] = (uint16_t)p;
        }
    }
}

static const char *base(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
static bool under_root(const char *p) { return !strncmp(p, ROOT, strlen(ROOT)) && (p[strlen(ROOT)] == 0 || p[strlen(ROOT)] == '/'); }
static void join_path(char *out, size_t n, const char *a, const char *b) { if (!strcmp(a, ROOT)) snprintf(out, n, "%s/%s", a, b); else snprintf(out, n, "%s/%s", a, b); }
static int entry_cmp(const void *a, const void *b) { const Entry *x = a, *y = b; if (x->dir != y->dir) return y->dir - x->dir; return strcasecmp(x->name, y->name); }

static void scan(void) {
    entry_count = 0; DIR *d = opendir(current); if (!d) { set_status("Cannot open folder"); return; }
    if (strcmp(current, ROOT) != 0) { strcpy(entries[entry_count].name, ".. "); entries[entry_count].dir = 1; entry_count++; }
    struct dirent *e;
    while ((e = readdir(d)) && entry_count < MAX_ENTRIES) {
        if (e->d_name[0] == '.') continue;
        struct stat st; char p[MAX_PATH]; join_path(p, sizeof p, current, e->d_name); if (stat(p, &st) != 0) continue;
        strncpy(entries[entry_count].name, e->d_name, sizeof entries[entry_count].name - 1); entries[entry_count].name[sizeof entries[entry_count].name - 1] = 0;
        entries[entry_count].dir = S_ISDIR(st.st_mode); entries[entry_count].size = st.st_size; entry_count++;
    }
    closedir(d); if (entry_count > 1) qsort(entries + (strcmp(current, ROOT) != 0), entry_count - (strcmp(current, ROOT) != 0), sizeof *entries, entry_cmp);
    if (selected >= entry_count) selected = entry_count ? entry_count - 1 : 0; if (selected < 0) selected = 0; scroll = selected >= 10 ? selected - 9 : 0;
}

static bool marked_path(const char *p) { for (int i = 0; i < marked_count; i++) if (!strcmp(marked[i], p)) return true; return false; }
static void toggle_mark(void) { if (selected >= entry_count || !strcmp(entries[selected].name, ".. ")) return; char p[MAX_PATH]; join_path(p, sizeof p, current, entries[selected].name); for (int i = 0; i < marked_count; i++) if (!strcmp(marked[i], p)) { memmove(marked[i], marked[i + 1], (size_t)(marked_count - i - 1) * sizeof marked[0]); marked_count--; set_status("Unmarked"); return; } if (marked_count < MAX_MARKED) { strcpy(marked[marked_count++], p); set_status("Marked"); } }
static int selected_paths(char out[][MAX_PATH], int cap) { int n = 0; if (marked_count) { for (int i = 0; i < marked_count && n < cap; i++) strcpy(out[n++], marked[i]); } else if (selected < entry_count && strcmp(entries[selected].name, ".. ")) { join_path(out[0], MAX_PATH, current, entries[selected].name); n = 1; } return n; }

static int copy_tree(const char *src, const char *dst) {
    struct stat st; if (lstat(src, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) { if (mkdir(dst, st.st_mode & 0777) != 0 && errno != EEXIST) return -1; DIR *d = opendir(src); if (!d) return -1; struct dirent *e; int rc = 0; while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; char a[MAX_PATH], b[MAX_PATH]; join_path(a, sizeof a, src, e->d_name); join_path(b, sizeof b, dst, e->d_name); if (copy_tree(a, b) != 0) { rc = -1; break; } } closedir(d); return rc; }
    int in = open(src, O_RDONLY), out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777); if (in < 0 || out < 0) { if (in >= 0) close(in); if (out >= 0) close(out); return -1; }
    char buf[32768]; ssize_t got; int rc = 0; while ((got = read(in, buf, sizeof buf)) > 0) { char *p = buf; while (got) { ssize_t wr = write(out, p, (size_t)got); if (wr <= 0) { rc = -1; break; } p += wr; got -= wr; } if (rc) break; } close(in); close(out); return rc;
}

static int remove_tree(const char *p) { struct stat st; if (lstat(p, &st) != 0) return -1; if (S_ISDIR(st.st_mode)) { DIR *d = opendir(p); if (!d) return -1; struct dirent *e; int rc = 0; while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; char c[MAX_PATH]; join_path(c, sizeof c, p, e->d_name); if (remove_tree(c) != 0) rc = -1; } closedir(d); if (rmdir(p) != 0) rc = -1; return rc; } return unlink(p); }

static void unique_copy_name(const char *dst, char *out, size_t n) {
    if (access(dst, F_OK) != 0) { strncpy(out, dst, n - 1); out[n - 1] = 0; return; }
    for (int i = 1; i < 1000; i++) {
        snprintf(out, n, "%s (copy %d)", dst, i);
        if (access(out, F_OK) != 0) return;
    }
    strncpy(out, dst, n - 1); out[n - 1] = 0;
}

/* policy: -1 ask, 0 skip, 1 overwrite, 2 keep both (auto-rename). */
static void paste_items(int start, int policy) {
    int rc = 0;
    for (int i = start; i < clipboard_count; i++) {
        char src[MAX_PATH], dst[MAX_PATH], final_dst[MAX_PATH];
        strcpy(src, clipboard_paths[i]);
        join_path(dst, sizeof dst, current, base(src));
        if (!under_root(dst) || !strcmp(src, dst)) { set_status("Already in this folder"); continue; }
        if (path_contains(src, dst)) { set_status("Cannot paste into itself"); rc = -1; continue; }
        if (access(dst, F_OK) == 0) {
            if (policy < 0) { conflict_index = i; mode = MODE_CONFLICT; return; }
            if (policy == 0) continue;
            if (policy == 2) unique_copy_name(dst, final_dst, sizeof final_dst);
            else { strcpy(final_dst, dst); if (remove_tree(final_dst) != 0) { rc = -1; continue; } }
        } else strcpy(final_dst, dst);
        if ((clipboard_op == OP_CUT ? rename(src, final_dst) : copy_tree(src, final_dst)) != 0) rc = -1;
    }
    if (rc == 0 && clipboard_op == OP_CUT) { clipboard[0] = 0; clipboard_count = 0; }
    set_status(rc == 0 ? "Paste complete" : "Some items failed"); scan(); mode = MODE_NORMAL;
}

static void clear_marks(void) { marked_count = 0; }
static const char *action_names[] = { "Copy", "Cut", "Paste", "Rename", "Delete", "New folder", "Info", "Cancel" };
static const int action_count = 8;
static void begin_keyboard(const char *initial, const char *old) { strncpy(prompt, initial ? initial : "", sizeof prompt - 1); prompt[sizeof prompt - 1] = 0; strncpy(prompt_original, old ? old : "", sizeof prompt_original - 1); prompt_original[sizeof prompt_original - 1] = 0; keyboard_row = 1; keyboard_col = 0; mode = MODE_KEYBOARD; }
static const char *kbd_rows[] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM_-" };

static void do_copy_or_cut(Op op) { char paths[MAX_MARKED][MAX_PATH]; int n = selected_paths(paths, MAX_MARKED); if (!n) { set_status("Nothing selected"); return; } clipboard_count = n; for (int i = 0; i < n; i++) strcpy(clipboard_paths[i], paths[i]); strcpy(clipboard, paths[0]); clipboard_op = op; set_status(op == OP_COPY ? "Copied to clipboard" : "Cut to clipboard"); }
static void do_paste(void) { if (!clipboard_count || !clipboard[0]) { set_status("Clipboard is empty"); return; } paste_items(0, -1); }
static void do_delete(void) { char paths[MAX_MARKED][MAX_PATH]; int n = selected_paths(paths, MAX_MARKED), rc = 0; for (int i = 0; i < n; i++) if (!under_root(paths[i]) || remove_tree(paths[i]) != 0) rc = -1; clear_marks(); set_status(rc ? "Delete failed" : "Deleted"); scan(); }
static void do_rename(const char *name) { char old[MAX_PATH], dst[MAX_PATH]; join_path(old, sizeof old, current, prompt_original); join_path(dst, sizeof dst, current, name); if (!name[0] || !under_root(dst) || rename(old, dst) != 0) set_status("Rename failed"); else set_status("Renamed"); scan(); }
static void do_new_folder(const char *name) { char dst[MAX_PATH]; join_path(dst, sizeof dst, current, name); if (!name[0] || !under_root(dst) || mkdir(dst, 0777) != 0) set_status("Create folder failed"); else set_status("Folder created"); scan(); }

static void draw(void) {
    int scale = screen.w >= 800 ? 2 : 1, row_h = 42 * scale, header = 48 * scale; clear(0x101010);
    rect(0, 0, screen.w, header, theme.accent); char title[120]; snprintf(title, sizeof title, "FROGSHELL  %s", current); text(12, 9 * scale, title, scale, theme.selected, screen.w - 24);
    int visible = (screen.h - header - 42 * scale) / row_h; if (visible < 1) visible = 1; if (selected < scroll) scroll = selected; if (selected >= scroll + visible) scroll = selected - visible + 1;
    for (int i = 0; i < visible && scroll + i < entry_count; i++) { int idx = scroll + i, y = header + i * row_h; bool active = idx == selected; char p[MAX_PATH]; join_path(p, sizeof p, current, entries[idx].name); uint32_t bg = active ? theme.accent : 0x202020; rect(0, y, screen.w, row_h - 2, bg); if (marked_path(p)) rect(0, y, 5 * scale, row_h - 2, 0xF0C040); char label[300]; snprintf(label, sizeof label, "%s%s", entries[idx].name, entries[idx].dir ? "/" : ""); text(14 * scale, y + 8 * scale, label, scale, active ? theme.selected : theme.text, screen.w - 120 * scale); if (!entries[idx].dir) { char sz[32]; snprintf(sz, sizeof sz, "%lld", (long long)entries[idx].size); text(screen.w - (int)strlen(sz) * 8 * scale - 14 * scale, y + 8 * scale, sz, scale, active ? theme.selected : 0xAAAAAA, 100 * scale); } }
    char footer[220]; snprintf(footer, sizeof footer, "A Open   B Back   X Menu   Y Mark   SELECT Paste   START New");
    rect(0, screen.h - 34 * scale, screen.w, 34 * scale, 0x181818);
    text(12 * scale, screen.h - 27 * scale, footer, scale, theme.text, screen.w - 24 * scale);
    if (status_frames > 0) { int w = (int)strlen(status_text) * 8 * scale + 24 * scale; rect((screen.w - w) / 2, screen.h - 68 * scale, w, 28 * scale, theme.accent); text((screen.w - w) / 2 + 12 * scale, screen.h - 61 * scale, status_text, scale, theme.selected, w - 24 * scale); }
    if (mode == MODE_ACTIONS) { int w = 250 * scale, h = action_count * row_h + 20 * scale, x = (screen.w - w) / 2, y = (screen.h - h) / 2; rect(x, y, w, h, 0x303030); for (int i = 0; i < action_count; i++) { bool a = i == menu_item; if (a) rect(x + 4 * scale, y + 8 * scale + i * row_h, w - 8 * scale, row_h - 2, theme.accent); text(x + 18 * scale, y + 15 * scale + i * row_h, action_names[i], scale, a ? theme.selected : theme.text, w - 30 * scale); } }
    if (mode == MODE_CONFIRM) { int w = 430 * scale, x = (screen.w - w) / 2; rect(x, screen.h / 2 - 48 * scale, w, 96 * scale, 0x303030); text(x + 18 * scale, screen.h / 2 - 28 * scale, confirm_kind == 1 ? "Delete selected item(s)?" : "Paste into this folder?", scale, theme.text, w - 36 * scale); text(x + 18 * scale, screen.h / 2 + 10 * scale, "A YES   B CANCEL", scale, theme.selected, w - 36 * scale); }
    if (mode == MODE_CONFLICT) {
        int w = screen.w - 44 * scale, h = 168 * scale, x = (screen.w - w) / 2, y = (screen.h - h) / 2;
        rect(x, y, w, h, 0x303030); rect(x, y, w, 36 * scale, theme.accent);
        text(x + 14 * scale, y + 8 * scale, "ITEM ALREADY EXISTS", scale, theme.selected, w - 28 * scale);
        text(x + 14 * scale, y + 50 * scale, base(clipboard_paths[conflict_index]), scale, theme.text, w - 28 * scale);
        const char *choices[] = { "Skip", "Overwrite", "Keep both" };
        for (int i = 0; i < 3; i++) { int bx = x + 14 * scale + i * ((w - 28 * scale) / 3); if (i == conflict_choice) rect(bx, y + 86 * scale, (w - 42 * scale) / 3, 30 * scale, theme.accent); text(bx + 8 * scale, y + 94 * scale, choices[i], scale, i == conflict_choice ? theme.selected : theme.text, (w - 42 * scale) / 3 - 12 * scale); }
        text(x + 14 * scale, y + 132 * scale, "LEFT/RIGHT CHOOSE   A APPLY   B CANCEL", scale, theme.text, w - 28 * scale);
    }
    if (mode == MODE_INFO) { int w = screen.w - 40 * scale; rect(20 * scale, screen.h / 2 - 70 * scale, w, 140 * scale, 0x303030); char p[MAX_PATH], info[160]; if (selected < entry_count) { join_path(p, sizeof p, current, entries[selected].name); struct stat st; stat(p, &st); snprintf(info, sizeof info, "%s  %s  %lld bytes", entries[selected].name, entries[selected].dir ? "folder" : "file", (long long)st.st_size); text(32 * scale, screen.h / 2 - 35 * scale, info, scale, theme.text, w - 24 * scale); } text(32 * scale, screen.h / 2 + 10 * scale, "B CLOSE", scale, theme.selected, w - 24 * scale); }
    if (mode == MODE_KEYBOARD) { int w = screen.w - 30 * scale, x = 15 * scale, y = screen.h / 2 - 100 * scale; rect(x, y, w, 190 * scale, 0x303030); text(x + 12 * scale, y + 12 * scale, prompt, scale, theme.selected, w - 24 * scale); for (int r = 0; r < 4; r++) text(x + 18 * scale, y + 48 * scale + r * 24 * scale, kbd_rows[r], scale, r == keyboard_row ? theme.selected : theme.text, w - 36 * scale); text(x + 18 * scale, y + 150 * scale, "SPACE  DEL  DONE", scale, theme.text, w - 36 * scale); text(x + 18 * scale, y + 174 * scale, "A TYPE  START SAVE  B CANCEL", scale, theme.selected, w - 36 * scale); }
    present();
}

static void keyboard_input(uint32_t k) {
    if (pressed(k, BTN_UP)) keyboard_row = (keyboard_row + 3) % 4;
    if (pressed(k, BTN_DOWN)) keyboard_row = (keyboard_row + 1) % 4;
    if (pressed(k, BTN_LEFT)) keyboard_col--;
    if (pressed(k, BTN_RIGHT)) keyboard_col++;
    int len = (int)strlen(kbd_rows[keyboard_row]); if (keyboard_col < 0) keyboard_col = len - 1; if (keyboard_col >= len) keyboard_col = 0;
    if (pressed(k, BTN_A) && strlen(prompt) + 1 < sizeof prompt) { size_t n = strlen(prompt); prompt[n] = kbd_rows[keyboard_row][keyboard_col]; prompt[n + 1] = 0; }
    if (pressed(k, BTN_Y) && prompt[0]) prompt[strlen(prompt) - 1] = 0;
    if (pressed(k, BTN_START)) { if (prompt_original[0]) do_rename(prompt); else do_new_folder(prompt); mode = MODE_NORMAL; }
    if (pressed(k, BTN_B)) mode = MODE_NORMAL;
}

static void actions_input(uint32_t k) {
    if (pressed(k, BTN_UP)) menu_item = (menu_item + action_count - 1) % action_count;
    if (pressed(k, BTN_DOWN)) menu_item = (menu_item + 1) % action_count;
    if (pressed(k, BTN_B) || (menu_item == action_count - 1 && pressed(k, BTN_A))) mode = MODE_NORMAL;
    if (!pressed(k, BTN_A)) return;
    switch (menu_item) {
    case 0: do_copy_or_cut(OP_COPY); mode = MODE_NORMAL; break;
    case 1: do_copy_or_cut(OP_CUT); mode = MODE_NORMAL; break;
    case 2: mode = clipboard[0] ? MODE_CONFIRM : MODE_NORMAL; confirm_kind = 2; if (!clipboard[0]) set_status("Clipboard is empty"); break;
    case 3: if (selected < entry_count && strcmp(entries[selected].name, ".. ")) begin_keyboard(entries[selected].name, entries[selected].name); break;
    case 4: mode = MODE_CONFIRM; confirm_kind = 1; break;
    case 5: begin_keyboard("", NULL); break;
    case 6: mode = MODE_INFO; break;
    default: break;
    }
}

static void normal_input(uint32_t k) {
    int visible = (screen.h - 48 * (screen.w >= 800 ? 2 : 1) - 34 * (screen.w >= 800 ? 2 : 1)) / (42 * (screen.w >= 800 ? 2 : 1)); if (visible < 1) visible = 1;
    if (pressed(k, BTN_UP) && selected > 0) selected--; if (pressed(k, BTN_DOWN) && selected + 1 < entry_count) selected++; if (pressed(k, BTN_L1)) selected -= visible; if (pressed(k, BTN_R1)) selected += visible; if (selected < 0) selected = 0; if (selected >= entry_count) selected = entry_count - 1;
    if (pressed(k, BTN_Y)) toggle_mark();
    if (pressed(k, BTN_SELECT)) { confirm_kind = 2; mode = clipboard[0] ? MODE_CONFIRM : MODE_NORMAL; if (!clipboard[0]) set_status("Clipboard is empty"); }
    if (pressed(k, BTN_X)) { menu_item = 0; mode = MODE_ACTIONS; }
    if (pressed(k, BTN_START)) begin_keyboard("", NULL);
    if (pressed(k, BTN_B)) { if (strcmp(current, ROOT) == 0) { quit_requested = 1; return; } char *s = strrchr(current, '/'); if (s && s != current) *s = 0; else strcpy(current, ROOT); scan(); }
    if (pressed(k, BTN_A) && selected < entry_count) { if (entries[selected].dir) { if (!strcmp(entries[selected].name, ".. ")) { char *s = strrchr(current, '/'); if (s && s != current) *s = 0; else strcpy(current, ROOT); } else { char p[MAX_PATH]; join_path(p, sizeof p, current, entries[selected].name); if (under_root(p)) strcpy(current, p); } scan(); } else { menu_item = 0; mode = MODE_ACTIONS; } }
}

static void input_loop(void) {
    uint32_t k = keys_now();
    uint32_t quit_chord = (1u << BTN_START) | (1u << BTN_SELECT);
    if ((k & quit_chord) == quit_chord && (previous_keys & quit_chord) != quit_chord) { quit_requested = 1; previous_keys = k; return; }
    if (mode == MODE_ACTIONS) actions_input(k); else if (mode == MODE_KEYBOARD) keyboard_input(k); else if (mode == MODE_CONFIRM) { if (pressed(k, BTN_A)) { if (confirm_kind == 1) { do_delete(); mode = MODE_NORMAL; } else do_paste(); } if (pressed(k, BTN_B)) mode = MODE_NORMAL; } else if (mode == MODE_CONFLICT) { if (pressed(k, BTN_LEFT)) conflict_choice = (conflict_choice + 2) % 3; if (pressed(k, BTN_RIGHT)) conflict_choice = (conflict_choice + 1) % 3; if (pressed(k, BTN_A)) paste_items(conflict_index, conflict_choice); if (pressed(k, BTN_B)) mode = MODE_NORMAL; } else if (mode == MODE_INFO) { if (pressed(k, BTN_B) || pressed(k, BTN_A)) mode = MODE_NORMAL; } else normal_input(k); previous_keys = k;
}

int main(void) {
    signal(SIGINT, die_signal); signal(SIGTERM, die_signal); load_theme(); load_selected_font(); load_keymap(); raw_keys = open_keys(); screen.fd = -1;
    configure_layer();
    if (screen_open() != 0) return 1;
    scan();
    int64_t last = 0;
    while (!quit_requested) { int64_t t = now_ms(); if (t - last >= 16) { last = t; input_loop(); if (status_frames > 0) status_frames--; draw(); } else usleep(1000); }
    clear(0); present(); screen_close(); if (raw_keys) shmdt((const void *)raw_keys); return 0;
}
