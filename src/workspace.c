#include "kumde.h"
#include <stdlib.h>
#include <string.h>

void kum_workspace_init(struct kum_server *server)
{
    for (int i = 0; i < KUM_WORKSPACE_COUNT; i++) {
        server->workspaces[i].index      = i;
        server->workspaces[i].layout     = LAYOUT_FLOATING;
        server->workspaces[i].scene_tree =
            wlr_scene_tree_create(&server->scene->tree);
        wl_list_init(&server->workspaces[i].toplevels);
        wlr_scene_node_set_enabled(
            &server->workspaces[i].scene_tree->node, false);
    }
}

static struct kum_output *output_for_workspace(struct kum_server *server,
    int ws_index)
{
    struct kum_output *o;
    wl_list_for_each(o, &server->outputs, link) {
        if (o->active_workspace == ws_index)
            return o;
    }
    return NULL;
}

static void ipc_broadcast_workspace(struct kum_server *server,
    struct kum_output *output, int index)
{
    char msg[80];
    snprintf(msg, sizeof(msg),
        "{\"event\":\"workspace\",\"output\":\"%s\",\"index\":%d}\n",
        output->wlr_output->name, index);
    kum_ipc_broadcast(server, msg, (int)strlen(msg));
}

void kum_workspace_arrange(struct kum_server *server,
    struct kum_output *output, int ws_index)
{
    struct kum_workspace *ws = &server->workspaces[ws_index];
    if (ws->layout == LAYOUT_FLOATING)
        return;
    if (ws->layout == LAYOUT_MONOCLE) {
        kum_workspace_monocle_arrange(server, output, ws_index);
        return;
    }

    int   gap = server->cfg.gap;
    float mr  = server->cfg.master_ratio;

    struct wlr_box area = output->usable_area;

    int count = 0;
    struct kum_toplevel *tl;
    wl_list_for_each(tl, &ws->toplevels, workspace_link) {
        if (!tl->floating && tl->xdg_toplevel->base->surface->mapped)
            count++;
    }

    if (count == 0)
        return;

    int master_w   = (count == 1)
        ? area.width - gap * 2
        : (int)((area.width - gap * 3) * mr);
    int stack_w    = area.width - gap * 3 - master_w;
    int stack_count = count - 1;

    int i = 0;
    wl_list_for_each(tl, &ws->toplevels, workspace_link) {
        if (tl->floating || !tl->xdg_toplevel->base->surface->mapped)
            continue;

        int x, y, w, h;
        if (i == 0) {
            x = area.x + gap;
            y = area.y + gap;
            w = master_w;
            h = area.height - gap * 2;
        } else {
            int slot   = i - 1;
            int slot_h = stack_count > 0
                ? (area.height - gap * (stack_count + 1)) / stack_count
                : area.height - gap * 2;
            x = area.x + gap * 2 + master_w;
            y = area.y + gap + slot * (slot_h + gap);
            w = stack_w;
            h = slot_h;
        }

        wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel, w, h);
        kum_border_update(tl, tl == server->focused);
        i++;
    }
}

void kum_workspace_switch(struct kum_server *server,
    struct kum_output *output, int index)
{
    if (index < 0 || index >= KUM_WORKSPACE_COUNT)
        return;
    if (!output)
        return;
    if (output->active_workspace == index)
        return;

    int prev = output->active_workspace;

    struct kum_output *other;
    wl_list_for_each(other, &server->outputs, link) {
        if (other != output && other->active_workspace == index) {
            other->active_workspace = prev;
            wlr_scene_node_set_enabled(
                &server->workspaces[prev].scene_tree->node, true);
            ipc_broadcast_workspace(server, other, prev);
            break;
        }
    }

    wlr_scene_node_set_enabled(
        &server->workspaces[prev].scene_tree->node, false);

    output->active_workspace = index;

    wlr_scene_node_set_enabled(
        &server->workspaces[index].scene_tree->node, true);

    if (server->focused && server->focused->workspace == prev) {
        server->focused = NULL;
        wlr_seat_keyboard_clear_focus(server->seat);
    }

    struct kum_toplevel *tl;
    wl_list_for_each(tl, &server->workspaces[index].toplevels, workspace_link) {
        if (tl->xdg_toplevel->base->surface->mapped) {
            kum_focus_toplevel(tl, tl->xdg_toplevel->base->surface);
            break;
        }
    }

    ipc_broadcast_workspace(server, output, index);
    wlr_log(WLR_DEBUG, "workspace: %s -> %d", output->wlr_output->name, index);
}

void kum_workspace_move_toplevel(struct kum_server *server,
    struct kum_toplevel *tl, int index)
{
    if (index < 0 || index >= KUM_WORKSPACE_COUNT)
        return;
    if (index == tl->workspace)
        return;

    struct kum_output *src = output_for_workspace(server, tl->workspace);
    int old_ws = tl->workspace;

    wl_list_remove(&tl->workspace_link);
    wl_list_insert(&server->workspaces[index].toplevels, &tl->workspace_link);
    wlr_scene_node_reparent(&tl->scene_tree->node,
        server->workspaces[index].scene_tree);

    tl->workspace = index;

    struct kum_output *dst = output_for_workspace(server, index);

    if (dst) {
        kum_workspace_arrange(server, dst, index);
    } else {
        wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
    }

    if (src)
        kum_workspace_arrange(server, src, old_ws);

    if (server->focused == tl) {
        server->focused = NULL;
        wlr_seat_keyboard_clear_focus(server->seat);

        if (src) {
            struct kum_toplevel *next;
            wl_list_for_each(next, &server->workspaces[old_ws].toplevels,
                    workspace_link) {
                if (next->xdg_toplevel->base->surface->mapped) {
                    kum_focus_toplevel(next,
                        next->xdg_toplevel->base->surface);
                    break;
                }
            }
        }
    }

    wlr_log(WLR_DEBUG, "workspace: moved toplevel to %d", index);
}

void kum_workspace_toggle_layout(struct kum_server *server,
    struct kum_output *output)
{
    if (!output)
        return;

    int idx = output->active_workspace;
    struct kum_workspace *ws = &server->workspaces[idx];

    ws->layout = (ws->layout == LAYOUT_FLOATING)
        ? LAYOUT_TILE : LAYOUT_FLOATING;

    if (ws->layout == LAYOUT_TILE)
        kum_workspace_arrange(server, output, idx);

    wlr_log(WLR_DEBUG, "workspace %d: layout %s", idx,
        ws->layout == LAYOUT_TILE ? "tile" : "float");
}

void kum_workspace_scratchpad_toggle(struct kum_server *server)
{
    struct kum_output *output = kum_output_focused(server);
    if (!output) return;
    int cur = output->active_workspace;
    int scratch = server->scratchpad_ws;
    if (scratch < 0) return;
    if (cur == scratch) {
        for (int i = 0; i < KUM_WORKSPACE_COUNT; i++) {
            if (i != scratch) {
                kum_workspace_switch(server, output, i);
                break;
            }
        }
    } else {
        kum_workspace_switch(server, output, scratch);
    }
}

void kum_workspace_monocle_arrange(struct kum_server *server,
    struct kum_output *output, int ws_index)
{
    struct kum_workspace *ws = &server->workspaces[ws_index];
    int gap = server->cfg.gap;
    struct wlr_box area = output->usable_area;

    struct kum_toplevel *tl;
    wl_list_for_each(tl, &ws->toplevels, workspace_link) {
        if (tl->floating || !tl->xdg_toplevel->base->surface->mapped)
            continue;
        wlr_scene_node_set_position(&tl->scene_tree->node,
            area.x + gap, area.y + gap);
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel,
            area.width - gap*2, area.height - gap*2);
        wlr_scene_node_lower_to_bottom(&tl->scene_tree->node);
        kum_border_update(tl, tl == server->focused);
    }
    if (server->focused && server->focused->workspace == ws_index
            && !server->focused->floating)
        wlr_scene_node_raise_to_top(&server->focused->scene_tree->node);
}
