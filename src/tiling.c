#include "kumde.h"

void kum_tiling_swap_master(struct kum_server *server)
{
    if (!server->focused) return;

    struct kum_toplevel *focused = server->focused;
    int ws = focused->workspace;
    struct kum_workspace *wsp = &server->workspaces[ws];

    if (wsp->layout != LAYOUT_TILE) return;
    if (focused->floating) return;

    struct kum_toplevel *master = NULL;
    wl_list_for_each(master, &wsp->toplevels, workspace_link) {
        if (!master->floating && master->xdg_toplevel->base->surface->mapped)
            break;
        master = NULL;
    }

    if (!master || master == focused) return;

    wl_list_remove(&focused->workspace_link);
    wl_list_insert(wsp->toplevels.prev, &focused->workspace_link);

    struct kum_output *output = NULL;
    struct kum_output *o;
    wl_list_for_each(o, &server->outputs, link) {
        if (o->active_workspace == ws) { output = o; break; }
    }

    if (output)
        kum_workspace_arrange(server, output, ws);
}

void kum_tiling_resize(struct kum_server *server, int dw, int dh)
{
    if (!server->focused) return;

    struct kum_toplevel *tl = server->focused;
    struct kum_workspace *ws = &server->workspaces[tl->workspace];

    if (ws->layout == LAYOUT_TILE) {
        server->cfg.master_ratio += (float)dw / 2000.0f;
        if (server->cfg.master_ratio < 0.1f) server->cfg.master_ratio = 0.1f;
        if (server->cfg.master_ratio > 0.9f) server->cfg.master_ratio = 0.9f;

        struct kum_output *output = NULL;
        struct kum_output *o;
        wl_list_for_each(o, &server->outputs, link) {
            if (o->active_workspace == tl->workspace) { output = o; break; }
        }
        if (output) kum_workspace_arrange(server, output, tl->workspace);
    } else {
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(tl->xdg_toplevel->base, &geo);
        int nw = geo.width  + dw;
        int nh = geo.height + dh;
        if (nw < KUM_RESIZE_MIN_W) nw = KUM_RESIZE_MIN_W;
        if (nh < KUM_RESIZE_MIN_H) nh = KUM_RESIZE_MIN_H;
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel, nw, nh);
    }
}
