#include "kumde.h"
#include <stdlib.h>
#include <string.h>

/* scene_tree->node.data points at either a struct kum_toplevel or (when
 * XWayland is enabled) a struct kum_xwayland_surface; both begin with a
 * kum_node_kind tag so the real type can be identified before casting. */
struct kum_node_tag {
    kum_node_kind kind;
};

void *kum_scene_node_at(struct kum_server *server,
    double lx, double ly, struct wlr_surface **surface,
    double *sx, double *sy, kum_node_kind *kind_out)
{
    struct wlr_scene_node *node =
        wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);

    if (!node || node->type != WLR_SCENE_NODE_BUFFER)
        return NULL;

    struct wlr_scene_buffer *sbuf = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *ssurface = wlr_scene_surface_try_from_buffer(sbuf);
    if (!ssurface)
        return NULL;

    *surface = ssurface->surface;

    struct wlr_scene_tree *tree = node->parent;
    while (tree && tree->node.data == NULL)
        tree = tree->node.parent;

    if (!tree || !tree->node.data)
        return NULL;

    if (kind_out)
        *kind_out = ((struct kum_node_tag *)tree->node.data)->kind;

    return tree->node.data;
}

struct kum_toplevel *kum_toplevel_at(struct kum_server *server,
    double lx, double ly, struct wlr_surface **surface,
    double *sx, double *sy)
{
    kum_node_kind kind;
    void *data = kum_scene_node_at(server, lx, ly, surface, sx, sy, &kind);
    if (!data || kind != KUM_NODE_XDG)
        return NULL;
    return data;
}

void kum_focus_toplevel(struct kum_toplevel *toplevel,
                        struct wlr_surface *surface)
{
    if (!toplevel)
        return;

    struct kum_server *server = toplevel->server;
    if (server->locked)
        return;
    struct wlr_seat   *seat   = server->seat;
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
#ifdef KUM_XWAYLAND
        else if (server->focused_xwayland &&
                server->focused_xwayland->xwayland_surface->surface == prev) {
            wlr_xwayland_surface_activate(
                server->focused_xwayland->xwayland_surface, false);
            kum_xwayland_border_update(server->focused_xwayland, false);
        }
#endif
    }

#ifdef KUM_XWAYLAND
    server->focused_xwayland = NULL;
#endif

    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);

    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

    kum_anim_start(&toplevel->anim, ANIM_FOCUSING, 0.0f, 1.0f,
        server->cfg.anim_focus_ms);
    kum_border_update(toplevel, true);
    server->focused = toplevel;
    toplevel->focus_serial = ++server->next_focus_serial;

    kum_ipc_broadcast_window_title(server, toplevel->workspace,
        toplevel->xdg_toplevel->title ? toplevel->xdg_toplevel->title : "");

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(seat, surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
}

static void toplevel_center_on_output(struct kum_toplevel *tl,
    struct kum_output *output)
{
    if (!output)
        return;

    struct wlr_box geo = tl->xdg_toplevel->base->geometry;

    struct wlr_box area = output->usable_area;
    int x = area.x + (area.width  - geo.width)  / 2;
    int y = area.y + (area.height - geo.height) / 2;
    wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
}

static struct kum_output *output_for_toplevel(struct kum_toplevel *tl)
{
    struct kum_output *o;
    wl_list_for_each(o, &tl->server->outputs, link) {
        if (o->active_workspace == tl->workspace)
            return o;
    }
    return NULL;
}

static void toplevel_map(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl     = wl_container_of(listener, tl, map);
    struct kum_server   *server = tl->server;
    struct kum_output   *output = output_for_toplevel(tl);
    struct kum_workspace *ws    = &server->workspaces[tl->workspace];

    kum_border_create(tl);
    kum_border_update(tl, false);
    kum_shadow_create(tl);
    kum_corners_apply(tl);

    if (ws->layout == LAYOUT_TILE && !tl->floating) {
        kum_workspace_arrange(server, output, tl->workspace);
    } else {
        toplevel_center_on_output(tl, output);
    }

    if (server->cfg.animations)
        kum_anim_start(&tl->anim, ANIM_OPENING, 0.0f, 1.0f,
            server->cfg.anim_open_ms);

    kum_rules_apply(tl->server, tl);
    kum_focus_toplevel(tl, tl->xdg_toplevel->base->surface);
    kum_ipc_broadcast_occupancy(tl->server);
}

static void toplevel_unmap(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl     = wl_container_of(listener, tl, unmap);
    struct kum_server   *server = tl->server;
    struct kum_output   *output = output_for_toplevel(tl);

    if (server->focused == tl)
        server->focused = NULL;

    if (server->cfg.animations)
        kum_anim_start(&tl->anim, ANIM_CLOSING, 1.0f, 0.0f,
            server->cfg.anim_close_ms);

    kum_border_destroy(tl);
    kum_shadow_destroy(tl);
    kum_corners_destroy(tl);
    kum_ipc_broadcast_occupancy(tl->server);

    if (server->workspaces[tl->workspace].layout == LAYOUT_TILE && output)
        kum_workspace_arrange(server, output, tl->workspace);
}

static void toplevel_commit(struct wl_listener *listener, void *data)
{
    struct kum_toplevel  *tl = wl_container_of(listener, tl, commit);
    struct kum_workspace *ws = &tl->server->workspaces[tl->workspace];

    /* The client's first wl_surface.commit() (no buffer attached yet) is
     * the "initial commit" -- the compositor must respond with the first
     * configure or a spec-compliant client waits forever and never attaches
     * a buffer, so the toplevel never maps. Without this every xdg-shell
     * client hangs invisibly on startup.
     *
     * Use the toplevel-specific setter rather than calling
     * wlr_xdg_surface_schedule_configure() directly: the latter asserts
     * surface->initialized, which (unlike wlr_layer_surface_v1) is not yet
     * true for an xdg_toplevel by the time this commit listener runs --
     * confirmed live, it aborted the compositor on the client's very next
     * commit. wlr_xdg_toplevel_set_size() is the toplevel-role path that
     * populates pending state before scheduling, matching the canonical
     * tinywl idiom (0,0 lets the client pick its own initial size). */
    if (tl->xdg_toplevel->base->initial_commit)
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel, 0, 0);

    if (ws->layout == LAYOUT_TILE && !tl->floating)
        kum_border_update(tl, tl == tl->server->focused);

    if (tl->shadow_buf) {
        struct wlr_box geo = tl->xdg_toplevel->base->geometry;
        if (geo.width  != tl->last_geo.width ||
            geo.height != tl->last_geo.height) {
            kum_shadow_update(tl);
            kum_corners_apply(tl);
            tl->last_geo = geo;
        }
    }
}

static void toplevel_destroy(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, destroy);
    kum_shadow_destroy(tl);
    kum_corners_destroy(tl);
    wl_list_remove(&tl->map.link);
    wl_list_remove(&tl->unmap.link);
    wl_list_remove(&tl->commit.link);
    wl_list_remove(&tl->destroy.link);
    wl_list_remove(&tl->request_move.link);
    wl_list_remove(&tl->request_resize.link);
    wl_list_remove(&tl->request_maximize.link);
    wl_list_remove(&tl->request_fullscreen.link);
    wl_list_remove(&tl->set_title.link);
    wl_list_remove(&tl->workspace_link);
    wl_list_remove(&tl->link);
    free(tl);
}

void kum_toplevel_begin_interactive(struct kum_toplevel *tl, enum wlr_edges edges)
{
    struct kum_workspace *ws = &tl->server->workspaces[tl->workspace];
    if (ws->layout == LAYOUT_TILE && !tl->floating) {
        tl->floating = true;
        struct wlr_box geo = tl->xdg_toplevel->base->geometry;
        tl->saved_geom = geo;
        struct kum_output *output = output_for_toplevel(tl);
        kum_workspace_arrange(tl->server, output, tl->workspace);
    }

    tl->grabbed      = true;
    tl->resize_edges = edges;
    tl->grab_x       = (int)tl->server->cursor->x;
    tl->grab_y       = (int)tl->server->cursor->y;

    struct wlr_box geo = tl->xdg_toplevel->base->geometry;

    int nx, ny;
    wlr_scene_node_coords(&tl->scene_tree->node, &nx, &ny);
    tl->grab_geobox.x      = nx;
    tl->grab_geobox.y      = ny;
    tl->grab_geobox.width  = geo.width;
    tl->grab_geobox.height = geo.height;
}

static void request_move(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, request_move);
    kum_toplevel_begin_interactive(tl, WLR_EDGE_NONE);
}

static void request_resize(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, request_resize);
    struct wlr_xdg_toplevel_resize_event *ev = data;
    kum_toplevel_begin_interactive(tl, ev->edges);
}

static void request_maximize(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, request_maximize);
    if (!tl->xdg_toplevel->requested.maximized) {
        if (tl->saved_geom.width > 0) {
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel,
                tl->saved_geom.width, tl->saved_geom.height);
        }
        wlr_xdg_toplevel_set_maximized(tl->xdg_toplevel, false);
        return;
    }
    struct kum_output *output = output_for_toplevel(tl);
    if (!output)
        return;
    struct wlr_box geo = tl->xdg_toplevel->base->geometry;
    tl->saved_geom = geo;
    struct wlr_box area = output->usable_area;
    wlr_xdg_toplevel_set_size(tl->xdg_toplevel, area.width, area.height);
    wlr_scene_node_set_position(&tl->scene_tree->node, area.x, area.y);
    wlr_xdg_toplevel_set_maximized(tl->xdg_toplevel, true);
}

void kum_toplevel_set_fullscreen(struct kum_toplevel *tl, bool fs)
{
    if (fs) {
        struct kum_output *output = output_for_toplevel(tl);
        if (output) {
            struct wlr_box geo = tl->xdg_toplevel->base->geometry;
            tl->saved_geom = geo;
            struct wlr_box full;
            wlr_output_layout_get_box(tl->server->output_layout,
                output->wlr_output, &full);
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel,
                full.width, full.height);
            wlr_scene_node_set_position(&tl->scene_tree->node,
                full.x, full.y);
        }
    } else {
        if (tl->saved_geom.width > 0) {
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel,
                tl->saved_geom.width, tl->saved_geom.height);
        }
    }

    wlr_xdg_toplevel_set_fullscreen(tl->xdg_toplevel, fs);
}

void kum_toplevel_toggle_fullscreen(struct kum_toplevel *tl)
{
    kum_toplevel_set_fullscreen(tl, !tl->xdg_toplevel->current.fullscreen);
}

static void request_fullscreen(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, request_fullscreen);
    kum_toplevel_set_fullscreen(tl, tl->xdg_toplevel->requested.fullscreen);
}

static void set_title(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, set_title);
    wlr_log(WLR_DEBUG, "title: %s",
        tl->xdg_toplevel->title ? tl->xdg_toplevel->title : "(null)");
}

void kum_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
    struct kum_server       *server =
        wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *wlr_tl = data;

    struct kum_output *output = kum_output_focused(server);
    int ws = output ? output->active_workspace : 0;

    struct kum_toplevel *tl = calloc(1, sizeof(*tl));
    tl->kind         = KUM_NODE_XDG;
    tl->server       = server;
    tl->xdg_toplevel = wlr_tl;
    tl->workspace    = ws;
    tl->floating     = false;

    tl->scene_tree = wlr_scene_xdg_surface_create(
        server->workspaces[ws].scene_tree, wlr_tl->base);
    tl->scene_tree->node.data = tl;
    wlr_tl->base->data        = tl->scene_tree;

    tl->map.notify                = toplevel_map;
    tl->unmap.notify              = toplevel_unmap;
    tl->commit.notify             = toplevel_commit;
    tl->destroy.notify            = toplevel_destroy;
    tl->request_move.notify       = request_move;
    tl->request_resize.notify     = request_resize;
    tl->request_maximize.notify   = request_maximize;
    tl->request_fullscreen.notify = request_fullscreen;
    tl->set_title.notify          = set_title;

    wl_signal_add(&wlr_tl->base->surface->events.map,    &tl->map);
    wl_signal_add(&wlr_tl->base->surface->events.unmap,  &tl->unmap);
    wl_signal_add(&wlr_tl->base->surface->events.commit, &tl->commit);
    wl_signal_add(&wlr_tl->events.destroy,               &tl->destroy);
    wl_signal_add(&wlr_tl->events.request_move,          &tl->request_move);
    wl_signal_add(&wlr_tl->events.request_resize,        &tl->request_resize);
    wl_signal_add(&wlr_tl->events.request_maximize,      &tl->request_maximize);
    wl_signal_add(&wlr_tl->events.request_fullscreen,    &tl->request_fullscreen);
    wl_signal_add(&wlr_tl->events.set_title,             &tl->set_title);

    wl_list_insert(&server->toplevels, &tl->link);
    wl_list_insert(&server->workspaces[ws].toplevels, &tl->workspace_link);
}

static void popup_commit(struct wl_listener *listener, void *data)
{
    struct kum_popup *popup = wl_container_of(listener, popup, commit);
    if (popup->xdg_popup->base->initial_commit)
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
}

static void popup_destroy(struct wl_listener *listener, void *data)
{
    struct kum_popup *popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    free(popup);
}

void kum_new_xdg_popup(struct wl_listener *listener, void *data)
{
    struct wlr_xdg_popup *xdg_popup = data;

    struct kum_popup *popup = calloc(1, sizeof(*popup));
    popup->xdg_popup = xdg_popup;

    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    struct wlr_scene_tree *parent_tree = parent->data;
    xdg_popup->base->data =
        wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    popup->commit.notify  = popup_commit;
    popup->destroy.notify = popup_destroy;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}
