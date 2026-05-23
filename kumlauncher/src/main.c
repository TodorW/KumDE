#include <cairo/cairo.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define MAX_ENTRIES   512
#define MAX_QUERY     128
#define ITEM_HEIGHT   36
#define VISIBLE_ROWS  8
#define WIDTH         480
#define FONT          "monospace"
#define FONT_SIZE     14.0

#define BG_R   0.10f
#define BG_G   0.10f
#define BG_B   0.14f
#define FG_R   0.88f
#define FG_G   0.88f
#define FG_B   0.92f
#define HL_R   0.45f
#define HL_G   0.60f
#define HL_B   0.95f
#define IN_R   0.16f
#define IN_G   0.16f
#define IN_B   0.20f

static struct {
    struct wl_display               *display;
    struct wl_registry              *registry;
    struct wl_compositor            *compositor;
    struct wl_shm                   *shm;
    struct wl_seat                  *seat;
    struct wl_keyboard              *keyboard;
    struct zwlr_layer_shell_v1      *layer_shell;
    struct wl_surface               *surface;
    struct zwlr_layer_surface_v1    *layer_surface;
    struct wl_buffer                *buffer;
    uint8_t                         *data;
    struct xkb_context              *xkb_ctx;
    struct xkb_keymap               *xkb_map;
    struct xkb_state                *xkb_state;
    bool                             running;
    bool                             configured;
    int                              width;
    int                              height;
    char                             query[MAX_QUERY];
    int                              query_len;
    char                             entries[MAX_ENTRIES][256];
    int                              entry_count;
    int                              filtered[MAX_ENTRIES];
    int                              filtered_count;
    int                              selected;
    int                              scroll;
} app;

static int create_shm_file(size_t size)
{
    char name[] = "/kumlauncher-XXXXXX";
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

static void collect_path_entries(void)
{
    const char *path_env = getenv("PATH");
    if (!path_env)
        return;

    char path[4096];
    strncpy(path, path_env, sizeof(path) - 1);

    char *dir = strtok(path, ":");
    while (dir && app.entry_count < MAX_ENTRIES) {
        DIR *d = opendir(dir);
        if (!d) {
            dir = strtok(NULL, ":");
            continue;
        }
        struct dirent *ent;
        while ((ent = readdir(d)) && app.entry_count < MAX_ENTRIES) {
            if (ent->d_name[0] == '.')
                continue;
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
            struct stat st;
            if (stat(full, &st) == 0 && (st.st_mode & S_IXUSR)) {
                bool dup = false;
                for (int i = 0; i < app.entry_count; i++) {
                    if (strcmp(app.entries[i], ent->d_name) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                    strncpy(app.entries[app.entry_count++], ent->d_name,
                        255);
            }
        }
        closedir(d);
        dir = strtok(NULL, ":");
    }
}

static int entry_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static void filter_entries(void)
{
    app.filtered_count = 0;
    app.selected       = 0;
    app.scroll         = 0;

    for (int i = 0; i < app.entry_count; i++) {
        if (app.query_len == 0 ||
            strncasecmp(app.entries[i], app.query, app.query_len) == 0) {
            app.filtered[app.filtered_count++] = i;
        }
    }
    for (int i = 0; i < app.entry_count; i++) {
        if (app.query_len == 0)
            break;
        bool already = false;
        for (int j = 0; j < app.filtered_count; j++) {
            if (app.filtered[j] == i) { already = true; break; }
        }
        if (!already && strcasestr(app.entries[i], app.query))
            app.filtered[app.filtered_count++] = i;
    }
}

static void launch_selected(void)
{
    if (app.filtered_count == 0)
        return;

    const char *cmd = app.entries[app.filtered[app.selected]];

    if (fork() == 0) {
        setsid();
        execlp(cmd, cmd, NULL);
        _exit(1);
    }

    app.running = false;
}

static struct wl_buffer *make_buffer(int w, int h, uint8_t **data_out)
{
    int stride = w * 4;
    size_t size = (size_t)stride * h;
    int fd = create_shm_file(size);
    if (fd < 0)
        return NULL;

    uint8_t *data = mmap(NULL, size,
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(app.shm, fd, (int32_t)size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
        WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    *data_out = data;
    return buf;
}

static void render(void)
{
    if (!app.configured || app.width == 0)
        return;

    int rows    = app.filtered_count < VISIBLE_ROWS
                    ? app.filtered_count : VISIBLE_ROWS;
    int h       = ITEM_HEIGHT + rows * ITEM_HEIGHT + 2;
    app.height  = h;

    if (app.buffer)
        wl_buffer_destroy(app.buffer);

    uint8_t *data = NULL;
    app.buffer = make_buffer(app.width, h, &data);
    if (!app.buffer)
        return;
    app.data = data;

    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        data, CAIRO_FORMAT_ARGB32, app.width, h, app.width * 4);
    cairo_t *cr = cairo_create(cs);

    cairo_set_source_rgba(cr, BG_R, BG_G, BG_B, 0.96);
    cairo_rectangle(cr, 0, 0, app.width, h);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, IN_R, IN_G, IN_B, 1.0);
    cairo_rectangle(cr, 0, 0, app.width, ITEM_HEIGHT);
    cairo_fill(cr);

    cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, FONT_SIZE);
    cairo_set_source_rgba(cr, FG_R, FG_G, FG_B, 1.0);

    char display_query[MAX_QUERY + 2];
    snprintf(display_query, sizeof(display_query), "%s|", app.query);
    cairo_move_to(cr, 12, ITEM_HEIGHT * 0.65);
    cairo_show_text(cr, display_query);

    for (int i = 0; i < rows; i++) {
        int idx = app.scroll + i;
        if (idx >= app.filtered_count)
            break;

        int y = ITEM_HEIGHT + i * ITEM_HEIGHT;
        bool sel = (idx == app.selected);

        if (sel) {
            cairo_set_source_rgba(cr, HL_R, HL_G, HL_B, 0.18);
            cairo_rectangle(cr, 0, y, app.width, ITEM_HEIGHT);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, HL_R, HL_G, HL_B, 1.0);
        } else {
            cairo_set_source_rgba(cr, FG_R, FG_G, FG_B, 0.80);
        }

        cairo_move_to(cr, 12, y + ITEM_HEIGHT * 0.65);
        cairo_show_text(cr, app.entries[app.filtered[idx]]);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(cs);

    zwlr_layer_surface_v1_set_size(app.layer_surface, app.width, h);
    wl_surface_attach(app.surface, app.buffer, 0, 0);
    wl_surface_damage(app.surface, 0, 0, app.width, h);
    wl_surface_commit(app.surface);
}

static void handle_key(uint32_t keycode, uint32_t state)
{
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    uint32_t code = keycode + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkb_state, code);

    switch (sym) {
    case XKB_KEY_Escape:
        app.running = false;
        break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        launch_selected();
        break;
    case XKB_KEY_BackSpace:
        if (app.query_len > 0) {
            app.query[--app.query_len] = '\0';
            filter_entries();
            render();
        }
        break;
    case XKB_KEY_Up:
        if (app.selected > 0) {
            app.selected--;
            if (app.selected < app.scroll)
                app.scroll = app.selected;
            render();
        }
        break;
    case XKB_KEY_Down:
        if (app.selected < app.filtered_count - 1) {
            app.selected++;
            if (app.selected >= app.scroll + VISIBLE_ROWS)
                app.scroll = app.selected - VISIBLE_ROWS + 1;
            render();
        }
        break;
    default: {
        char buf[8];
        int len = xkb_state_key_get_utf8(app.xkb_state, code, buf, sizeof(buf));
        if (len > 0 && app.query_len + len < MAX_QUERY - 1) {
            memcpy(app.query + app.query_len, buf, len);
            app.query_len += len;
            app.query[app.query_len] = '\0';
            filter_entries();
            render();
        }
        break;
    }
    }
}

static void kb_keymap(void *data, struct wl_keyboard *kb,
    uint32_t fmt, int32_t fd, uint32_t size)
{
    char *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return;
    }
    if (app.xkb_map)
        xkb_keymap_unref(app.xkb_map);
    if (app.xkb_state)
        xkb_state_unref(app.xkb_state);

    app.xkb_map   = xkb_keymap_new_from_string(app.xkb_ctx, map,
        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    app.xkb_state = xkb_state_new(app.xkb_map);
    munmap(map, size);
    close(fd);
}

static void kb_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
    struct wl_surface *surface, struct wl_array *keys) {}
static void kb_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
    struct wl_surface *surface) {}
static void kb_modifiers(void *data, struct wl_keyboard *kb,
    uint32_t serial, uint32_t mods_depressed,
    uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
    if (app.xkb_state)
        xkb_state_update_mask(app.xkb_state,
            mods_depressed, mods_latched, mods_locked, 0, 0, group);
}
static void kb_repeat_info(void *data, struct wl_keyboard *kb,
    int32_t rate, int32_t delay) {}
static void kb_key(void *data, struct wl_keyboard *kb, uint32_t serial,
    uint32_t time, uint32_t key, uint32_t state)
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

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !app.keyboard) {
        app.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app.keyboard, &kb_listener, NULL);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {}
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name         = seat_name,
};

static void layer_configure(void *data,
    struct zwlr_layer_surface_v1 *ls,
    uint32_t serial, uint32_t w, uint32_t h)
{
    app.width      = (int)w ? (int)w : WIDTH;
    app.configured = true;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    render();
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    app.running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_configure,
    .closed    = layer_closed,
};

static void registry_global(void *data, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t version)
{
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        app.compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (strcmp(iface, wl_shm_interface.name) == 0)
        app.shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (strcmp(iface, wl_seat_interface.name) == 0) {
        app.seat = wl_registry_bind(reg, name, &wl_seat_interface, 7);
        wl_seat_add_listener(app.seat, &seat_listener, NULL);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0)
        app.layer_shell = wl_registry_bind(reg, name,
            &zwlr_layer_shell_v1_interface, 1);
}

static void registry_remove(void *data, struct wl_registry *reg,
    uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_remove,
};

int main(int argc, char *argv[])
{
    memset(&app, 0, sizeof(app));
    app.running = true;
    app.width   = WIDTH;

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "kumlauncher: cannot connect to display\n");
        return 1;
    }

    app.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, NULL);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.shm || !app.layer_shell) {
        fprintf(stderr, "kumlauncher: missing required protocols\n");
        return 1;
    }

    collect_path_entries();
    qsort(app.entries, app.entry_count, sizeof(app.entries[0]), entry_cmp);
    filter_entries();

    app.surface = wl_compositor_create_surface(app.compositor);
    app.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        app.layer_shell, app.surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "kumlauncher");

    zwlr_layer_surface_v1_set_size(app.layer_surface, WIDTH, 0);
    zwlr_layer_surface_v1_set_anchor(app.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
    zwlr_layer_surface_v1_set_keyboard_interactivity(app.layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_add_listener(app.layer_surface,
        &layer_listener, NULL);
    wl_surface_commit(app.surface);

    while (app.running && wl_display_dispatch(app.display) != -1)
        ;

    return 0;
}
