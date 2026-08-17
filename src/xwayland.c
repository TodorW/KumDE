#ifdef KUM_XWAYLAND

#include "kumde.h"
#include "pixelbuffer.h"
#include <stdlib.h>
#include <string.h>

static void xwayland_surface_map(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, map);

    struct kum_server *server = xs->server;

    struct kum_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_box obox;
        wlr_output_layout_get_box(server->output_layout,
            output->wlr_output, &obox);

        int x = obox.x + (obox.width  - xs->xwayland_surface->width)  / 2;
        int y = obox.y + (obox.height - xs->xwayland_surface->height) / 2;
        wlr_scene_node_set_position(&xs->scene_tree->node, x, y);
        break;
    }

    kum_rules_apply_xwayland(server, xs);

    kum_xwayland_border_create(xs);
    kum_xwayland_border_update(xs, false);
    xs->last_width  = xs->xwayland_surface->width;
    xs->last_height = xs->xwayland_surface->height;
    kum_xwayland_shadow_create(xs);
    kum_xwayland_corners_apply(xs);

    kum_anim_start(&xs->anim, ANIM_OPENING, 0.0f, 1.0f,
        server->cfg.anim_open_ms);

    wlr_scene_node_set_enabled(&xs->scene_tree->node, true);
    kum_ipc_broadcast_occupancy(server);
    wlr_log(WLR_DEBUG, "xwayland: mapped '%s'",
        xs->xwayland_surface->title ? xs->xwayland_surface->title : "(null)");
}

static void xwayland_surface_unmap(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, unmap);

    if (xs->server->focused_xwayland == xs)
        xs->server->focused_xwayland = NULL;

    kum_anim_start(&xs->anim, ANIM_CLOSING, 1.0f, 0.0f,
        xs->server->cfg.anim_close_ms);

    kum_ipc_broadcast_occupancy(xs->server);

    kum_xwayland_border_destroy(xs);
    kum_xwayland_shadow_destroy(xs);
    kum_xwayland_corners_destroy(xs);
}

static void xwayland_surface_commit(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, commit);

    if (!xs->xwayland_surface->surface->mapped)
        return;

    kum_xwayland_border_update(xs, xs->server->focused_xwayland == xs);

    int w = xs->xwayland_surface->width;
    int h = xs->xwayland_surface->height;
    if (w != xs->last_width || h != xs->last_height) {
        xs->last_width  = w;
        xs->last_height = h;
        kum_xwayland_shadow_create(xs);
        kum_xwayland_corners_apply(xs);
    }
}

static void xwayland_surface_destroy(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, destroy);

    if (xs->server->focused_xwayland == xs)
        xs->server->focused_xwayland = NULL;

    kum_xwayland_border_destroy(xs);
    kum_xwayland_shadow_destroy(xs);
    kum_xwayland_corners_destroy(xs);

    if (xs->associated) {
        wl_list_remove(&xs->map.link);
        wl_list_remove(&xs->unmap.link);
        wl_list_remove(&xs->commit.link);
    }
    wl_list_remove(&xs->associate.link);
    wl_list_remove(&xs->dissociate.link);
    wl_list_remove(&xs->destroy.link);
    wl_list_remove(&xs->request_move.link);
    wl_list_remove(&xs->request_resize.link);
    wl_list_remove(&xs->request_maximize.link);
    wl_list_remove(&xs->request_fullscreen.link);
    wl_list_remove(&xs->set_title.link);
    wl_list_remove(&xs->link);
    free(xs);
}

void kum_xwayland_begin_interactive(struct kum_xwayland_surface *xs, enum wlr_edges edges)
{
    xs->grabbed      = true;
    xs->resize_edges = edges;
    xs->grab_x       = (int)xs->server->cursor->x;
    xs->grab_y       = (int)xs->server->cursor->y;

    int nx, ny;
    wlr_scene_node_coords(&xs->scene_tree->node, &nx, &ny);
    xs->grab_geobox.x      = nx;
    xs->grab_geobox.y      = ny;
    xs->grab_geobox.width  = xs->xwayland_surface->width;
    xs->grab_geobox.height = xs->xwayland_surface->height;
}

static void xwayland_request_move(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, request_move);
    kum_xwayland_begin_interactive(xs, WLR_EDGE_NONE);
}

static void xwayland_request_resize(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, request_resize);
    struct wlr_xwayland_resize_event *ev = data;
    kum_xwayland_begin_interactive(xs, ev->edges);
}

static void xwayland_set_maximized(struct kum_xwayland_surface *xs, bool want)
{
    if (want) {
        struct kum_output *output;
        wl_list_for_each(output, &xs->server->outputs, link) {
            if (output->active_workspace != xs->workspace)
                continue;
            xs->saved_geom.x      = xs->xwayland_surface->x;
            xs->saved_geom.y      = xs->xwayland_surface->y;
            xs->saved_geom.width  = xs->xwayland_surface->width;
            xs->saved_geom.height = xs->xwayland_surface->height;

            struct wlr_box area = output->usable_area;
            wlr_xwayland_surface_configure(xs->xwayland_surface,
                area.x, area.y, area.width, area.height);
            wlr_scene_node_set_position(&xs->scene_tree->node,
                area.x, area.y);
            break;
        }
    } else if (xs->saved_geom.width > 0) {
        wlr_xwayland_surface_configure(xs->xwayland_surface,
            xs->saved_geom.x, xs->saved_geom.y,
            xs->saved_geom.width, xs->saved_geom.height);
        wlr_scene_node_set_position(&xs->scene_tree->node,
            xs->saved_geom.x, xs->saved_geom.y);
    }

    wlr_xwayland_surface_set_maximized(xs->xwayland_surface, want, want);
}

static void xwayland_request_maximize(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, request_maximize);
    bool want = xs->xwayland_surface->maximized_horz ||
        xs->xwayland_surface->maximized_vert;
    xwayland_set_maximized(xs, want);
}

void kum_xwayland_set_fullscreen(struct kum_xwayland_surface *xs, bool fs)
{
    if (fs) {
        struct kum_output *output;
        wl_list_for_each(output, &xs->server->outputs, link) {
            if (output->active_workspace != xs->workspace)
                continue;
            xs->saved_geom.x      = xs->xwayland_surface->x;
            xs->saved_geom.y      = xs->xwayland_surface->y;
            xs->saved_geom.width  = xs->xwayland_surface->width;
            xs->saved_geom.height = xs->xwayland_surface->height;

            struct wlr_box full;
            wlr_output_layout_get_box(xs->server->output_layout,
                output->wlr_output, &full);
            wlr_xwayland_surface_configure(xs->xwayland_surface,
                full.x, full.y, full.width, full.height);
            wlr_scene_node_set_position(&xs->scene_tree->node,
                full.x, full.y);
            break;
        }
    } else if (xs->saved_geom.width > 0) {
        wlr_xwayland_surface_configure(xs->xwayland_surface,
            xs->saved_geom.x, xs->saved_geom.y,
            xs->saved_geom.width, xs->saved_geom.height);
        wlr_scene_node_set_position(&xs->scene_tree->node,
            xs->saved_geom.x, xs->saved_geom.y);
    }

    wlr_xwayland_surface_set_fullscreen(xs->xwayland_surface, fs);
}

void kum_xwayland_toggle_fullscreen(struct kum_xwayland_surface *xs)
{
    kum_xwayland_set_fullscreen(xs, !xs->xwayland_surface->fullscreen);
}

static void xwayland_request_fullscreen(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, request_fullscreen);
    kum_xwayland_set_fullscreen(xs, xs->xwayland_surface->fullscreen);
}

static void xwayland_set_title(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, set_title);
    wlr_log(WLR_DEBUG, "xwayland title: %s",
        xs->xwayland_surface->title ? xs->xwayland_surface->title : "(null)");
}

void kum_focus_xwayland_surface(struct kum_xwayland_surface *xs)
{
    struct kum_server *server = xs->server;
    if (server->locked)
        return;
    struct wlr_seat   *seat   = server->seat;
    struct wlr_surface *surface = xs->xwayland_surface->surface;
    struct wlr_surface *prev  = seat->keyboard_state.focused_surface;

    if (prev == surface)
        return;

    if (prev) {
        struct wlr_xdg_surface *prev_xdg =
            wlr_xdg_surface_try_from_wlr_surface(prev);
        if (prev_xdg && prev_xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
            wlr_xdg_toplevel_set_activated(prev_xdg->toplevel, false);
            struct kum_toplevel *t;
            wl_list_for_each(t, &server->toplevels, link) {
                if (t->xdg_toplevel->base->surface == prev) {
                    kum_border_update(t, false);
                    break;
                }
            }
        } else if (server->focused_xwayland &&
                server->focused_xwayland->xwayland_surface->surface == prev) {
            wlr_xwayland_surface_activate(
                server->focused_xwayland->xwayland_surface, false);
            kum_xwayland_border_update(server->focused_xwayland, false);
        }
    }

    server->focused           = NULL;
    server->focused_xwayland  = xs;
    xs->focus_serial          = ++server->next_focus_serial;

    wlr_scene_node_raise_to_top(&xs->scene_tree->node);
    wlr_xwayland_surface_activate(xs->xwayland_surface, true);
    kum_xwayland_border_update(xs, true);

    kum_ipc_broadcast_window_title(server, xs->workspace,
        xs->xwayland_surface->title ? xs->xwayland_surface->title : "");

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(seat, surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
}

static void xwayland_surface_associate(struct wl_listener *listener,
    void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, associate);
    struct wlr_xwayland_surface *wlr_xs = xs->xwayland_surface;

    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_create(xs->scene_tree, wlr_xs->surface);
    (void)scene_surface;

    xs->map.notify    = xwayland_surface_map;
    xs->unmap.notify  = xwayland_surface_unmap;
    xs->commit.notify = xwayland_surface_commit;

    wl_signal_add(&wlr_xs->surface->events.map,    &xs->map);
    wl_signal_add(&wlr_xs->surface->events.unmap,  &xs->unmap);
    wl_signal_add(&wlr_xs->surface->events.commit, &xs->commit);

    xs->associated = true;
}

static void xwayland_surface_dissociate(struct wl_listener *listener,
    void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, dissociate);

    if (!xs->associated)
        return;

    wl_list_remove(&xs->map.link);
    wl_list_remove(&xs->unmap.link);
    wl_list_remove(&xs->commit.link);
    xs->associated = false;
}

static void handle_new_xwayland_surface(struct wl_listener *listener,
    void *data)
{
    struct kum_server             *server =
        wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface   *wlr_xs = data;

    struct kum_output *output = kum_output_focused(server);
    int ws = output ? output->active_workspace : 0;

    struct kum_xwayland_surface *xs = calloc(1, sizeof(*xs));
    xs->kind             = KUM_NODE_XWAYLAND;
    xs->server           = server;
    xs->xwayland_surface = wlr_xs;
    xs->workspace        = ws;

    xs->scene_tree = wlr_scene_tree_create(
        server->workspaces[xs->workspace].scene_tree);
    xs->scene_tree->node.data = xs;
    wlr_xs->data              = xs;

    /* wlr_xs->surface is NULL until the associate event fires (it becomes
     * invalid again on dissociate) -- creating the scene surface or
     * touching surface->events here segfaults deep inside wlroots on the
     * very first X11 window, confirmed live via coredumpctl. Defer
     * anything surface-dependent to xwayland_surface_associate(). */
    xs->associate.notify   = xwayland_surface_associate;
    xs->dissociate.notify  = xwayland_surface_dissociate;
    xs->destroy.notify           = xwayland_surface_destroy;
    xs->request_move.notify      = xwayland_request_move;
    xs->request_resize.notify    = xwayland_request_resize;
    xs->request_maximize.notify  = xwayland_request_maximize;
    xs->request_fullscreen.notify = xwayland_request_fullscreen;
    xs->set_title.notify         = xwayland_set_title;

    wl_signal_add(&wlr_xs->events.associate,       &xs->associate);
    wl_signal_add(&wlr_xs->events.dissociate,      &xs->dissociate);
    wl_signal_add(&wlr_xs->events.destroy,         &xs->destroy);
    wl_signal_add(&wlr_xs->events.request_move,    &xs->request_move);
    wl_signal_add(&wlr_xs->events.request_resize,  &xs->request_resize);
    wl_signal_add(&wlr_xs->events.request_maximize, &xs->request_maximize);
    wl_signal_add(&wlr_xs->events.request_fullscreen, &xs->request_fullscreen);
    wl_signal_add(&wlr_xs->events.set_title,       &xs->set_title);

    wl_list_insert(&server->xwayland_surfaces, &xs->link);
}

static void handle_xwayland_ready(struct wl_listener *listener, void *data)
{
    struct kum_server *server =
        wl_container_of(listener, server, xwayland_ready);

    wlr_log(WLR_INFO, "xwayland ready");

    struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(
        server->cursor_mgr, "default", 1);
    if (xcursor) {
        struct wlr_xcursor_image *image = xcursor->images[0];
        size_t size = (size_t)image->width * image->height * 4;
        uint32_t *pixels = malloc(size);
        if (pixels) {
            memcpy(pixels, image->buffer, size);
            struct wlr_buffer *cursor_buf =
                kum_pixel_buffer_create(image->width, image->height, pixels);
            if (cursor_buf) {
                wlr_xwayland_set_cursor(server->xwayland, cursor_buf,
                    image->hotspot_x, image->hotspot_y);
                wlr_buffer_drop(cursor_buf);
            }
        }
    }
}

void kum_xwayland_init(struct kum_server *server)
{
    /* server->xwayland_surfaces is initialized unconditionally in
     * kum_server_init(), not here -- see the comment there. */
    server->xwayland = wlr_xwayland_create(server->display,
        server->compositor, false);

    if (!server->xwayland) {
        wlr_log(WLR_ERROR, "xwayland: failed to create");
        return;
    }

    server->xwayland_ready.notify = handle_xwayland_ready;
    wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);

    server->new_xwayland_surface.notify = handle_new_xwayland_surface;
    wl_signal_add(&server->xwayland->events.new_surface,
        &server->new_xwayland_surface);

    setenv("DISPLAY", server->xwayland->display_name, true);
    wlr_log(WLR_INFO, "xwayland: DISPLAY=%s", server->xwayland->display_name);
}

void kum_xwayland_finish(struct kum_server *server)
{
    if (server->xwayland) {
        wlr_xwayland_destroy(server->xwayland);
        server->xwayland = NULL;
    }
}

#endif
