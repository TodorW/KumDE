#include "kumde.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct kum_server *g_server = NULL;

static void handle_sighup(int sig)
{
    (void)sig;
    if (g_server)
        kum_server_reload_config(g_server);
}

void kum_server_reload_config(struct kum_server *server)
{
    struct kum_runtime_config fresh;
    kum_config_defaults(&fresh);

    const char *xdg  = getenv("XDG_CONFIG_HOME");
    char path[512];
    if (xdg)
        snprintf(path, sizeof(path), "%s/kumde/kumde.conf", xdg);
    else {
        const char *home = getenv("HOME");
        snprintf(path, sizeof(path), "%s/.config/kumde/kumde.conf",
            home ? home : "");
    }

    kum_config_load(&fresh, path);

    server->cfg.border_width     = fresh.border_width;
    server->cfg.border_active[0] = fresh.border_active[0];
    server->cfg.border_active[1] = fresh.border_active[1];
    server->cfg.border_active[2] = fresh.border_active[2];
    server->cfg.border_inactive[0] = fresh.border_inactive[0];
    server->cfg.border_inactive[1] = fresh.border_inactive[1];
    server->cfg.border_inactive[2] = fresh.border_inactive[2];
    server->cfg.anim_open_ms     = fresh.anim_open_ms;
    server->cfg.anim_close_ms    = fresh.anim_close_ms;
    server->cfg.anim_focus_ms    = fresh.anim_focus_ms;
    server->cfg.animations       = fresh.animations;
    server->cfg.gap              = fresh.gap;
    server->cfg.master_ratio     = fresh.master_ratio;
    server->cfg.corner_radius    = fresh.corner_radius;
    server->cfg.shadow_radius    = fresh.shadow_radius;
    server->cfg.shadow_alpha     = fresh.shadow_alpha;
    server->cfg.shadow_offset_x  = fresh.shadow_offset_x;
    server->cfg.shadow_offset_y  = fresh.shadow_offset_y;
    server->cfg.shadows          = fresh.shadows;
    server->cfg.rounded_corners  = fresh.rounded_corners;
    server->cfg.focus_follows_mouse = fresh.focus_follows_mouse;

    strncpy(server->cfg.kb_layout,  fresh.kb_layout,  sizeof(server->cfg.kb_layout)  - 1);
    strncpy(server->cfg.kb_variant, fresh.kb_variant, sizeof(server->cfg.kb_variant) - 1);
    strncpy(server->cfg.kb_model,   fresh.kb_model,   sizeof(server->cfg.kb_model)   - 1);
    strncpy(server->cfg.kb_options, fresh.kb_options, sizeof(server->cfg.kb_options) - 1);
    server->cfg.tap_to_click         = fresh.tap_to_click;
    server->cfg.natural_scroll       = fresh.natural_scroll;
    server->cfg.disable_while_typing = fresh.disable_while_typing;
    server->cfg.pointer_accel        = fresh.pointer_accel;

    server->cfg.output_config_count = fresh.output_config_count;
    memcpy(server->cfg.output_configs, fresh.output_configs,
        sizeof(server->cfg.output_configs));

    struct kum_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link) {
        kum_border_update(tl, tl == server->focused);
        kum_shadow_destroy(tl);
        kum_corners_destroy(tl);
        if (tl->xdg_toplevel->base->surface->mapped) {
            kum_shadow_create(tl);
            kum_corners_apply(tl);
        }
    }

    struct kum_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (server->workspaces[output->active_workspace].layout == LAYOUT_TILE)
            kum_workspace_arrange(server, output, output->active_workspace);
    }

    kum_input_configure(server);
    kum_kb_layout_init(server);
    kum_output_configure_all(server);

    kum_rules_free(server);
    kum_rules_load(server, path);
    wlr_log(WLR_INFO, "config reloaded");
}

static void server_setup_globals(struct kum_server *server)
{
    server->compositor = wlr_compositor_create(server->display,
        KUM_COMPOSITOR_VERSION, server->renderer);
    wlr_subcompositor_create(server->display);
    wlr_data_device_manager_create(server->display);
    wlr_primary_selection_v1_device_manager_create(server->display);
    wlr_data_control_manager_v1_create(server->display);
    wlr_viewporter_create(server->display);
    wlr_screencopy_manager_v1_create(server->display);
    wlr_server_decoration_manager_create(server->display);
    wlr_xdg_decoration_manager_v1_create(server->display);
    server->idle_notifier = wlr_idle_notifier_v1_create(server->display);
}

static void server_setup_outputs(struct kum_server *server)
{
    server->output_layout = wlr_output_layout_create(server->display);
    server->scene_layout  = wlr_scene_attach_output_layout(
        server->scene, server->output_layout);

    wl_list_init(&server->outputs);
    server->new_output.notify = kum_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);
}

static void server_setup_shell(struct kum_server *server)
{
    server->xdg_shell = wlr_xdg_shell_create(server->display,
        KUM_XDG_SHELL_VERSION);
    server->new_xdg_toplevel.notify = kum_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel,
        &server->new_xdg_toplevel);
    server->new_xdg_popup.notify = kum_new_xdg_popup;
    wl_signal_add(&server->xdg_shell->events.new_popup,
        &server->new_xdg_popup);

    wl_list_init(&server->layer_surfaces);
    server->layer_shell = wlr_layer_shell_v1_create(server->display,
        KUM_LAYER_SHELL_VERSION);
    server->new_layer_surface.notify = kum_new_layer_surface;
    wl_signal_add(&server->layer_shell->events.new_surface,
        &server->new_layer_surface);
}

static void server_setup_cursor(struct kum_server *server)
{
    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
    server->cursor_mgr = wlr_xcursor_manager_create(NULL,
        server->cfg.cursor_size);

    server->cursor_motion.notify = kum_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);

    server->cursor_motion_absolute.notify = kum_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute,
        &server->cursor_motion_absolute);

    server->cursor_button.notify = kum_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);

    server->cursor_axis.notify = kum_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);

    server->cursor_frame.notify = kum_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);
}

static void server_setup_input(struct kum_server *server)
{
    wl_list_init(&server->keyboards);
    wl_list_init(&server->pointers);
    wl_list_init(&server->keybinds);

    server->new_input.notify = kum_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);

    server->seat = wlr_seat_create(server->display, "seat0");
    server->request_cursor.notify = kum_request_cursor;
    wl_signal_add(&server->seat->events.request_set_cursor,
        &server->request_cursor);
    server->request_set_selection.notify = kum_request_set_selection;
    wl_signal_add(&server->seat->events.request_set_selection,
        &server->request_set_selection);

    kum_keybind_setup_defaults(server);
    kum_keybind_setup_extended(server);
    kum_keybind_setup_session(server);
}

static const char *config_path(void)
{
    static char path[512];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg)
        snprintf(path, sizeof(path), "%s/kumde/kumde.conf", xdg);
    else {
        const char *home = getenv("HOME");
        snprintf(path, sizeof(path), "%s/.config/kumde/kumde.conf",
            home ? home : "");
    }
    return path;
}

void kum_server_init(struct kum_server *server)
{
    memset(server, 0, sizeof(*server));
    wlr_log_init(WLR_INFO, NULL);

    kum_config_defaults(&server->cfg);
    kum_config_load(&server->cfg, config_path());
    kum_rules_load(server, config_path());

    server->display = wl_display_create();
    if (!server->display)
        goto fatal;

    server->backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server->display), NULL);
    if (!server->backend)
        goto fatal;

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer)
        goto fatal;
    wlr_renderer_init_wl_display(server->renderer, server->display);

    server->allocator = wlr_allocator_autocreate(server->backend,
        server->renderer);
    if (!server->allocator)
        goto fatal;

    server->scene = wlr_scene_create();
    if (!server->scene)
        goto fatal;

    wl_list_init(&server->toplevels);
#ifdef KUM_XWAYLAND
    /* Initialized unconditionally (not just when cfg.xwayland is true and
     * kum_xwayland_init() actually runs): every wl_list_for_each() over
     * this list throughout the codebase is guarded only by #ifdef
     * KUM_XWAYLAND, not by the runtime config flag, so it must always be
     * a valid empty list when XWayland support is compiled in but not
     * enabled at runtime -- otherwise it's four zeroed-out NULL pointers
     * left over from kum_server_init()'s memset(), and the first
     * wl_list_for_each() over it (e.g. on the very first cursor motion
     * event) segfaults immediately. */
    wl_list_init(&server->xwayland_surfaces);
#endif

    kum_workspace_init(server);
    kum_rules_init(server);
    server->scratchpad_ws = KUM_WORKSPACE_COUNT - 1;
    server_setup_globals(server);
    server_setup_outputs(server);
    server_setup_shell(server);
    server_setup_cursor(server);
    server_setup_input(server);
    kum_ipc_init(server);
    kum_kb_layout_init(server);

#ifdef KUM_XWAYLAND
    if (server->cfg.xwayland)
        kum_xwayland_init(server);
#endif

    g_server = server;
    signal(SIGHUP, handle_sighup);

    wlr_log(WLR_INFO, "kumde %s ready", KUM_VERSION);
    return;

fatal:
    wlr_log(WLR_ERROR, "fatal: server initialisation failed");
    exit(1);
}

static void spawn_terminal(const char *preferred)
{
    if (fork() != 0)
        return;
    setsid();
    if (preferred && preferred[0])
        execlp(preferred, preferred, NULL);
    execlp("foot",            "foot",            NULL);
    execlp("alacritty",       "alacritty",       NULL);
    execlp("kitty",           "kitty",           NULL);
    execlp("weston-terminal", "weston-terminal", NULL);
    _exit(1);
}

void kum_server_run(struct kum_server *server)
{
    const char *socket = wl_display_add_socket_auto(server->display);
    if (!socket) {
        wlr_log(WLR_ERROR, "failed to create wayland socket");
        exit(1);
    }

    if (!wlr_backend_start(server->backend)) {
        wlr_log(WLR_ERROR, "failed to start backend");
        exit(1);
    }

    setenv("WAYLAND_DISPLAY", socket, 1);
    wlr_log(WLR_INFO, "WAYLAND_DISPLAY=%s", socket);

    kum_autostart_run(server);
    spawn_terminal(server->cfg.terminal);
    wl_display_run(server->display);
}

void kum_server_finish(struct kum_server *server)
{
    signal(SIGHUP, SIG_DFL);
    kum_rules_free(server);
    kum_ipc_finish(server);

#ifdef KUM_XWAYLAND
    if (server->cfg.xwayland)
        kum_xwayland_finish(server);
#endif

    wl_display_destroy_clients(server->display);
    wlr_scene_node_destroy(&server->scene->tree.node);
    wlr_xcursor_manager_destroy(server->cursor_mgr);
    wlr_cursor_destroy(server->cursor);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);
    wlr_backend_destroy(server->backend);
    wl_display_destroy(server->display);
}
