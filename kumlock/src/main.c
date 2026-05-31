#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include "ext-session-lock-v1-client-protocol.h"

#define FONT        "monospace"
#define BG_R        0.06f
#define BG_G        0.06f
#define BG_B        0.09f
#define FG_R        0.85f
#define FG_G        0.85f
#define FG_B        0.90f
#define ERR_R       0.90f
#define ERR_G       0.30f
#define ERR_B       0.30f
#define DOT_R       0.45f
#define DOT_G       0.60f
#define DOT_B       0.95f
#define INPUT_MAX   256

static struct {
    struct wl_display                  *display;
    struct wl_registry                 *registry;
    struct wl_compositor               *compositor;
    struct wl_shm                      *shm;
    struct wl_seat                     *seat;
    struct wl_keyboard                 *keyboard;
    struct ext_session_lock_manager_v1 *lock_manager;
    struct ext_session_lock_v1         *lock;
    struct wl_list                      surfaces;
    struct xkb_context                 *xkb_ctx;
    struct xkb_keymap                  *xkb_map;
    struct xkb_state                   *xkb_state;
    bool                                running;
    bool                                locked;
    char                                input[INPUT_MAX];
    int                                 input_len;
    bool                                error;
    char                                username[64];
} app;

struct lock_surface {
    struct wl_list                      link;
    struct wl_output                   *wl_output;
    struct wl_surface                  *surface;
    struct ext_session_lock_surface_v1 *lock_surface;
    struct wl_buffer                   *buffer;
    int                                 width;
    int                                 height;
    bool                                configured;
};

static int create_shm_file(size_t size)
{
    char name[] = "/kumlock-XXXXXX";
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

static void render_surface(struct lock_surface *ls)
{
    if (!ls->configured || ls->width == 0)
        return;

    int stride = ls->width * 4;
    size_t size = (size_t)stride * ls->height;

    int fd = create_shm_file(size);
    if (fd < 0)
        return;

    uint8_t *data = mmap(NULL, size,
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(app.shm, fd, (int32_t)size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0,
        ls->width, ls->height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        data, CAIRO_FORMAT_ARGB32, ls->width, ls->height, stride);
    cairo_t *cr = cairo_create(cs);

    cairo_set_source_rgb(cr, BG_R, BG_G, BG_B);
    cairo_paint(cr);

    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char timebuf[16], datebuf[32];
    strftime(timebuf, sizeof(timebuf), "%H:%M", tm_info);
    strftime(datebuf, sizeof(datebuf), "%A, %d %B", tm_info);

    cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 72.0);
    cairo_set_source_rgb(cr, FG_R, FG_G, FG_B);

    cairo_text_extents_t ext;
    cairo_text_extents(cr, timebuf, &ext);
    cairo_move_to(cr,
        (ls->width  - ext.width)  / 2.0 - ext.x_bearing,
        ls->height  * 0.38        - ext.y_bearing / 2.0);
    cairo_show_text(cr, timebuf);

    cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 20.0);
    cairo_set_source_rgba(cr, FG_R, FG_G, FG_B, 0.50);
    cairo_text_extents(cr, datebuf, &ext);
    cairo_move_to(cr,
        (ls->width - ext.width) / 2.0 - ext.x_bearing,
        ls->height * 0.38 + 56.0);
    cairo_show_text(cr, datebuf);

    if (app.username[0]) {
        char ubuf[80];
        snprintf(ubuf, sizeof(ubuf), "%s", app.username);
        cairo_set_font_size(cr, 15.0);
        cairo_set_source_rgba(cr, FG_R, FG_G, FG_B, 0.35);
        cairo_text_extents(cr, ubuf, &ext);
        cairo_move_to(cr,
            (ls->width - ext.width) / 2.0 - ext.x_bearing,
            ls->height * 0.38 + 84.0);
        cairo_show_text(cr, ubuf);
    }

    int dot_count = app.input_len < 32 ? app.input_len : 32;
    int dot_r   = 6;
    int dot_gap = 18;
    int total   = dot_count > 0
        ? dot_count * (dot_r * 2) + (dot_count - 1) * (dot_gap - dot_r * 2)
        : 0;
    int ox = (ls->width - total) / 2;
    int oy = (int)(ls->height * 0.56);

    for (int i = 0; i < dot_count; i++) {
        int cx = ox + i * dot_gap + dot_r;
        if (app.error)
            cairo_set_source_rgb(cr, ERR_R, ERR_G, ERR_B);
        else
            cairo_set_source_rgb(cr, DOT_R, DOT_G, DOT_B);
        cairo_arc(cr, cx, oy, dot_r, 0, 2.0 * 3.14159265);
        cairo_fill(cr);
    }

    if (dot_count == 0) {
        const char *hint = app.error ? "incorrect password" : "enter password";
        cairo_set_font_size(cr, 14.0);
        if (app.error)
            cairo_set_source_rgba(cr, ERR_R, ERR_G, ERR_B, 0.85);
        else
            cairo_set_source_rgba(cr, FG_R, FG_G, FG_B, 0.28);
        cairo_text_extents(cr, hint, &ext);
        cairo_move_to(cr,
            (ls->width - ext.width) / 2.0 - ext.x_bearing,
            oy - ext.y_bearing / 2.0);
        cairo_show_text(cr, hint);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(cs);

    if (ls->buffer)
        wl_buffer_destroy(ls->buffer);
    ls->buffer = buf;

    wl_surface_attach(ls->surface, buf, 0, 0);
    wl_surface_damage(ls->surface, 0, 0, ls->width, ls->height);
    wl_surface_commit(ls->surface);

    munmap(data, size);
}

static void render_all(void)
{
    struct lock_surface *ls;
    wl_list_for_each(ls, &app.surfaces, link)
        render_surface(ls);
}

static int pam_conversation(int num_msg, const struct pam_message **msg,
    struct pam_response **resp, void *appdata_ptr)
{
    struct pam_response *r = calloc(num_msg, sizeof(struct pam_response));
    if (!r)
        return PAM_CONV_ERR;

    for (int i = 0; i < num_msg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
            msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            r[i].resp = strdup((const char *)appdata_ptr);
        }
    }

    *resp = r;
    return PAM_SUCCESS;
}

static bool verify_password(const char *password)
{
    struct pam_conv conv = {
        .conv        = pam_conversation,
        .appdata_ptr = (void *)password,
    };

    pam_handle_t *handle = NULL;
    int ret = pam_start("login", app.username, &conv, &handle);
    if (ret != PAM_SUCCESS) {
        pam_end(handle, ret);
        return false;
    }

    ret = pam_authenticate(handle, PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);
    pam_end(handle, ret);
    return ret == PAM_SUCCESS;
}

static void attempt_unlock(void)
{
    if (verify_password(app.input)) {
        ext_session_lock_v1_unlock_and_destroy(app.lock);
        app.lock    = NULL;
        app.running = false;
    } else {
        app.error     = true;
        app.input_len = 0;
        app.input[0]  = '\0';
        render_all();
    }
}

static void handle_key(uint32_t keycode, uint32_t state)
{
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    uint32_t code = keycode + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkb_state, code);

    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        attempt_unlock();
        break;
    case XKB_KEY_BackSpace:
        if (app.input_len > 0) {
            app.input[--app.input_len] = '\0';
            app.error = false;
            render_all();
        }
        break;
    case XKB_KEY_Escape:
        app.input_len = 0;
        app.input[0]  = '\0';
        app.error     = false;
        render_all();
        break;
    default: {
        char buf[8];
        int len = xkb_state_key_get_utf8(app.xkb_state, code, buf, sizeof(buf));
        if (len > 0 && app.input_len + len < INPUT_MAX - 1) {
            memcpy(app.input + app.input_len, buf, len);
            app.input_len += len;
            app.input[app.input_len] = '\0';
            app.error = false;
            render_all();
        }
        break;
    }
    }
}

static void kb_keymap(void *data, struct wl_keyboard *kb,
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
    handle_key(key, state);
}

static const struct wl_keyboard_listener kb_listener = {
    .keymap      = kb_keymap,
    .enter       = kb_enter,
    .leave       = kb_leave,
    .key         = kb_key,
    .modifiers   = kb_modifiers,
    .repeat_info = kb_repeat_info,
};

static void seat_capabilities(void *d, struct wl_seat *seat, uint32_t caps)
{
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !app.keyboard) {
        app.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app.keyboard, &kb_listener, NULL);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name         = seat_name,
};

static void lock_surface_configure(void *data,
    struct ext_session_lock_surface_v1 *ls,
    uint32_t serial, uint32_t w, uint32_t h)
{
    struct lock_surface *surface = data;
    surface->width      = (int)w;
    surface->height     = (int)h;
    surface->configured = true;
    ext_session_lock_surface_v1_ack_configure(ls, serial);
    render_surface(surface);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
    .configure = lock_surface_configure,
};

static void session_lock_locked(void *data,
    struct ext_session_lock_v1 *lock)
{
    app.locked = true;
    render_all();
}

static void session_lock_finished(void *data,
    struct ext_session_lock_v1 *lock)
{
    app.running = false;
}

static const struct ext_session_lock_v1_listener lock_listener = {
    .locked   = session_lock_locked,
    .finished = session_lock_finished,
};

static void output_create_lock_surface(struct lock_surface *ls)
{
    if (!app.lock)
        return;
    ls->surface = wl_compositor_create_surface(app.compositor);
    ls->lock_surface = ext_session_lock_v1_get_lock_surface(
        app.lock, ls->surface, ls->wl_output);
    ext_session_lock_surface_v1_add_listener(ls->lock_surface,
        &lock_surface_listener, ls);
    wl_surface_commit(ls->surface);
}

static void wl_output_done(void *data, struct wl_output *wl_out)
{
    output_create_lock_surface(data);
}

static void wl_output_geometry(void *d, struct wl_output *o,
    int x, int y, int pw, int ph, int sp,
    const char *mk, const char *mo, int t) {}
static void wl_output_mode(void *d, struct wl_output *o,
    uint32_t f, int w, int h, int r) {}
static void wl_output_scale(void *d, struct wl_output *o, int32_t s) {}
static void wl_output_name(void *d, struct wl_output *o, const char *n) {}
static void wl_output_description(void *d, struct wl_output *o, const char *dc) {}

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
        app.compositor = wl_registry_bind(reg, name,
            &wl_compositor_interface, 4);
    else if (strcmp(iface, wl_shm_interface.name) == 0)
        app.shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (strcmp(iface, wl_seat_interface.name) == 0) {
        app.seat = wl_registry_bind(reg, name, &wl_seat_interface, 7);
        wl_seat_add_listener(app.seat, &seat_listener, NULL);
    } else if (strcmp(iface, ext_session_lock_manager_v1_interface.name) == 0) {
        app.lock_manager = wl_registry_bind(reg, name,
            &ext_session_lock_manager_v1_interface, 1);
    } else if (strcmp(iface, wl_output_interface.name) == 0) {
        struct lock_surface *ls = calloc(1, sizeof(*ls));
        ls->wl_output = wl_registry_bind(reg, name, &wl_output_interface, 4);
        wl_output_add_listener(ls->wl_output, &wl_output_listener, ls);
        wl_list_insert(&app.surfaces, &ls->link);
    }
}

static void registry_remove(void *d, struct wl_registry *r, uint32_t n) {}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_remove,
};

int main(int argc, char *argv[])
{
    memset(&app, 0, sizeof(app));
    wl_list_init(&app.surfaces);
    app.running = true;

    struct passwd *pw = getpwuid(getuid());
    if (pw)
        strncpy(app.username, pw->pw_name, sizeof(app.username) - 1);

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "kumlock: cannot connect to display\n");
        return 1;
    }

    app.xkb_ctx  = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, NULL);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    if (!app.lock_manager) {
        fprintf(stderr,
            "kumlock: compositor does not support ext-session-lock-v1\n");
        return 1;
    }

    app.lock = ext_session_lock_manager_v1_lock(app.lock_manager);
    ext_session_lock_v1_add_listener(app.lock, &lock_listener, NULL);

    struct lock_surface *ls;
    wl_list_for_each(ls, &app.surfaces, link)
        output_create_lock_surface(ls);

    wl_display_roundtrip(app.display);

    while (app.running && wl_display_dispatch(app.display) != -1)
        ;

    return 0;
}
