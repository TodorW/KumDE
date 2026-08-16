#ifdef KUM_XWAYLAND

#include "kumde.h"
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

    kum_anim_start(&xs->anim, ANIM_OPENING, 0.0f, 1.0f,
        server->cfg.anim_open_ms);

    wlr_scene_node_set_enabled(&xs->scene_tree->node, true);
    wlr_log(WLR_DEBUG, "xwayland: mapped '%s'",
        xs->xwayland_surface->title ? xs->xwayland_surface->title : "(null)");
}

static void xwayland_surface_unmap(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, unmap);

    kum_anim_start(&xs->anim, ANIM_CLOSING, 1.0f, 0.0f,
        xs->server->cfg.anim_close_ms);
}

static void xwayland_surface_destroy(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, destroy);

    wl_list_remove(&xs->map.link);
    wl_list_remove(&xs->unmap.link);
    wl_list_remove(&xs->destroy.link);
    wl_list_remove(&xs->request_move.link);
    wl_list_remove(&xs->request_resize.link);
    wl_list_remove(&xs->request_maximize.link);
    wl_list_remove(&xs->request_fullscreen.link);
    wl_list_remove(&xs->set_title.link);
    wl_list_remove(&xs->link);
    free(xs);
}

static void xwayland_request_move(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, request_move);

    xs->grabbed      = true;
    xs->resize_edges = WLR_EDGE_NONE;
    xs->grab_x       = (int)xs->server->cursor->x;
    xs->grab_y       = (int)xs->server->cursor->y;

    int nx, ny;
    wlr_scene_node_coords(&xs->scene_tree->node, &nx, &ny);
    xs->grab_geobox.x      = nx;
    xs->grab_geobox.y      = ny;
    xs->grab_geobox.width  = xs->xwayland_surface->width;
    xs->grab_geobox.height = xs->xwayland_surface->height;
}

static void xwayland_request_resize(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, request_resize);
    struct wlr_xwayland_resize_event *ev = data;

    xs->grabbed      = true;
    xs->resize_edges = ev->edges;
    xs->grab_x       = (int)xs->server->cursor->x;
    xs->grab_y       = (int)xs->server->cursor->y;

    int nx, ny;
    wlr_scene_node_coords(&xs->scene_tree->node, &nx, &ny);
    xs->grab_geobox.x      = nx;
    xs->grab_geobox.y      = ny;
    xs->grab_geobox.width  = xs->xwayland_surface->width;
    xs->grab_geobox.height = xs->xwayland_surface->height;
}

static void xwayland_request_maximize(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, request_maximize);

    struct kum_output *o;
    wl_list_for_each(o, &xs->server->outputs, link) {
        struct wlr_box box;
        wlr_output_layout_get_box(xs->server->output_layout,
            o->wlr_output, &box);
        wlr_xwayland_surface_set_maximized(xs->xwayland_surface, true);
        wlr_xwayland_surface_configure(xs->xwayland_surface,
            box.x, box.y, box.width, box.height);
        wlr_scene_node_set_position(&xs->scene_tree->node, box.x, box.y);
        break;
    }
}

static void xwayland_request_fullscreen(struct wl_listener *listener, void *data)
{
    struct kum_xwayland_surface *xs =
        wl_container_of(listener, xs, request_fullscreen);
    wlr_xwayland_surface_set_fullscreen(xs->xwayland_surface,
        xs->xwayland_surface->fullscreen);
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
        }
    }

    server->focused = NULL;

    wlr_scene_node_raise_to_top(&xs->scene_tree->node);
    wlr_xwayland_surface_activate(xs->xwayland_surface, true);

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(seat, surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
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

    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_create(xs->scene_tree, wlr_xs->surface);
    (void)scene_surface;

    xs->map.notify               = xwayland_surface_map;
    xs->unmap.notify             = xwayland_surface_unmap;
    xs->destroy.notify           = xwayland_surface_destroy;
    xs->request_move.notify      = xwayland_request_move;
    xs->request_resize.notify    = xwayland_request_resize;
    xs->request_maximize.notify  = xwayland_request_maximize;
    xs->request_fullscreen.notify = xwayland_request_fullscreen;
    xs->set_title.notify         = xwayland_set_title;

    wl_signal_add(&wlr_xs->surface->events.map,    &xs->map);
    wl_signal_add(&wlr_xs->surface->events.unmap,  &xs->unmap);
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
        wlr_xwayland_set_cursor(server->xwayland,
            image->buffer, image->width * 4,
            image->width, image->height,
            image->hotspot_x, image->hotspot_y);
    }
}

void kum_xwayland_init(struct kum_server *server)
{
    wl_list_init(&server->xwayland_surfaces);

    server->xwayland = wlr_xwayland_create(server->display,
        wlr_compositor_from_server(server->display), false);

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
