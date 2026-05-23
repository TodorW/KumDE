#include <cairo/cairo.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static struct {
    struct wl_display          *display;
    struct wl_registry         *registry;
    struct wl_compositor       *compositor;
    struct wl_shm              *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_list              outputs;
    bool                        running;
    const char                 *image_path;
    float                       color[3];
    bool                        use_color;
} app;

struct wall_output {
    struct wl_list               link;
    struct wl_output            *wl_output;
    struct wl_surface           *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_buffer            *buffer;
    int                          width;
    int                          height;
    bool                         configured;
};

static int create_shm_file(size_t size)
{
    char name[] = "/kumwall-XXXXXX";
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return -1;
    shm_unlink(name);
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void render_output(struct wall_output *output)
{
    if (!output->configured || output->width == 0 || output->height == 0)
        return;

    int stride = output->width * 4;
    size_t size = (size_t)stride * output->height;

    int fd = create_shm_file(size);
    if (fd < 0)
        return;

    uint8_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(app.shm, fd, (int32_t)size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0,
        output->width, output->height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        data, CAIRO_FORMAT_RGB24, output->width, output->height, stride);
    cairo_t *cr = cairo_create(cs);

    if (!app.use_color && app.image_path) {
        cairo_surface_t *img = cairo_image_surface_create_from_png(app.image_path);
        if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
            int iw = cairo_image_surface_get_width(img);
            int ih = cairo_image_surface_get_height(img);
            double sx = (double)output->width  / iw;
            double sy = (double)output->height / ih;
            double scale = sx > sy ? sx : sy;
            double ox = (output->width  - iw * scale) / 2.0;
            double oy = (output->height - ih * scale) / 2.0;

            cairo_set_source_rgba(cr, 0, 0, 0, 1);
            cairo_paint(cr);
            cairo_translate(cr, ox, oy);
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, img, 0, 0);
            cairo_paint(cr);
        } else {
            cairo_set_source_rgb(cr, 0.08, 0.08, 0.12);
            cairo_paint(cr);
        }
        cairo_surface_destroy(img);
    } else {
        cairo_set_source_rgb(cr,
            app.color[0], app.color[1], app.color[2]);
        cairo_paint(cr);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(cs);

    if (output->buffer)
        wl_buffer_destroy(output->buffer);
    output->buffer = buf;

    wl_surface_attach(output->surface, buf, 0, 0);
    wl_surface_damage(output->surface, 0, 0,
        output->width, output->height);
    wl_surface_commit(output->surface);

    munmap(data, size);
}

static void layer_configure(void *data,
    struct zwlr_layer_surface_v1 *ls,
    uint32_t serial, uint32_t w, uint32_t h)
{
    struct wall_output *output = data;
    output->width      = (int)w;
    output->height     = (int)h;
    output->configured = true;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    render_output(output);
}

static void layer_closed(void *data,
    struct zwlr_layer_surface_v1 *ls) {}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_configure,
    .closed    = layer_closed,
};

static void wl_output_geometry(void *data, struct wl_output *o,
    int x, int y, int pw, int ph, int sp,
    const char *make, const char *model, int transform) {}
static void wl_output_mode(void *data, struct wl_output *o,
    uint32_t flags, int w, int h, int refresh) {}
static void wl_output_scale(void *data, struct wl_output *o, int32_t s) {}
static void wl_output_name(void *data, struct wl_output *o, const char *n) {}
static void wl_output_description(void *data, struct wl_output *o,
    const char *d) {}
static void wl_output_done(void *data, struct wl_output *wl_out)
{
    struct wall_output *output = data;

    output->surface = wl_compositor_create_surface(app.compositor);
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        app.layer_shell, output->surface, wl_out,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "kumwall");

    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(output->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP    |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT   |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
    zwlr_layer_surface_v1_add_listener(output->layer_surface,
        &layer_listener, output);
    wl_surface_commit(output->surface);
}

static const struct wl_output_listener wl_output_listener = {
    .geometry    = wl_output_geometry,
    .mode        = wl_output_mode,
    .done        = wl_output_done,
    .scale       = wl_output_scale,
    .name        = wl_output_name,
    .description = wl_output_description,
};

static void registry_global(void *data, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t version)
{
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        app.compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (strcmp(iface, wl_shm_interface.name) == 0)
        app.shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0)
        app.layer_shell = wl_registry_bind(reg, name,
            &zwlr_layer_shell_v1_interface, 1);
    else if (strcmp(iface, wl_output_interface.name) == 0) {
        struct wall_output *output = calloc(1, sizeof(*output));
        output->wl_output = wl_registry_bind(reg, name, &wl_output_interface, 4);
        wl_output_add_listener(output->wl_output, &wl_output_listener, output);
        wl_list_insert(&app.outputs, &output->link);
    }
}

static void registry_remove(void *data, struct wl_registry *reg,
    uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_remove,
};

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [-c R G B] [-i /path/to/image.png]\n"
        "  -c R G B    solid color wallpaper (0.0-1.0 each)\n"
        "  -i PATH     PNG image wallpaper (scaled to fill)\n",
        argv0);
}

int main(int argc, char *argv[])
{
    memset(&app, 0, sizeof(app));
    wl_list_init(&app.outputs);
    app.running = true;

    app.color[0] = 0.08f;
    app.color[1] = 0.08f;
    app.color[2] = 0.12f;
    app.use_color = true;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 3 < argc) {
            app.color[0] = (float)atof(argv[++i]);
            app.color[1] = (float)atof(argv[++i]);
            app.color[2] = (float)atof(argv[++i]);
            app.use_color = true;
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            app.image_path = argv[++i];
            app.use_color  = false;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "kumwall: cannot connect to display\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, NULL);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    while (app.running && wl_display_dispatch(app.display) != -1)
        ;

    return 0;
}
