#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#define CLIP_MAX      (8 * 1024 * 1024)
#define MIME_TEXT     "text/plain;charset=utf-8"
#define MIME_ALT      "text/plain"
#define MIME_UTF8     "UTF8_STRING"
#define MIME_STRING   "STRING"

typedef struct { char *data; size_t len; char mime[128]; } clip_t;

static struct {
    struct wl_display                 *display;
    struct wl_registry                *registry;
    struct wl_seat                    *seat;
    struct wl_data_device_manager     *ddm;
    struct wl_data_device             *dd;
    struct wl_data_source             *source;
    clip_t   held;
    bool     running;
    bool     is_owner;
} app;

static void clip_free(clip_t *c)
{ free(c->data); c->data = NULL; c->len = 0; c->mime[0] = '\0'; }

static bool clip_read(struct wl_data_offer *offer, const char *mime,
    clip_t *out)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) return false;
    wl_data_offer_receive(offer, mime, fds[1]);
    close(fds[1]);
    wl_display_flush(app.display);

    char *buf = malloc(CLIP_MAX);
    if (!buf) { close(fds[0]); return false; }
    size_t total = 0;
    while (total < (size_t)CLIP_MAX) {
        ssize_t n = read(fds[0], buf + total, CLIP_MAX - total);
        if (n <= 0) break;
        total += n;
    }
    close(fds[0]);
    if (!total) { free(buf); return false; }
    clip_free(out);
    out->data = buf; out->len = total;
    strncpy(out->mime, mime, sizeof(out->mime) - 1);
    return true;
}

static void source_send(void *data, struct wl_data_source *src,
    const char *mime, int32_t fd)
{
    clip_t *c = data;
    if (!c->data) { close(fd); return; }
    size_t w = 0;
    while (w < c->len) {
        ssize_t n = write(fd, c->data + w, c->len - w);
        if (n <= 0) break;
        w += n;
    }
    close(fd);
}

static void source_cancelled(void *data, struct wl_data_source *src)
{
    app.is_owner = false;
    wl_data_source_destroy(src);
    if (app.source == src) app.source = NULL;
}

static void src_target(void *d, struct wl_data_source *s, const char *m) {}
static void src_dnd_drop(void *d, struct wl_data_source *s) {}
static void src_dnd_finished(void *d, struct wl_data_source *s) {}
static void src_action(void *d, struct wl_data_source *s, uint32_t a) {}

static const struct wl_data_source_listener source_listener = {
    .target = src_target, .send = source_send,
    .cancelled = source_cancelled, .dnd_drop_performed = src_dnd_drop,
    .dnd_finished = src_dnd_finished, .action = src_action,
};

static void claim(void)
{
    if (app.is_owner || !app.held.data || !app.dd) return;
    if (app.source) wl_data_source_destroy(app.source);
    app.source = wl_data_device_manager_create_data_source(app.ddm);
    wl_data_source_add_listener(app.source, &source_listener, &app.held);
    wl_data_source_offer(app.source, MIME_TEXT);
    wl_data_source_offer(app.source, MIME_ALT);
    wl_data_source_offer(app.source, MIME_UTF8);
    wl_data_source_offer(app.source, MIME_STRING);
    wl_data_device_set_selection(app.dd, app.source, 0);
    app.is_owner = true;
}

static struct { struct wl_data_offer *offer; bool has_text; } pending;

static void offer_mime(void *data, struct wl_data_offer *o, const char *mime)
{
    bool *ht = data;
    if (!strcmp(mime, MIME_TEXT) || !strcmp(mime, MIME_ALT) ||
        !strcmp(mime, MIME_UTF8) || !strcmp(mime, MIME_STRING))
        *ht = true;
}

static void offer_src_actions(void *d, struct wl_data_offer *o, uint32_t a) {}
static void offer_action(void *d, struct wl_data_offer *o, uint32_t a) {}

static const struct wl_data_offer_listener offer_listener = {
    .offer = offer_mime, .source_actions = offer_src_actions,
    .action = offer_action,
};

static void dd_offer(void *d, struct wl_data_device *dd,
    struct wl_data_offer *offer)
{
    if (pending.offer) wl_data_offer_destroy(pending.offer);
    pending.offer = offer; pending.has_text = false;
    wl_data_offer_add_listener(offer, &offer_listener, &pending.has_text);
}

static void dd_selection(void *d, struct wl_data_device *dd,
    struct wl_data_offer *offer)
{
    if (app.is_owner) return;
    if (!offer) return;
    if (!pending.has_text) { wl_data_offer_destroy(offer); pending.offer = NULL; return; }
    clip_t fresh = {0};
    bool ok = clip_read(offer, MIME_TEXT, &fresh);
    if (!ok) ok = clip_read(offer, MIME_ALT, &fresh);
    if (!ok) ok = clip_read(offer, MIME_UTF8, &fresh);
    if (!ok) ok = clip_read(offer, MIME_STRING, &fresh);
    wl_data_offer_destroy(offer); pending.offer = NULL;
    if (!ok) return;
    clip_free(&app.held); app.held = fresh;
    claim();
}

static void dd_enter(void *d, struct wl_data_device *dd, uint32_t s,
    struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y,
    struct wl_data_offer *o) {}
static void dd_leave(void *d, struct wl_data_device *dd) {}
static void dd_motion(void *d, struct wl_data_device *dd,
    uint32_t t, wl_fixed_t x, wl_fixed_t y) {}
static void dd_drop(void *d, struct wl_data_device *dd) {}

static const struct wl_data_device_listener dd_listener = {
    .data_offer = dd_offer, .enter = dd_enter, .leave = dd_leave,
    .motion = dd_motion, .drop = dd_drop, .selection = dd_selection,
};

static void seat_caps(void *d, struct wl_seat *seat, uint32_t caps)
{
    if (!app.dd && app.ddm) {
        app.dd = wl_data_device_manager_get_data_device(app.ddm, seat);
        wl_data_device_add_listener(app.dd, &dd_listener, NULL);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_caps, .name = seat_name,
};

static void reg_global(void *d, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t ver)
{
    if (!strcmp(iface, wl_seat_interface.name)) {
        app.seat = wl_registry_bind(reg, name, &wl_seat_interface, 7);
        wl_seat_add_listener(app.seat, &seat_listener, NULL);
    } else if (!strcmp(iface, wl_data_device_manager_interface.name)) {
        app.ddm = wl_registry_bind(reg, name,
            &wl_data_device_manager_interface, 3);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_remove,
};

int main(int argc, char *argv[])
{
    memset(&app, 0, sizeof(app));
    app.running = true;

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "kumclip: cannot connect to display\n");
        return 1;
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &reg_listener, NULL);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    if (!app.seat || !app.ddm) {
        fprintf(stderr, "kumclip: missing seat or data device manager\n");
        return 1;
    }

    struct pollfd pfd = { .fd = wl_display_get_fd(app.display), .events = POLLIN };
    while (app.running) {
        wl_display_flush(app.display);
        if (poll(&pfd, 1, -1) < 0) break;
        if (pfd.revents & POLLIN)
            if (wl_display_dispatch(app.display) < 0) break;
    }
    clip_free(&app.held);
    return 0;
}
