#include "kumde.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void server_setup_globals(struct kum_server *server)
{
    wlr_compositor_create(server->display, KUM_COMPOSITOR_VERSION,
        server->renderer);
    wlr_subcompositor_create(server->display);
    wlr_data_device_manager_create(server->display);
    wlr_primary_selection_v1_device_manager_create(server->display);
    wlr_viewporter_create(server->display);
    wlr_screencopy_manager_v1_create(server->display);
    wlr_server_decoration_manager_create(server->display);
    wlr_xdg_decoration_manager_v1_create(server->display);
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
    server->cursor_mgr = wlr_xcursor_manager_create(NULL, KUM_CURSOR_SIZE);

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
}

void kum_server_init(struct kum_server *server)
{
    memset(server, 0, sizeof(*server));
    wlr_log_init(WLR_INFO, NULL);

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

    server_setup_globals(server);
    server_setup_outputs(server);
    server_setup_shell(server);
    server_setup_cursor(server);
    server_setup_input(server);

    wlr_log(WLR_INFO, "kumde %s initialised", KUM_VERSION);
    return;

fatal:
    wlr_log(WLR_ERROR, "fatal: server initialisation failed");
    exit(1);
}

static void spawn_terminal(void)
{
    if (fork() != 0)
        return;

    setsid();
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

    spawn_terminal();
    wl_display_run(server->display);
}

void kum_server_finish(struct kum_server *server)
{
    wl_display_destroy_clients(server->display);
    wlr_scene_node_destroy(&server->scene->tree.node);
    wlr_xcursor_manager_destroy(server->cursor_mgr);
    wlr_cursor_destroy(server->cursor);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);
    wlr_backend_destroy(server->backend);
    wl_display_destroy(server->display);
}
