#include "kumbar.h"
#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

#include "xdg-output-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static int create_shm_file(size_t size)
{
    char name[] = "/kumbar-XXXXXX";
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

static struct wl_buffer *create_buffer(struct kumbar *bar,
    struct kumbar_output *output, uint8_t **data_out)
{
    int stride = output->width * 4;
    size_t size = (size_t)stride * KUMBAR_HEIGHT;

    int fd = create_shm_file(size);
    if (fd < 0)
        return NULL;

    uint8_t *data = mmap(NULL, size,
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(bar->shm, fd, (int32_t)size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0,
        output->width, KUMBAR_HEIGHT, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    *data_out = data;
    return buf;
}

static void render_background(cairo_t *cr, int width)
{
    cairo_set_source_rgba(cr,
        KUMBAR_BG_R, KUMBAR_BG_G, KUMBAR_BG_B, KUMBAR_ALPHA);
    cairo_rectangle(cr, 0, 0, width, KUMBAR_HEIGHT);
    cairo_fill(cr);
}

static void render_workspaces(cairo_t *cr, struct kumbar_output *output)
{
    int x       = KUMBAR_PADDING;
    int cell    = KUMBAR_HEIGHT - 8;

    for (int i = 0; i < KUMBAR_WS_COUNT; i++) {
        bool active = (i == output->active_ws);

        if (active) {
            cairo_set_source_rgba(cr,
                KUMBAR_ACC_R, KUMBAR_ACC_G, KUMBAR_ACC_B, 0.20f);
            cairo_rectangle(cr, x - 3, 3, cell + 6, cell + 2);
            cairo_fill(cr);

            cairo_set_source_rgba(cr,
                KUMBAR_ACC_R, KUMBAR_ACC_G, KUMBAR_ACC_B, KUMBAR_ALPHA);
        } else {
            cairo_set_source_rgba(cr,
                KUMBAR_FG_R, KUMBAR_FG_G, KUMBAR_FG_B, 0.30f);
        }

        cairo_select_font_face(cr, KUMBAR_FONT,
            CAIRO_FONT_SLANT_NORMAL,
            active ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, KUMBAR_FONT_SIZE - 1.0);

        char label[4];
        snprintf(label, sizeof(label), "%d", i + 1);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, label, &ext);

        double tx = x + (cell - ext.width) / 2.0 - ext.x_bearing;
        double ty = (KUMBAR_HEIGHT + ext.height) / 2.0 - ext.y_bearing;
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, label);

        x += cell + 6;
    }
}

static void render_clock(cairo_t *cr, int width)
{
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M  %a %d %b", tm_info);

    cairo_select_font_face(cr, KUMBAR_FONT,
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, KUMBAR_FONT_SIZE);
    cairo_set_source_rgba(cr,
        KUMBAR_FG_R, KUMBAR_FG_G, KUMBAR_FG_B, KUMBAR_ALPHA);

    cairo_text_extents_t ext;
    cairo_text_extents(cr, buf, &ext);

    double tx = width - ext.width - ext.x_bearing - KUMBAR_PADDING;
    double ty = (KUMBAR_HEIGHT + ext.height) / 2.0 - ext.y_bearing;
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, buf);
}

void kumbar_render_output(struct kumbar *bar, struct kumbar_output *output)
{
    if (!output->configured || output->width == 0)
        return;

    if (output->buffer) {
        wl_buffer_destroy(output->buffer);
        output->buffer = NULL;
    }

    uint8_t *data = NULL;
    output->buffer = create_buffer(bar, output, &data);
    if (!output->buffer)
        return;

    output->data = data;

    int stride = output->width * 4;
    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        data, CAIRO_FORMAT_ARGB32, output->width, KUMBAR_HEIGHT, stride);
    cairo_t *cr = cairo_create(cs);

    render_background(cr, output->width);
    render_workspaces(cr, output);
    render_clock(cr, output->width);

    cairo_destroy(cr);
    cairo_surface_destroy(cs);

    wl_surface_attach(output->surface, output->buffer, 0, 0);
    wl_surface_damage(output->surface, 0, 0, output->width, KUMBAR_HEIGHT);
    wl_surface_commit(output->surface);

    output->dirty = false;
}

void kumbar_ipc_connect(struct kumbar *bar)
{
    const char *path = getenv("KUMDE_IPC");
    if (!path)
        return;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return;
    }

    bar->ipc_fd = fd;
}

static void handle_ipc_message(struct kumbar *bar, const char *msg)
{
    char output_name[64] = {0};
    int  ws_index = -1;

    if (sscanf(msg, "{\"event\":\"workspace\",\"output\":\"%63[^\"]\",\"index\":%d}",
               output_name, &ws_index) == 2 && ws_index >= 0) {
        struct kumbar_output *o;
        wl_list_for_each(o, &bar->outputs, link) {
            if (strcmp(o->name, output_name) == 0) {
                o->active_ws = ws_index;
                o->dirty     = true;
                break;
            }
        }
    }
}

void kumbar_ipc_dispatch(struct kumbar *bar)
{
    if (bar->ipc_fd < 0)
        return;

    char tmp[KUMBAR_IPC_BUF];
    ssize_t n = recv(bar->ipc_fd, tmp, sizeof(tmp) - 1, MSG_DONTWAIT);
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(bar->ipc_fd);
            bar->ipc_fd = -1;
        }
        return;
    }
    tmp[n] = '\0';

    int remaining = (int)(sizeof(bar->ipc_ibuf) - bar->ipc_ibuf_len - 1);
    if (n > remaining)
        n = remaining;
    memcpy(bar->ipc_ibuf + bar->ipc_ibuf_len, tmp, n);
    bar->ipc_ibuf_len += (int)n;
    bar->ipc_ibuf[bar->ipc_ibuf_len] = '\0';

    char *start = bar->ipc_ibuf;
    char *nl;
    while ((nl = strchr(start, '\n')) != NULL) {
        *nl = '\0';
        handle_ipc_message(bar, start);
        start = nl + 1;
    }

    int leftover = (int)(bar->ipc_ibuf + bar->ipc_ibuf_len - start);
    if (leftover > 0)
        memmove(bar->ipc_ibuf, start, leftover);
    bar->ipc_ibuf_len = leftover;
    bar->ipc_ibuf[bar->ipc_ibuf_len] = '\0';
}

static void layer_surface_configure(void *data,
    struct zwlr_layer_surface_v1 *surface,
    uint32_t serial, uint32_t w, uint32_t h)
{
    struct kumbar_output *output = data;
    output->width      = (int)w;
    output->configured = true;
    output->dirty      = true;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
}

static void layer_surface_closed(void *data,
    struct zwlr_layer_surface_v1 *surface)
{
    struct kumbar_output *output = data;
    zwlr_layer_surface_v1_destroy(output->layer_surface);
    output->layer_surface = NULL;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed    = layer_surface_closed,
};

static void xdg_output_name(void *data,
    struct zxdg_output_v1 *xdg_output, const char *name)
{
    struct kumbar_output *output = data;
    strncpy(output->name, name, sizeof(output->name) - 1);
    zxdg_output_v1_destroy(xdg_output);
}

static void xdg_output_description(void *data,
    struct zxdg_output_v1 *xdg_output, const char *desc) {}
static void xdg_output_logical_position(void *data,
    struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y) {}
static void xdg_output_logical_size(void *data,
    struct zxdg_output_v1 *xdg_output, int32_t w, int32_t h) {}
static void xdg_output_done(void *data,
    struct zxdg_output_v1 *xdg_output) {}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_logical_position,
    .logical_size     = xdg_output_logical_size,
    .done             = xdg_output_done,
    .name             = xdg_output_name,
    .description      = xdg_output_description,
};

static void wl_output_geometry(void *data, struct wl_output *wl_out,
    int x, int y, int pw, int ph, int subpixel,
    const char *make, const char *model, int transform) {}
static void wl_output_mode(void *data, struct wl_output *wl_out,
    uint32_t flags, int w, int h, int refresh)
{
    struct kumbar_output *output = data;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        output->width  = w;
        output->height = h;
    }
}
static void wl_output_done(void *data, struct wl_output *wl_out) {}
static void wl_output_scale(void *data, struct wl_output *wl_out, int32_t s)
{
    ((struct kumbar_output *)data)->scale = s;
}
static void wl_output_name(void *data, struct wl_output *wl_out, const char *n)
{
    struct kumbar_output *output = data;
    strncpy(output->name, n, sizeof(output->name) - 1);
}
static void wl_output_description(void *data, struct wl_output *wl_out,
    const char *d) {}

static const struct wl_output_listener wl_output_listener = {
    .geometry    = wl_output_geometry,
    .mode        = wl_output_mode,
    .done        = wl_output_done,
    .scale       = wl_output_scale,
    .name        = wl_output_name,
    .description = wl_output_description,
};

static void setup_layer_surface(struct kumbar *bar,
    struct kumbar_output *output)
{
    output->surface = wl_compositor_create_surface(bar->compositor);
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        bar->layer_shell, output->surface, output->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "kumbar");

    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, KUMBAR_HEIGHT);
    zwlr_layer_surface_v1_set_anchor(output->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface,
        KUMBAR_HEIGHT);

    zwlr_layer_surface_v1_add_listener(output->layer_surface,
        &layer_surface_listener, output);

    wl_surface_commit(output->surface);
}

static void registry_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *iface, uint32_t version)
{
    struct kumbar *bar = data;

    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        bar->compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        bar->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        bar->layer_shell = wl_registry_bind(registry, name,
            &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(iface, zxdg_output_manager_v1_interface.name) == 0) {
        bar->xdg_output_manager = wl_registry_bind(registry, name,
            &zxdg_output_manager_v1_interface, 2);
    } else if (strcmp(iface, wl_output_interface.name) == 0) {
        struct kumbar_output *output = calloc(1, sizeof(*output));
        output->scale    = 1;
        output->active_ws = 0;
        output->dirty     = false;
        output->wl_output = wl_registry_bind(registry, name,
            &wl_output_interface, 4);
        wl_output_add_listener(output->wl_output, &wl_output_listener, output);
        wl_list_insert(&bar->outputs, &output->link);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
    uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

int main(int argc, char *argv[])
{
    struct kumbar bar = {0};
    wl_list_init(&bar.outputs);
    bar.running  = true;
    bar.ipc_fd   = -1;

    bar.display = wl_display_connect(NULL);
    if (!bar.display) {
        fprintf(stderr, "kumbar: cannot connect to display\n");
        return 1;
    }

    bar.registry = wl_display_get_registry(bar.display);
    wl_registry_add_listener(bar.registry, &registry_listener, &bar);
    wl_display_roundtrip(bar.display);
    wl_display_roundtrip(bar.display);

    if (!bar.compositor || !bar.shm || !bar.layer_shell) {
        fprintf(stderr, "kumbar: missing required protocols\n");
        return 1;
    }

    struct kumbar_output *output;
    wl_list_for_each(output, &bar.outputs, link) {
        if (bar.xdg_output_manager) {
            struct zxdg_output_v1 *xdg_out =
                zxdg_output_manager_v1_get_xdg_output(
                    bar.xdg_output_manager, output->wl_output);
            zxdg_output_v1_add_listener(xdg_out, &xdg_output_listener, output);
        }
        setup_layer_surface(&bar, output);
    }

    wl_display_roundtrip(bar.display);

    kumbar_ipc_connect(&bar);

    wl_list_for_each(output, &bar.outputs, link)
        kumbar_render_output(&bar, output);

    wl_display_flush(bar.display);

    int wl_fd = wl_display_get_fd(bar.display);

    while (bar.running) {
        struct pollfd fds[2];
        int nfds = 0;

        fds[nfds].fd     = wl_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        if (bar.ipc_fd >= 0) {
            fds[nfds].fd     = bar.ipc_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        int ret = poll(fds, nfds, 1000);

        if (ret < 0)
            break;

        if (fds[0].revents & POLLIN)
            wl_display_dispatch(bar.display);

        if (nfds > 1 && (fds[1].revents & POLLIN)) {
            kumbar_ipc_dispatch(&bar);
            wl_list_for_each(output, &bar.outputs, link) {
                if (output->dirty)
                    kumbar_render_output(&bar, output);
            }
        }

        if (ret == 0) {
            wl_list_for_each(output, &bar.outputs, link)
                kumbar_render_output(&bar, output);
        }

        wl_display_flush(bar.display);
    }

    return 0;
}
