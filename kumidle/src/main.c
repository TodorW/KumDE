#include <stdbool.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <wayland-client-protocol.h>

#include "ext-idle-notify-v1-client-protocol.h"

#define DEFAULT_IDLE_S   300
#define DEFAULT_LOCK_S   310
#define DEFAULT_DPMS_S   330

static struct {
    struct wl_display            *display;
    struct wl_registry           *registry;
    struct wl_seat               *seat;
    struct ext_idle_notifier_v1  *notifier;
    struct ext_idle_notification_v1 *idle_n, *lock_n, *dpms_n;
    uint32_t idle_ms, lock_ms, dpms_ms;
    char lock_cmd[256], idle_cmd[256], resume_cmd[256];
    bool running;
} app;

static void run_cmd(const char *cmd)
{
    if (!cmd || !cmd[0]) return;
    if (fork() == 0) { setsid(); execl("/bin/sh","sh","-c",cmd,NULL); _exit(1); }
}

static void notif_idled(void *data, struct ext_idle_notification_v1 *n)
{
    if (n == app.idle_n) run_cmd(app.idle_cmd);
    else if (n == app.lock_n) run_cmd(app.lock_cmd);
    else if (n == app.dpms_n) {
        FILE *f = popen("wlr-randr --output all --off 2>/dev/null || true","r");
        if (f) pclose(f);
    }
}

static void notif_resumed(void *data, struct ext_idle_notification_v1 *n)
{
    if (n == app.dpms_n) {
        FILE *f = popen("wlr-randr --output all --on 2>/dev/null || true","r");
        if (f) pclose(f);
    }
    if (n == app.idle_n) run_cmd(app.resume_cmd);
}

static const struct ext_idle_notification_v1_listener notif_listener = {
    .idled = notif_idled, .resumed = notif_resumed,
};

static void seat_caps(void *d, struct wl_seat *s, uint32_t c) {}
static void seat_name(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_caps, .name = seat_name,
};

static void reg_global(void *d, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t ver)
{
    if (!strcmp(iface, wl_seat_interface.name)) {
        app.seat = wl_registry_bind(reg, name, &wl_seat_interface, 1);
        wl_seat_add_listener(app.seat, &seat_listener, NULL);
    } else if (!strcmp(iface, ext_idle_notifier_v1_interface.name)) {
        app.notifier = wl_registry_bind(reg, name,
            &ext_idle_notifier_v1_interface, 1);
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
    app.idle_ms = DEFAULT_IDLE_S * 1000;
    app.lock_ms = DEFAULT_LOCK_S * 1000;
    app.dpms_ms = DEFAULT_DPMS_S * 1000;
    strncpy(app.lock_cmd, "kumlock", sizeof(app.lock_cmd) - 1);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i],"--idle-s")    && i+1<argc) app.idle_ms  = (uint32_t)(atoi(argv[++i])*1000);
        else if (!strcmp(argv[i],"--lock-s")    && i+1<argc) app.lock_ms  = (uint32_t)(atoi(argv[++i])*1000);
        else if (!strcmp(argv[i],"--dpms-s")    && i+1<argc) app.dpms_ms  = (uint32_t)(atoi(argv[++i])*1000);
        else if (!strcmp(argv[i],"--lock-cmd")  && i+1<argc) strncpy(app.lock_cmd,  argv[++i], 255);
        else if (!strcmp(argv[i],"--idle-cmd")  && i+1<argc) strncpy(app.idle_cmd,  argv[++i], 255);
        else if (!strcmp(argv[i],"--resume-cmd")&& i+1<argc) strncpy(app.resume_cmd,argv[++i], 255);
        else { fprintf(stderr,"kumidle: unknown arg %s\n",argv[i]); return 1; }
    }

    app.display = wl_display_connect(NULL);
    if (!app.display) { fprintf(stderr,"kumidle: cannot connect\n"); return 1; }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &reg_listener, NULL);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    if (!app.notifier) {
        fprintf(stderr,"kumidle: ext-idle-notify-v1 not supported\n"); return 1;
    }

    if (app.idle_ms > 0 && app.idle_cmd[0]) {
        app.idle_n = ext_idle_notifier_v1_get_idle_notification(
            app.notifier, app.idle_ms, app.seat);
        ext_idle_notification_v1_add_listener(app.idle_n, &notif_listener, NULL);
    }
    if (app.lock_ms > 0) {
        app.lock_n = ext_idle_notifier_v1_get_idle_notification(
            app.notifier, app.lock_ms, app.seat);
        ext_idle_notification_v1_add_listener(app.lock_n, &notif_listener, NULL);
    }
    if (app.dpms_ms > 0) {
        app.dpms_n = ext_idle_notifier_v1_get_idle_notification(
            app.notifier, app.dpms_ms, app.seat);
        ext_idle_notification_v1_add_listener(app.dpms_n, &notif_listener, NULL);
    }

    struct pollfd pfd = { .fd = wl_display_get_fd(app.display), .events = POLLIN };
    while (app.running) {
        wl_display_flush(app.display);
        if (poll(&pfd, 1, -1) < 0) break;
        if (pfd.revents & POLLIN)
            if (wl_display_dispatch(app.display) < 0) break;
    }
    return 0;
}
