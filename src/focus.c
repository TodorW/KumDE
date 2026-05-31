#include "kumde.h"

static struct kum_output *output_at(struct kum_server *server, double x, double y)
{
    struct wlr_output *wo = wlr_output_layout_output_at(server->output_layout, x, y);
    if (!wo) return NULL;
    struct kum_output *o;
    wl_list_for_each(o, &server->outputs, link)
        if (o->wlr_output == wo) return o;
    return NULL;
}

void kum_output_focus_adjacent(struct kum_server *server, int dx, int dy)
{
    struct kum_output *cur = kum_output_focused(server);
    if (!cur) return;

    struct wlr_box cb;
    wlr_output_layout_get_box(server->output_layout, cur->wlr_output, &cb);

    double probe_x = cb.x + cb.width  / 2.0 + dx * (cb.width  * 0.75);
    double probe_y = cb.y + cb.height / 2.0 + dy * (cb.height * 0.75);

    struct kum_output *target = output_at(server, probe_x, probe_y);
    if (!target || target == cur) return;

    struct wlr_box tb;
    wlr_output_layout_get_box(server->output_layout, target->wlr_output, &tb);

    wlr_cursor_warp(server->cursor, NULL,
        tb.x + tb.width  / 2.0,
        tb.y + tb.height / 2.0);

    int ws = target->active_workspace;
    struct kum_toplevel *tl;
    wl_list_for_each(tl, &server->workspaces[ws].toplevels, workspace_link) {
        if (tl->xdg_toplevel->base->surface->mapped) {
            kum_focus_toplevel(tl, tl->xdg_toplevel->base->surface);
            return;
        }
    }
}
