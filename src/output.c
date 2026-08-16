#include "kumde.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void tick_animations(struct kum_server *server)
{
    struct kum_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link) {
        if (tl->anim.done || tl->anim.type == ANIM_NONE)
            continue;

        kum_anim_tick(&tl->anim);

        float v = tl->anim.current;

        if (tl->anim.type == ANIM_OPENING || tl->anim.type == ANIM_CLOSING) {
            wlr_scene_node_set_enabled(&tl->scene_tree->node, v > 0.05f);
#if WLR_VERSION_MINOR >= 18
            wlr_scene_node_set_opacity(&tl->scene_tree->node, v);
#endif
        } else if (tl->anim.type == ANIM_FOCUSING) {
            kum_border_update(tl, true);
        }
    }
}

static void output_frame(struct wl_listener *listener, void *data)
{
    struct kum_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
        output->server->scene, output->wlr_output);

    tick_animations(output->server);
    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data)
{
    struct kum_output *output =
        wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void migrate_workspace(struct kum_server *server, int ws_index)
{
    struct kum_output *dst = NULL;
    struct kum_output *o;

    wl_list_for_each(o, &server->outputs, link) {
        dst = o;
        break;
    }

    if (!dst)
        return;

    int old_active = dst->active_workspace;
    dst->active_workspace = ws_index;

    wlr_scene_node_set_enabled(
        &server->workspaces[old_active].scene_tree->node, false);
    wlr_scene_node_set_enabled(
        &server->workspaces[ws_index].scene_tree->node, true);

    if (server->workspaces[ws_index].layout == LAYOUT_TILE)
        kum_workspace_arrange(server, dst, ws_index);

    struct kum_toplevel *tl;
    wl_list_for_each(tl, &server->workspaces[ws_index].toplevels, workspace_link) {
        if (tl->xdg_toplevel->base->surface->mapped) {
            kum_focus_toplevel(tl, tl->xdg_toplevel->base->surface);
            break;
        }
    }

    char msg[80];
    snprintf(msg, sizeof(msg),
        "{\"event\":\"workspace\",\"output\":\"%s\",\"index\":%d}\n",
        dst->wlr_output->name, ws_index);
    kum_ipc_broadcast(server, msg, (int)strlen(msg));

    wlr_log(WLR_INFO, "output removed: migrated ws %d to %s",
        ws_index, dst->wlr_output->name);
}

static void output_destroy(struct wl_listener *listener, void *data)
{
    struct kum_output *output = wl_container_of(listener, output, destroy);
    struct kum_server *server = output->server;

    int ws = output->active_workspace;

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);

    if (server->focused) {
        struct kum_toplevel *tl = server->focused;
        if (tl->workspace == ws) {
            server->focused = NULL;
            wlr_seat_keyboard_clear_focus(server->seat);
        }
    }

    free(output);

    if (!wl_list_empty(&server->outputs))
        migrate_workspace(server, ws);
    else
        wlr_scene_node_set_enabled(
            &server->workspaces[ws].scene_tree->node, false);
}

static int next_free_workspace(struct kum_server *server)
{
    bool used[KUM_WORKSPACE_COUNT] = {0};
    struct kum_output *o;
    wl_list_for_each(o, &server->outputs, link)
        used[o->active_workspace] = true;

    for (int i = 0; i < KUM_WORKSPACE_COUNT; i++) {
        if (!used[i])
            return i;
    }
    return 0;
}

struct kum_output *kum_output_focused(struct kum_server *server)
{
    double cx = server->cursor->x;
    double cy = server->cursor->y;
    struct wlr_output *wlr_out =
        wlr_output_layout_output_at(server->output_layout, cx, cy);
    if (!wlr_out)
        return NULL;
    struct kum_output *o;
    wl_list_for_each(o, &server->outputs, link) {
        if (o->wlr_output == wlr_out)
            return o;
    }
    return NULL;
}

void kum_new_output(struct wl_listener *listener, void *data)
{
    struct kum_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode)
        wlr_output_state_set_mode(&state, mode);

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    struct kum_output *output = calloc(1, sizeof(*output));
    output->server           = server;
    output->wlr_output       = wlr_output;
    output->active_workspace = next_free_workspace(server);

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);

    struct wlr_output_layout_output *lo =
        wlr_output_layout_add_auto(server->output_layout, wlr_output);

    wlr_output_layout_get_box(server->output_layout, wlr_output,
        &output->usable_area);

    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, lo,
        output->scene_output);

    wlr_scene_node_set_enabled(
        &server->workspaces[output->active_workspace].scene_tree->node, true);

    kum_output_configure_all(server);

    wlr_log(WLR_INFO, "output connected: %s ws=%d",
        wlr_output->name, output->active_workspace);
}
