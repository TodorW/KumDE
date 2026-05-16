#include "kumde.h"
#include <stdlib.h>
#include <string.h>

struct kum_toplevel *kum_toplevel_at(struct kum_server *server,
    double lx, double ly, struct wlr_surface **surface,
    double *sx, double *sy)
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

    return tree ? tree->node.data : NULL;
}

void kum_focus_toplevel(struct kum_toplevel *toplevel,
                        struct wlr_surface *surface)
{
    if (!toplevel)
        return;

    struct kum_server *server = toplevel->server;
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
    }

    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);

    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

    kum_anim_start(&toplevel->anim, ANIM_FOCUSING, 0.0f, 1.0f,
        KUM_ANIM_FOCUS_MS);
    kum_border_update(toplevel, true);
    server->focused = toplevel;

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(seat, surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
}

static void toplevel_center_on_output(struct kum_toplevel *tl)
{
    struct kum_output *output;
    wl_list_for_each(output, &tl->server->outputs, link) {
        struct wlr_box obox;
        wlr_output_layout_get_box(tl->server->output_layout,
            output->wlr_output, &obox);

        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(tl->xdg_toplevel->base, &geo);

        int x = obox.x + (obox.width  - geo.width)  / 2;
        int y = obox.y + (obox.height - geo.height) / 2;
        wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
        break;
    }
}

static void toplevel_map(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, map);
    toplevel_center_on_output(tl);
    kum_border_create(tl);
    kum_border_update(tl, false);
    kum_anim_start(&tl->anim, ANIM_OPENING, 0.0f, 1.0f, KUM_ANIM_OPEN_MS);
    kum_focus_toplevel(tl, tl->xdg_toplevel->base->surface);
}

static void toplevel_unmap(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, unmap);
    if (tl->server->focused == tl)
        tl->server->focused = NULL;
    kum_anim_start(&tl->anim, ANIM_CLOSING, 1.0f, 0.0f, KUM_ANIM_CLOSE_MS);
    kum_border_destroy(tl);
}

static void toplevel_destroy(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, destroy);
    wl_list_remove(&tl->map.link);
    wl_list_remove(&tl->unmap.link);
    wl_list_remove(&tl->destroy.link);
    wl_list_remove(&tl->request_move.link);
    wl_list_remove(&tl->request_resize.link);
    wl_list_remove(&tl->request_maximize.link);
    wl_list_remove(&tl->request_fullscreen.link);
    wl_list_remove(&tl->set_title.link);
    wl_list_remove(&tl->link);
    free(tl);
}

static void begin_interactive(struct kum_toplevel *tl, enum wlr_edges edges)
{
    tl->grabbed      = true;
    tl->resize_edges = edges;
    tl->grab_x       = (int)tl->server->cursor->x;
    tl->grab_y       = (int)tl->server->cursor->y;

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(tl->xdg_toplevel->base, &geo);

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
    begin_interactive(tl, WLR_EDGE_NONE);
}

static void request_resize(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, request_resize);
    struct wlr_xdg_toplevel_resize_event *ev = data;
    begin_interactive(tl, ev->edges);
}

static void request_maximize(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, request_maximize);
    if (!tl->xdg_toplevel->requested.maximized) {
        wlr_xdg_toplevel_set_maximized(tl->xdg_toplevel, false);
        return;
    }
    struct kum_output *o;
    wl_list_for_each(o, &tl->server->outputs, link) {
        struct wlr_box box;
        wlr_output_layout_get_box(tl->server->output_layout,
            o->wlr_output, &box);
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel, box.width, box.height);
        wlr_scene_node_set_position(&tl->scene_tree->node, box.x, box.y);
        break;
    }
    wlr_xdg_toplevel_set_maximized(tl->xdg_toplevel, true);
}

static void request_fullscreen(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, request_fullscreen);
    wlr_xdg_toplevel_set_fullscreen(tl->xdg_toplevel,
        tl->xdg_toplevel->requested.fullscreen);
}

static void set_title(struct wl_listener *listener, void *data)
{
    struct kum_toplevel *tl = wl_container_of(listener, tl, set_title);
    wlr_log(WLR_DEBUG, "title: %s",
        tl->xdg_toplevel->title ? tl->xdg_toplevel->title : "(null)");
}

void kum_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
    struct kum_server       *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *wlr_tl = data;

    struct kum_toplevel *tl = calloc(1, sizeof(*tl));
    tl->server       = server;
    tl->xdg_toplevel = wlr_tl;
    tl->scene_tree   = wlr_scene_xdg_surface_create(&server->scene->tree,
        wlr_tl->base);
    tl->scene_tree->node.data = tl;
    wlr_tl->base->data        = tl->scene_tree;

    tl->map.notify               = toplevel_map;
    tl->unmap.notify             = toplevel_unmap;
    tl->destroy.notify           = toplevel_destroy;
    tl->request_move.notify      = request_move;
    tl->request_resize.notify    = request_resize;
    tl->request_maximize.notify  = request_maximize;
    tl->request_fullscreen.notify = request_fullscreen;
    tl->set_title.notify         = set_title;

    wl_signal_add(&wlr_tl->base->surface->events.map,    &tl->map);
    wl_signal_add(&wlr_tl->base->surface->events.unmap,  &tl->unmap);
    wl_signal_add(&wlr_tl->events.destroy,               &tl->destroy);
    wl_signal_add(&wlr_tl->events.request_move,          &tl->request_move);
    wl_signal_add(&wlr_tl->events.request_resize,        &tl->request_resize);
    wl_signal_add(&wlr_tl->events.request_maximize,      &tl->request_maximize);
    wl_signal_add(&wlr_tl->events.request_fullscreen,    &tl->request_fullscreen);
    wl_signal_add(&wlr_tl->events.set_title,             &tl->set_title);

    wl_list_insert(&server->toplevels, &tl->link);
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
