#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#include "wlr-data-control-unstable-v1-client-protocol.h"

#define CLIP_MAX      (32 * 1024 * 1024)
#define MIME_TEXT     "text/plain;charset=utf-8"
#define MIME_ALT      "text/plain"
#define MIME_UTF8     "UTF8_STRING"
#define MIME_STRING   "STRING"
#define MIME_PNG      "image/png"
#define MIME_JPEG     "image/jpeg"

typedef struct { char *data; size_t len; char mime[128]; } clip_t;

typedef struct {
    struct zwlr_data_control_offer_v1 *offer;
    bool has_text;
    char image_mime[32];
} offer_state_t;

static bool mime_is_text(const char *m)
{
    return !strcmp(m, MIME_TEXT) || !strcmp(m, MIME_ALT) ||
           !strcmp(m, MIME_UTF8) || !strcmp(m, MIME_STRING);
}

static struct {
    struct wl_display                     *display;
    struct wl_registry                    *registry;
    struct wl_seat                        *seat;
    struct zwlr_data_control_manager_v1   *ddm;
    struct zwlr_data_control_device_v1    *dd;
    struct zwlr_data_control_source_v1    *source;
    clip_t   held;
    bool     running;
    bool     is_owner;
} app;

static void clip_free(clip_t *c)
{ free(c->data); c->data = NULL; c->len = 0; c->mime[0] = '\0'; }

static bool clip_read(struct zwlr_data_control_offer_v1 *offer,
    const char *mime, clip_t *out)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) return false;
    zwlr_data_control_offer_v1_receive(offer, mime, fds[1]);
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

static void source_send(void *data, struct zwlr_data_control_source_v1 *src,
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

static void source_cancelled(void *data,
    struct zwlr_data_control_source_v1 *src)
{
    app.is_owner = false;
    zwlr_data_control_source_v1_destroy(src);
    if (app.source == src) app.source = NULL;
}

static const struct zwlr_data_control_source_v1_listener source_listener = {
    .send = source_send, .cancelled = source_cancelled,
};

static void claim(void)
{
    if (app.is_owner || !app.held.data || !app.dd) return;
    if (app.source) zwlr_data_control_source_v1_destroy(app.source);
    app.source = zwlr_data_control_manager_v1_create_data_source(app.ddm);
    zwlr_data_control_source_v1_add_listener(app.source, &source_listener,
        &app.held);
    if (mime_is_text(app.held.mime)) {
        zwlr_data_control_source_v1_offer(app.source, MIME_TEXT);
        zwlr_data_control_source_v1_offer(app.source, MIME_ALT);
        zwlr_data_control_source_v1_offer(app.source, MIME_UTF8);
        zwlr_data_control_source_v1_offer(app.source, MIME_STRING);
    } else {
        zwlr_data_control_source_v1_offer(app.source, app.held.mime);
    }
    zwlr_data_control_device_v1_set_selection(app.dd, app.source);
    app.is_owner = true;
}

static offer_state_t pending;

static void offer_mime(void *data, struct zwlr_data_control_offer_v1 *o,
    const char *mime)
{
    offer_state_t *p = data;
    if (mime_is_text(mime)) {
        p->has_text = true;
    } else if (!p->image_mime[0] &&
            (!strcmp(mime, MIME_PNG) || !strcmp(mime, MIME_JPEG))) {
        strncpy(p->image_mime, mime, sizeof(p->image_mime) - 1);
    }
}

static const struct zwlr_data_control_offer_v1_listener offer_listener = {
    .offer = offer_mime,
};

static void dd_data_offer(void *d, struct zwlr_data_control_device_v1 *dd,
    struct zwlr_data_control_offer_v1 *offer)
{
    if (pending.offer) zwlr_data_control_offer_v1_destroy(pending.offer);
    pending.offer = offer;
    pending.has_text = false;
    pending.image_mime[0] = '\0';
    zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, &pending);
}

static void dd_selection(void *d, struct zwlr_data_control_device_v1 *dd,
    struct zwlr_data_control_offer_v1 *offer)
{
    if (app.is_owner) return;
    if (!offer) return;
    if (!pending.has_text && !pending.image_mime[0]) {
        zwlr_data_control_offer_v1_destroy(offer); pending.offer = NULL;
        return;
    }
    clip_t fresh = {0};
    bool ok = false;
    if (pending.has_text) {
        ok = clip_read(offer, MIME_TEXT, &fresh);
        if (!ok) ok = clip_read(offer, MIME_ALT, &fresh);
        if (!ok) ok = clip_read(offer, MIME_UTF8, &fresh);
        if (!ok) ok = clip_read(offer, MIME_STRING, &fresh);
    }
    if (!ok && pending.image_mime[0])
        ok = clip_read(offer, pending.image_mime, &fresh);
    zwlr_data_control_offer_v1_destroy(offer); pending.offer = NULL;
    if (!ok) return;
    clip_free(&app.held); app.held = fresh;
    claim();
}

static void dd_finished(void *d, struct zwlr_data_control_device_v1 *dd) {}
static void dd_primary_selection(void *d,
    struct zwlr_data_control_device_v1 *dd,
    struct zwlr_data_control_offer_v1 *offer)
{
    if (offer) zwlr_data_control_offer_v1_destroy(offer);
}

static const struct zwlr_data_control_device_v1_listener dd_listener = {
    .data_offer = dd_data_offer, .selection = dd_selection,
    .finished = dd_finished, .primary_selection = dd_primary_selection,
};

static void seat_caps(void *d, struct wl_seat *seat, uint32_t caps)
{
    if (!app.dd && app.ddm) {
        app.dd = zwlr_data_control_manager_v1_get_data_device(app.ddm, seat);
        zwlr_data_control_device_v1_add_listener(app.dd, &dd_listener, NULL);
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
    } else if (!strcmp(iface, zwlr_data_control_manager_v1_interface.name)) {
        app.ddm = wl_registry_bind(reg, name,
            &zwlr_data_control_manager_v1_interface, 2);
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
        fprintf(stderr,
            "kumclip: missing seat or wlr-data-control-unstable-v1\n");
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
