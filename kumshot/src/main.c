#include <cairo/cairo.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#include "wlr-screencopy-unstable-v1-client-protocol.h"

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
} shot_output;

static struct {
    struct wl_display                *display;
    struct wl_registry               *registry;
    struct wl_shm                    *shm;
    struct zwlr_screencopy_manager_v1 *screencopy;
    shot_output outputs[MAX_OUTPUTS];
    int         output_count;
    char        outpath[512];
    bool        all_outputs;
    bool        running;
} app;

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

    struct wl_shm_pool *pool = wl_shm_create_pool(app.shm, fd, (int32_t)size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0,
        width, height, stride, WL_SHM_FORMAT_XRGB8888);
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

static void reg_global(void *d, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t ver)
{
    if (!strcmp(iface, wl_shm_interface.name))
        app.shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, zwlr_screencopy_manager_v1_interface.name))
        app.screencopy = wl_registry_bind(reg, name,
            &zwlr_screencopy_manager_v1_interface, 3);
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
        "  -a           capture all outputs into one image\n", a);
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
        else if (!strcmp(argv[i],"-h")) { usage(argv[0]); return 0; }
    }

    if (!app.outpath[0])
        default_path(app.outpath, sizeof(app.outpath));

    app.display = wl_display_connect(NULL);
    if (!app.display) { fprintf(stderr,"kumshot: cannot connect\n"); return 1; }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &reg_listener, NULL);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    if (!app.shm || !app.screencopy) {
        fprintf(stderr,"kumshot: missing protocols\n"); return 1;
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
