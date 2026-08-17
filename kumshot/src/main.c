#include <stdbool.h>
#include <cairo/cairo.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#define MAX_OUTPUTS 8

typedef struct {
    struct wl_output          *wl_output;
    char                       name[64];
    int                        width, height;
    int                        stride;
    uint8_t                   *data;
    struct zwlr_screencopy_frame_v1 *frame;
    bool                       done;
    bool                       failed;
    int                        x, y;

    /* region-select overlay, only used with -r */
    struct wl_surface             *overlay_surface;
    struct zwlr_layer_surface_v1  *overlay_layer;
    struct wl_buffer              *overlay_buffer;
    int                             overlay_w, overlay_h;
    bool                            overlay_configured;
} shot_output;

static struct {
    struct wl_display                *display;
    struct wl_registry               *registry;
    struct wl_shm                    *shm;
    struct wl_compositor              *compositor;
    struct wl_seat                    *seat;
    struct wl_pointer                 *pointer;
    struct wl_keyboard                *keyboard;
    struct xkb_context                *xkb_ctx;
    struct xkb_keymap                 *xkb_map;
    struct xkb_state                  *xkb_state;
    struct zwlr_screencopy_manager_v1 *screencopy;
    struct zwlr_layer_shell_v1        *layer_shell;
    struct zxdg_output_manager_v1     *xdg_output_manager;
    shot_output outputs[MAX_OUTPUTS];
    int         output_count;
    char        outpath[512];
    bool        all_outputs;
    bool        region_mode;
    bool        running;
} app;

/* Region-select drag state (single active drag at a time). */
static struct {
    shot_output *hover_output;
    double       hover_x, hover_y;
    bool         dragging;
    double       start_x, start_y;
    shot_output *drag_output;
    bool         done;
    bool         cancelled;
} region;

static void frame_buffer(void *data,
    struct zwlr_screencopy_frame_v1 *frame,
    uint32_t format, uint32_t width, uint32_t height, uint32_t stride)
{
    shot_output *o = data;
    o->width  = (int)width;
    o->height = (int)height;
    o->stride = (int)stride;

    size_t size = (size_t)stride * height;
    char name[] = "/kumshot-XXXXXX";
    int fd = shm_open(name, O_RDWR|O_CREAT|O_EXCL, 0600);
    shm_unlink(name);
    if (fd < 0) { o->failed = true; return; }
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); o->failed = true; return; }

    o->data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (o->data == MAP_FAILED) { close(fd); o->failed = true; return; }

    /* Must use the exact format the compositor advertised in this event --
     * wlroots' screencopy implementation rejects a mismatched format (e.g.
     * its GLES2 renderer reads back ARGB8888, not XRGB8888) with a
     * zwlr_screencopy_frame_v1 protocol error and the copy never happens.
     * save_png()'s CAIRO_FORMAT_RGB24 ignores the alpha byte either way, so
     * this is safe regardless of which of the two the compositor picks. */
    struct wl_shm_pool *pool = wl_shm_create_pool(app.shm, fd, (int32_t)size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0,
        width, height, stride, format);
    wl_shm_pool_destroy(pool);
    close(fd);

    zwlr_screencopy_frame_v1_copy(frame, buf);
    wl_buffer_destroy(buf);
}

static void frame_flags(void *d, struct zwlr_screencopy_frame_v1 *f,
    uint32_t flags) {}
static void frame_damage(void *d, struct zwlr_screencopy_frame_v1 *f,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h) {}
static void frame_linux_dmabuf(void *d, struct zwlr_screencopy_frame_v1 *f,
    uint32_t fmt, uint32_t w, uint32_t h) {}
static void frame_buffer_done(void *d, struct zwlr_screencopy_frame_v1 *f) {}

static void frame_ready(void *data,
    struct zwlr_screencopy_frame_v1 *frame,
    uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
{
    shot_output *o = data;
    o->done = true;
}

static void frame_failed(void *data,
    struct zwlr_screencopy_frame_v1 *frame)
{
    shot_output *o = data;
    o->failed = true;
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer      = frame_buffer,
    .flags       = frame_flags,
    .ready       = frame_ready,
    .failed      = frame_failed,
    .damage      = frame_damage,
    .linux_dmabuf    = frame_linux_dmabuf,
    .buffer_done = frame_buffer_done,
};

static void wlo_geometry(void *d, struct wl_output *o,
    int x, int y, int pw, int ph, int sp,
    const char *mk, const char *mo, int t)
{
    shot_output *so = d;
    so->x = x; so->y = y;
}

static void wlo_mode(void *d, struct wl_output *o,
    uint32_t flags, int w, int h, int r)
{
    shot_output *so = d;
    if (flags & WL_OUTPUT_MODE_CURRENT) { so->width = w; so->height = h; }
}

static void wlo_done(void *d, struct wl_output *o) {}
static void wlo_scale(void *d, struct wl_output *o, int32_t s) {}
static void wlo_name(void *d, struct wl_output *o, const char *n)
{ strncpy(((shot_output*)d)->name, n, 63); }
static void wlo_desc(void *d, struct wl_output *o, const char *dc) {}

static const struct wl_output_listener wlo_listener = {
    .geometry=wlo_geometry,.mode=wlo_mode,.done=wlo_done,
    .scale=wlo_scale,.name=wlo_name,.description=wlo_desc,
};

/* wl_output's own geometry x/y are commonly left at 0,0 by compositors;
 * xdg-output's logical_position is the reliable source of layout position. */
static void xdgo_logical_position(void *d, struct zxdg_output_v1 *o,
    int32_t x, int32_t y)
{
    shot_output *so = d;
    so->x = x; so->y = y;
}
static void xdgo_logical_size(void *d, struct zxdg_output_v1 *o,
    int32_t w, int32_t h) {}
static void xdgo_done(void *d, struct zxdg_output_v1 *o) {}
static void xdgo_name(void *d, struct zxdg_output_v1 *o, const char *n) {}
static void xdgo_description(void *d, struct zxdg_output_v1 *o, const char *dc) {}

static const struct zxdg_output_v1_listener xdgo_listener = {
    .logical_position = xdgo_logical_position,
    .logical_size     = xdgo_logical_size,
    .done             = xdgo_done,
    .name             = xdgo_name,
    .description      = xdgo_description,
};

/* --- region-select overlay (-r) --- */

static int overlay_shm_file(size_t size)
{
    char name[] = "/kumshot-overlay-XXXXXX";
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return -1;
    shm_unlink(name);
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return -1; }
    return fd;
}

static void overlay_render(shot_output *so)
{
    if (!so->overlay_configured || so->overlay_w <= 0 || so->overlay_h <= 0)
        return;

    int stride = so->overlay_w * 4;
    size_t size = (size_t)stride * so->overlay_h;

    int fd = overlay_shm_file(size);
    if (fd < 0) return;
    uint8_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return; }

    struct wl_shm_pool *pool = wl_shm_create_pool(app.shm, fd, (int32_t)size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0,
        so->overlay_w, so->overlay_h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        data, CAIRO_FORMAT_ARGB32, so->overlay_w, so->overlay_h, stride);
    cairo_t *cr = cairo_create(cs);

    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_rectangle(cr, 0, 0, so->overlay_w, so->overlay_h);
    cairo_fill(cr);

    if (region.dragging && region.drag_output == so) {
        double x0 = region.start_x, y0 = region.start_y;
        double x1 = region.hover_x, y1 = region.hover_y;
        double rx = x0 < x1 ? x0 : x1;
        double ry = y0 < y1 ? y0 : y1;
        double rw = fabs(x1 - x0);
        double rh = fabs(y1 - y0);

        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_rectangle(cr, rx, ry, rw, rh);
        cairo_fill(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        cairo_set_source_rgba(cr, 0.45, 0.60, 0.95, 0.9);
        cairo_set_line_width(cr, 1.5);
        cairo_rectangle(cr, rx + 0.5, ry + 0.5, rw, rh);
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(cs);

    if (so->overlay_buffer) wl_buffer_destroy(so->overlay_buffer);
    so->overlay_buffer = buf;

    wl_surface_attach(so->overlay_surface, buf, 0, 0);
    wl_surface_damage(so->overlay_surface, 0, 0, so->overlay_w, so->overlay_h);
    wl_surface_commit(so->overlay_surface);

    munmap(data, size);
}

static void overlay_configure(void *data, struct zwlr_layer_surface_v1 *ls,
    uint32_t serial, uint32_t w, uint32_t h)
{
    shot_output *so = data;
    so->overlay_w = (int)w;
    so->overlay_h = (int)h;
    so->overlay_configured = true;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    overlay_render(so);
}

static void overlay_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    region.cancelled = true;
}

static const struct zwlr_layer_surface_v1_listener overlay_listener = {
    .configure = overlay_configure,
    .closed    = overlay_closed,
};

static shot_output *output_owning_surface(struct wl_surface *surface)
{
    for (int i = 0; i < app.output_count; i++)
        if (app.outputs[i].overlay_surface == surface)
            return &app.outputs[i];
    return NULL;
}

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial,
    struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
    region.hover_output = output_owning_surface(surface);
    region.hover_x = wl_fixed_to_double(sx);
    region.hover_y = wl_fixed_to_double(sy);
}
static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial,
    struct wl_surface *surface) {}
static void ptr_motion(void *d, struct wl_pointer *p, uint32_t time,
    wl_fixed_t sx, wl_fixed_t sy)
{
    region.hover_x = wl_fixed_to_double(sx);
    region.hover_y = wl_fixed_to_double(sy);
    if (region.dragging && region.drag_output)
        overlay_render(region.drag_output);
}
static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state)
{
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (!region.hover_output) return;
        region.dragging    = true;
        region.drag_output = region.hover_output;
        region.start_x     = region.hover_x;
        region.start_y     = region.hover_y;
    } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (region.dragging)
            region.done = true;
        region.dragging = false;
    }
}
static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time,
    uint32_t axis, wl_fixed_t value) {}
static void ptr_frame(void *d, struct wl_pointer *p) {}
static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t src) {}
static void ptr_axis_stop(void *d, struct wl_pointer *p,
    uint32_t time, uint32_t axis) {}
static void ptr_axis_discrete(void *d, struct wl_pointer *p,
    uint32_t axis, int32_t discrete) {}

static const struct wl_pointer_listener pointer_listener = {
    .enter=ptr_enter, .leave=ptr_leave, .motion=ptr_motion,
    .button=ptr_button, .axis=ptr_axis, .frame=ptr_frame,
    .axis_source=ptr_axis_source, .axis_stop=ptr_axis_stop,
    .axis_discrete=ptr_axis_discrete,
};

static void kb_keymap(void *d, struct wl_keyboard *kb,
    uint32_t fmt, int32_t fd, uint32_t size)
{
    char *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { close(fd); return; }
    if (app.xkb_map)   xkb_keymap_unref(app.xkb_map);
    if (app.xkb_state) xkb_state_unref(app.xkb_state);
    app.xkb_map   = xkb_keymap_new_from_string(app.xkb_ctx, map,
        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    app.xkb_state = xkb_state_new(app.xkb_map);
    munmap(map, size);
    close(fd);
}
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s,
    struct wl_surface *surf, struct wl_array *a) {}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s,
    struct wl_surface *surf) {}
static void kb_modifiers(void *d, struct wl_keyboard *k,
    uint32_t s, uint32_t dep, uint32_t lat, uint32_t lck, uint32_t grp)
{
    if (app.xkb_state)
        xkb_state_update_mask(app.xkb_state, dep, lat, lck, 0, 0, grp);
}
static void kb_repeat_info(void *d, struct wl_keyboard *k,
    int32_t rate, int32_t delay) {}
static void kb_key(void *d, struct wl_keyboard *k, uint32_t s,
    uint32_t t, uint32_t key, uint32_t state)
{
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !app.xkb_state) return;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkb_state, key + 8);
    if (sym == XKB_KEY_Escape)
        region.cancelled = true;
}

static const struct wl_keyboard_listener kb_listener = {
    .keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave,
    .key = kb_key, .modifiers = kb_modifiers, .repeat_info = kb_repeat_info,
};

static void seat_capabilities(void *d, struct wl_seat *seat, uint32_t caps)
{
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !app.pointer) {
        app.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app.pointer, &pointer_listener, NULL);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !app.keyboard) {
        app.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app.keyboard, &kb_listener, NULL);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities, .name = seat_name,
};

static void reg_global(void *d, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t ver)
{
    if (!strcmp(iface, wl_shm_interface.name))
        app.shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, wl_compositor_interface.name))
        app.compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_seat_interface.name)) {
        app.seat = wl_registry_bind(reg, name, &wl_seat_interface, 7);
        wl_seat_add_listener(app.seat, &seat_listener, NULL);
    }
    else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
        app.layer_shell = wl_registry_bind(reg, name,
            &zwlr_layer_shell_v1_interface, 1);
    else if (!strcmp(iface, zwlr_screencopy_manager_v1_interface.name))
        app.screencopy = wl_registry_bind(reg, name,
            &zwlr_screencopy_manager_v1_interface, 3);
    else if (!strcmp(iface, zxdg_output_manager_v1_interface.name))
        app.xdg_output_manager = wl_registry_bind(reg, name,
            &zxdg_output_manager_v1_interface, 3);
    else if (!strcmp(iface, wl_output_interface.name)) {
        if (app.output_count < MAX_OUTPUTS) {
            shot_output *so = &app.outputs[app.output_count];
            so->wl_output = wl_registry_bind(reg, name, &wl_output_interface, 4);
            wl_output_add_listener(so->wl_output, &wlo_listener, so);
            app.output_count++;
        }
    }
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener reg_listener = {
    .global=reg_global,.global_remove=reg_remove,
};

static void save_png(const char *path, uint8_t *data,
    int width, int height, int stride)
{
    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        data, CAIRO_FORMAT_RGB24, width, height, stride);
    cairo_surface_write_to_png(cs, path);
    cairo_surface_destroy(cs);
}

static char *default_path(char *buf, size_t sz)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", tm);
    snprintf(buf, sz, "%s/Pictures/kumshot_%s.png", home, ts);
    return buf;
}

static void usage(const char *a)
{
    fprintf(stderr,
        "usage: %s [options]\n"
        "  -o FILE      output path (default: ~/Pictures/kumshot_TIMESTAMP.png)\n"
        "  -s OUTPUT    capture specific output by name\n"
        "  -a           capture all outputs into one image\n"
        "  -r           interactively drag-select a region to capture\n", a);
}

static void destroy_overlays(void)
{
    for (int i = 0; i < app.output_count; i++) {
        shot_output *so = &app.outputs[i];
        if (so->overlay_layer)   { zwlr_layer_surface_v1_destroy(so->overlay_layer); so->overlay_layer = NULL; }
        if (so->overlay_surface) { wl_surface_destroy(so->overlay_surface); so->overlay_surface = NULL; }
        if (so->overlay_buffer)  { wl_buffer_destroy(so->overlay_buffer); so->overlay_buffer = NULL; }
    }
}

/* Runs the interactive drag-select overlay to completion. Returns
 * false (nothing to capture) if the user cancelled or selected an
 * empty region; on success fills out x/y/w/h in the coordinate space
 * of *out_output (which capture_output_region expects). */
static bool run_region_select(shot_output **out_output,
    int *out_x, int *out_y, int *out_w, int *out_h)
{
    if (!app.compositor || !app.layer_shell || !app.seat) {
        fprintf(stderr, "kumshot: -r needs compositor/layer-shell/seat\n");
        return false;
    }

    for (int i = 0; i < app.output_count; i++) {
        shot_output *so = &app.outputs[i];
        so->overlay_surface = wl_compositor_create_surface(app.compositor);
        so->overlay_layer = zwlr_layer_shell_v1_get_layer_surface(
            app.layer_shell, so->overlay_surface, so->wl_output,
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "kumshot-region");
        zwlr_layer_surface_v1_set_size(so->overlay_layer, 0, 0);
        zwlr_layer_surface_v1_set_anchor(so->overlay_layer,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(so->overlay_layer, -1);
        zwlr_layer_surface_v1_set_keyboard_interactivity(so->overlay_layer,
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
        zwlr_layer_surface_v1_add_listener(so->overlay_layer,
            &overlay_listener, so);
        wl_surface_commit(so->overlay_surface);
    }

    wl_display_roundtrip(app.display);

    while (!region.done && !region.cancelled) {
        if (wl_display_dispatch(app.display) < 0) {
            region.cancelled = true;
            break;
        }
    }

    destroy_overlays();
    wl_display_roundtrip(app.display);

    if (region.cancelled || !region.drag_output)
        return false;

    double x0 = region.start_x, y0 = region.start_y;
    double x1 = region.hover_x, y1 = region.hover_y;
    int rx = (int)(x0 < x1 ? x0 : x1);
    int ry = (int)(y0 < y1 ? y0 : y1);
    int rw = (int)fabs(x1 - x0);
    int rh = (int)fabs(y1 - y0);

    if (rw < 2 || rh < 2)
        return false;

    *out_output = region.drag_output;
    *out_x = rx;
    *out_y = ry;
    *out_w = rw;
    *out_h = rh;
    return true;
}

int main(int argc, char *argv[])
{
    memset(&app, 0, sizeof(app));
    app.running = true;
    app.all_outputs = false;

    char target_output[64] = {0};

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i],"-o") && i+1<argc) strncpy(app.outpath, argv[++i], 511);
        else if (!strcmp(argv[i],"-s") && i+1<argc) strncpy(target_output, argv[++i], 63);
        else if (!strcmp(argv[i],"-a")) app.all_outputs = true;
        else if (!strcmp(argv[i],"-r")) app.region_mode = true;
        else if (!strcmp(argv[i],"-h")) { usage(argv[0]); return 0; }
    }

    if (!app.outpath[0])
        default_path(app.outpath, sizeof(app.outpath));

    app.display = wl_display_connect(NULL);
    if (!app.display) { fprintf(stderr,"kumshot: cannot connect\n"); return 1; }

    /* kb_keymap() fires from the global wl_keyboard listener whenever the
     * compositor sends a keymap, regardless of -r/region mode -- it needs
     * a valid xkb_ctx even in plain screenshot mode, or xkbcommon
     * segfaults dereferencing a NULL context. */
    app.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &reg_listener, NULL);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    if (app.xdg_output_manager) {
        for (int i = 0; i < app.output_count; i++) {
            struct zxdg_output_v1 *xdg_out = zxdg_output_manager_v1_get_xdg_output(
                app.xdg_output_manager, app.outputs[i].wl_output);
            zxdg_output_v1_add_listener(xdg_out, &xdgo_listener, &app.outputs[i]);
        }
        wl_display_roundtrip(app.display);
    }

    if (!app.shm || !app.screencopy) {
        fprintf(stderr,"kumshot: missing protocols\n"); return 1;
    }

    if (app.region_mode) {
        shot_output *ro;
        int rx, ry, rw, rh;
        if (!run_region_select(&ro, &rx, &ry, &rw, &rh)) {
            fprintf(stderr, "kumshot: selection cancelled\n");
            return 1;
        }

        ro->frame = zwlr_screencopy_manager_v1_capture_output_region(
            app.screencopy, 0, ro->wl_output, rx, ry, rw, rh);
        zwlr_screencopy_frame_v1_add_listener(ro->frame, &frame_listener, ro);

        while (!ro->done && !ro->failed)
            if (wl_display_dispatch(app.display) < 0) break;

        if (!ro->done) {
            fprintf(stderr, "kumshot: capture failed\n");
            return 1;
        }

        save_png(app.outpath, ro->data, ro->width, ro->height, ro->stride);
        printf("%s\n", app.outpath);
        return 0;
    }

    for (int i = 0; i < app.output_count; i++) {
        shot_output *so = &app.outputs[i];
        if (!app.all_outputs && target_output[0] &&
                strcmp(so->name, target_output) != 0)
            continue;
        if (!app.all_outputs && !target_output[0] && i > 0)
            continue;
        so->frame = zwlr_screencopy_manager_v1_capture_output(
            app.screencopy, 0, so->wl_output);
        zwlr_screencopy_frame_v1_add_listener(so->frame, &frame_listener, so);
    }

    while (app.running) {
        wl_display_dispatch(app.display);
        bool all_done = true;
        for (int i = 0; i < app.output_count; i++) {
            if (app.outputs[i].frame && !app.outputs[i].done && !app.outputs[i].failed)
                all_done = false;
        }
        if (all_done) break;
    }

    if (app.all_outputs && app.output_count > 1) {
        int total_w = 0, total_h = 0;
        for (int i = 0; i < app.output_count; i++) {
            if (!app.outputs[i].done) continue;
            if (app.outputs[i].x + app.outputs[i].width > total_w)
                total_w = app.outputs[i].x + app.outputs[i].width;
            if (app.outputs[i].y + app.outputs[i].height > total_h)
                total_h = app.outputs[i].y + app.outputs[i].height;
        }
        if (total_w > 0 && total_h > 0) {
            cairo_surface_t *canvas = cairo_image_surface_create(
                CAIRO_FORMAT_RGB24, total_w, total_h);
            cairo_t *cr = cairo_create(canvas);
            for (int i = 0; i < app.output_count; i++) {
                shot_output *so = &app.outputs[i];
                if (!so->done) continue;
                cairo_surface_t *piece = cairo_image_surface_create_for_data(
                    so->data, CAIRO_FORMAT_RGB24,
                    so->width, so->height, so->stride);
                cairo_set_source_surface(cr, piece, so->x, so->y);
                cairo_paint(cr);
                cairo_surface_destroy(piece);
            }
            cairo_destroy(cr);
            cairo_surface_write_to_png(canvas, app.outpath);
            cairo_surface_destroy(canvas);
        }
    } else {
        for (int i = 0; i < app.output_count; i++) {
            if (app.outputs[i].done) {
                save_png(app.outpath, app.outputs[i].data,
                    app.outputs[i].width, app.outputs[i].height,
                    app.outputs[i].stride);
                break;
            }
        }
    }

    printf("%s\n", app.outpath);
    return 0;
}
