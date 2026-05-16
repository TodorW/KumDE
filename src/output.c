#include "kumde.h"
#include <stdlib.h>
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
    struct kum_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data)
{
    struct kum_output *output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
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
    output->server     = server;
    output->wlr_output = wlr_output;

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);

    struct wlr_output_layout_output *lo =
        wlr_output_layout_add_auto(server->output_layout, wlr_output);

    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, lo,
        output->scene_output);

    wlr_log(WLR_INFO, "output connected: %s", wlr_output->name);
}
