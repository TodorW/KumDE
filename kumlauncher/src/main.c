#include <cairo/cairo.h>
#include <ctype.h>
#include <dirent.h>
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

#define MAX_ENTRIES   1024
#define MAX_QUERY     128
#define ITEM_HEIGHT   38
#define VISIBLE_ROWS  10
#define WIDTH         520
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
#define IN_R   0.15f
#define IN_G   0.15f
#define IN_B   0.19f
#define DIM_R  0.55f
#define DIM_G  0.55f
#define DIM_B  0.60f

typedef struct {
    char name[256];
    char exec[512];
    char comment[256];
    bool from_desktop;
} Entry;

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
    struct xkb_context              *xkb_ctx;
    struct xkb_keymap               *xkb_map;
    struct xkb_state                *xkb_state;
    bool                             running;
    bool                             configured;
    int                              width;
    char                             query[MAX_QUERY];
    int                              query_len;
    Entry                            entries[MAX_ENTRIES];
    int                              entry_count;
    int                              filtered[MAX_ENTRIES];
    int                              filtered_count;
    int                              selected;
    int                              scroll;
} app;

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1)))
        end--;
    *end = '\0';
    return s;
}

static void strip_field_codes(char *exec)
{
    char out[512];
    int  j = 0;
    for (int i = 0; exec[i] && j < 510; i++) {
        if (exec[i] == '%' && exec[i + 1]) {
            i++;
            continue;
        }
        out[j++] = exec[i];
    }
    out[j] = '\0';
    strcpy(exec, trim(out));
}

static void parse_desktop_file(const char *path)
{
    if (app.entry_count >= MAX_ENTRIES)
        return;

    FILE *f = fopen(path, "r");
    if (!f)
        return;

    char line[512];
    bool in_entry = false;
    bool no_display = false;
    Entry e;
    memset(&e, 0, sizeof(e));
    e.from_desktop = true;

    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);

        if (s[0] == '[') {
            if (strncmp(s, "[Desktop Entry]", 15) == 0)
                in_entry = true;
            else if (in_entry)
                break;
            continue;
        }

        if (!in_entry || s[0] == '#')
            continue;

        char *eq = strchr(s, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (strcmp(key, "Name") == 0 && !e.name[0])
            strncpy(e.name, val, sizeof(e.name) - 1);
        else if (strcmp(key, "Exec") == 0 && !e.exec[0])
            strncpy(e.exec, val, sizeof(e.exec) - 1);
        else if (strcmp(key, "Comment") == 0 && !e.comment[0])
            strncpy(e.comment, val, sizeof(e.comment) - 1);
        else if (strcmp(key, "NoDisplay") == 0)
            no_display = strcmp(val, "true") == 0;
        else if (strcmp(key, "Type") == 0 && strcmp(val, "Application") != 0) {
            fclose(f);
            return;
        }
    }

    fclose(f);

    if (!in_entry || !e.name[0] || !e.exec[0] || no_display)
        return;

    strip_field_codes(e.exec);

    for (int i = 0; i < app.entry_count; i++) {
        if (strcmp(app.entries[i].name, e.name) == 0)
            return;
    }

    app.entries[app.entry_count++] = e;
}

static void collect_desktop_entries(void)
{
    const char *xdg_data_dirs = getenv("XDG_DATA_DIRS");
    if (!xdg_data_dirs)
        xdg_data_dirs = "/usr/local/share:/usr/share";

    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    char home_apps[512] = {0};
    if (xdg_data_home) {
        snprintf(home_apps, sizeof(home_apps), "%s/applications", xdg_data_home);
    } else {
        const char *home = getenv("HOME");
        if (home)
            snprintf(home_apps, sizeof(home_apps),
                "%s/.local/share/applications", home);
    }

    char dirs[4096];
    if (home_apps[0])
        snprintf(dirs, sizeof(dirs), "%s:%s", home_apps, xdg_data_dirs);
    else
        strncpy(dirs, xdg_data_dirs, sizeof(dirs) - 1);

    char *token = strtok(dirs, ":");
    while (token && app.entry_count < MAX_ENTRIES) {
        char appdir[512];
        snprintf(appdir, sizeof(appdir), "%s/applications", token);

        DIR *d = opendir(appdir);
        if (!d) {
            token = strtok(NULL, ":");
            continue;
        }

        struct dirent *ent;
        while ((ent = readdir(d)) && app.entry_count < MAX_ENTRIES) {
            size_t len = strlen(ent->d_name);
            if (len < 9 || strcmp(ent->d_name + len - 8, ".desktop") != 0)
                continue;
            char full[768];
            snprintf(full, sizeof(full), "%s/%s", appdir, ent->d_name);
            parse_desktop_file(full);
        }
        closedir(d);
        token = strtok(NULL, ":");
    }
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

            bool dup = false;
            for (int i = 0; i < app.entry_count; i++) {
                const char *cmp = app.entries[i].from_desktop
                    ? app.entries[i].exec
                    : app.entries[i].name;
                if (strcmp(cmp, ent->d_name) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            char full[512];
            snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
            struct stat st;
            if (stat(full, &st) == 0 && (st.st_mode & S_IXUSR)) {
                Entry *e = &app.entries[app.entry_count];
                strncpy(e->name, ent->d_name, sizeof(e->name) - 1);
                strncpy(e->exec, ent->d_name, sizeof(e->exec) - 1);
                e->from_desktop = false;
                app.entry_count++;
            }
        }
        closedir(d);
        dir = strtok(NULL, ":");
    }
}

static int entry_cmp(const void *a, const void *b)
{
    const Entry *ea = a;
    const Entry *eb = b;
    if (ea->from_desktop != eb->from_desktop)
        return ea->from_desktop ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
}

static void filter_entries(void)
{
    app.filtered_count = 0;
    app.selected       = 0;
    app.scroll         = 0;

    if (app.query_len == 0) {
        for (int i = 0; i < app.entry_count; i++)
            app.filtered[app.filtered_count++] = i;
        return;
    }

    for (int i = 0; i < app.entry_count; i++) {
        if (strncasecmp(app.entries[i].name, app.query, app.query_len) == 0)
            app.filtered[app.filtered_count++] = i;
    }
    for (int i = 0; i < app.entry_count; i++) {
        bool already = false;
        for (int j = 0; j < app.filtered_count; j++) {
            if (app.filtered[j] == i) { already = true; break; }
        }
        if (!already && strcasestr(app.entries[i].name, app.query))
            app.filtered[app.filtered_count++] = i;
    }
}

static void launch_selected(void)
{
    if (app.filtered_count == 0)
        return;

    const Entry *e = &app.entries[app.filtered[app.selected]];
    const char  *cmd = e->exec;

    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(1);
    }

    app.running = false;
}

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

    int rows = app.filtered_count < VISIBLE_ROWS
        ? app.filtered_count : VISIBLE_ROWS;
    int h = ITEM_HEIGHT + rows * ITEM_HEIGHT + 4;

    if (app.buffer)
        wl_buffer_destroy(app.buffer);

    uint8_t *data = NULL;
    app.buffer = make_buffer(app.width, h, &data);
    if (!app.buffer)
        return;

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

    char prompt[MAX_QUERY + 4];
    if (app.query_len > 0)
        snprintf(prompt, sizeof(prompt), "%s|", app.query);
    else
        snprintf(prompt, sizeof(prompt), "Search...|");

    cairo_set_source_rgba(cr,
        app.query_len > 0 ? FG_R : DIM_R,
        app.query_len > 0 ? FG_G : DIM_G,
        app.query_len > 0 ? FG_B : DIM_B,
        app.query_len > 0 ? 1.0  : 0.5);
    cairo_move_to(cr, 14, ITEM_HEIGHT * 0.66);
    cairo_show_text(cr, prompt);

    for (int i = 0; i < rows; i++) {
        int idx = app.scroll + i;
        if (idx >= app.filtered_count)
            break;

        const Entry *e   = &app.entries[app.filtered[idx]];
        bool         sel = (idx == app.selected);
        int          y   = ITEM_HEIGHT + i * ITEM_HEIGHT;

        if (sel) {
            cairo_set_source_rgba(cr, HL_R, HL_G, HL_B, 0.16);
            cairo_rectangle(cr, 2, y + 2, app.width - 4, ITEM_HEIGHT - 4);
            cairo_fill(cr);
        }

        cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL,
            sel ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, FONT_SIZE);
        cairo_set_source_rgba(cr,
            sel ? HL_R : FG_R,
            sel ? HL_G : FG_G,
            sel ? HL_B : FG_B,
            1.0);
        cairo_move_to(cr, 14, y + ITEM_HEIGHT * 0.62);
        cairo_show_text(cr, e->name);

        if (e->comment[0]) {
            cairo_text_extents_t nex;
            cairo_text_extents(cr, e->name, &nex);
            cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL,
                CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, FONT_SIZE - 2.5);
            cairo_set_source_rgba(cr, DIM_R, DIM_G, DIM_B, 0.65);
            cairo_move_to(cr, 14 + nex.x_advance + 10,
                y + ITEM_HEIGHT * 0.62);
            cairo_show_text(cr, e->comment);
        }
    }

    if (app.filtered_count > VISIBLE_ROWS) {
        int track_h = h - ITEM_HEIGHT;
        int thumb_h = track_h * VISIBLE_ROWS / app.filtered_count;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = ITEM_HEIGHT +
            (track_h - thumb_h) * app.scroll /
            (app.filtered_count - VISIBLE_ROWS);

        cairo_set_source_rgba(cr, HL_R, HL_G, HL_B, 0.25);
        cairo_rectangle(cr, app.width - 4, thumb_y, 3, thumb_h);
        cairo_fill(cr);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(cs);

    zwlr_layer_surface_v1_set_size(app.layer_surface, app.width, h);
    wl_surface_attach(app.surface, app.buffer, 0, 0);
    wl_surface_damage(app.surface, 0, 0, app.width, h);
    wl_surface_commit(app.surface);
}

static void handle_key(uint32_t keycode, uint32_t key_state)
{
    if (key_state != WL_KEYBOARD_KEY_STATE_PRESSED)
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
    case XKB_KEY_Page_Up:
        app.selected -= VISIBLE_ROWS;
        if (app.selected < 0) app.selected = 0;
        if (app.selected < app.scroll) app.scroll = app.selected;
        render();
        break;
    case XKB_KEY_Page_Down:
        app.selected += VISIBLE_ROWS;
        if (app.selected >= app.filtered_count)
            app.selected = app.filtered_count - 1;
        if (app.selected >= app.scroll + VISIBLE_ROWS)
            app.scroll = app.selected - VISIBLE_ROWS + 1;
        render();
        break;
    default: {
        char buf[8];
        int len = xkb_state_key_get_utf8(app.xkb_state, code,
            buf, sizeof(buf));
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

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
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

static void layer_configure(void *data,
    struct zwlr_layer_surface_v1 *ls,
    uint32_t serial, uint32_t w, uint32_t h)
{
    app.width      = (int)w ? (int)w : WIDTH;
    app.configured = true;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    render();
}

static void layer_closed(void *data,
    struct zwlr_layer_surface_v1 *ls)
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
        app.compositor = wl_registry_bind(reg, name,
            &wl_compositor_interface, 4);
    else if (strcmp(iface, wl_shm_interface.name) == 0)
        app.shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (strcmp(iface, wl_seat_interface.name) == 0) {
        app.seat = wl_registry_bind(reg, name, &wl_seat_interface, 7);
        wl_seat_add_listener(app.seat, &seat_listener, NULL);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        app.layer_shell = wl_registry_bind(reg, name,
            &zwlr_layer_shell_v1_interface, 1);
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
    app.running = true;
    app.width   = WIDTH;

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "kumlauncher: cannot connect to display\n");
        return 1;
    }

    app.xkb_ctx  = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, NULL);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.shm || !app.layer_shell) {
        fprintf(stderr, "kumlauncher: missing required protocols\n");
        return 1;
    }

    collect_desktop_entries();
    collect_path_entries();
    qsort(app.entries, app.entry_count, sizeof(Entry), entry_cmp);
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
