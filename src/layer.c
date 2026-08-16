#include "kumde.h"
#include <stdlib.h>

struct kum_layer_surface {
    struct wl_list                        link;
    struct wlr_layer_surface_v1          *wlr_layer_surface;
    struct wlr_scene_layer_surface_v1    *scene_layer;
    struct kum_server                    *server;
    struct wl_listener                    map;
    struct wl_listener                    unmap;
    struct wl_listener                    destroy;
    struct wl_listener                    commit;
};

static struct kum_output *output_from_wlr(struct kum_server *server,
    struct wlr_output *wlr_out)
{
    struct kum_output *o;
    wl_list_for_each(o, &server->outputs, link) {
        if (o->wlr_output == wlr_out)
            return o;
    }
    return NULL;
}

/* Recomputes usable_area by folding the exclusive zone of every mapped
 * layer-shell surface on this output, in layer order (background first,
 * overlay last), instead of trusting a single surface's own commit. */
static void recalculate_usable_area(struct kum_server *server,
    struct kum_output *output)
{
    struct wlr_box full;
    wlr_output_layout_get_box(server->output_layout,
        output->wlr_output, &full);

    struct wlr_box usable = full;

    for (int layer = 0; layer <= 3; layer++) {
        struct kum_layer_surface *ls;
        wl_list_for_each(ls, &server->layer_surfaces, link) {
            if (ls->wlr_layer_surface->output != output->wlr_output)
                continue;
            if (!ls->wlr_layer_surface->surface->mapped)
                continue;
            if ((int)ls->wlr_layer_surface->current.layer != layer)
                continue;
            wlr_scene_layer_surface_v1_configure(ls->scene_layer,
                &full, &usable);
        }
    }

    output->usable_area = usable;

    if (server->workspaces[output->active_workspace].layout != LAYOUT_FLOATING)
        kum_workspace_arrange(server, output, output->active_workspace);
}

static void layer_map(struct wl_listener *listener, void *data)
{
    struct kum_layer_surface *ls = wl_container_of(listener, ls, map);
    struct kum_output *output = output_from_wlr(ls->server,
        ls->wlr_layer_surface->output);
    if (output)
        recalculate_usable_area(ls->server, output);
}

static void layer_unmap(struct wl_listener *listener, void *data)
{
    struct kum_layer_surface *ls = wl_container_of(listener, ls, unmap);
    struct kum_output *output = output_from_wlr(ls->server,
        ls->wlr_layer_surface->output);
    if (output)
        recalculate_usable_area(ls->server, output);
}

static void layer_commit(struct wl_listener *listener, void *data)
{
    struct kum_layer_surface *ls = wl_container_of(listener, ls, commit);
    struct wlr_output *wlr_out = ls->wlr_layer_surface->output;
    if (!wlr_out)
        return;

    struct kum_output *output = output_from_wlr(ls->server, wlr_out);
    if (output)
        recalculate_usable_area(ls->server, output);
}

static void layer_destroy(struct wl_listener *listener, void *data)
{
    struct kum_layer_surface *ls = wl_container_of(listener, ls, destroy);
    struct kum_server *server = ls->server;
    struct kum_output *output = output_from_wlr(server,
        ls->wlr_layer_surface->output);

    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->link);
    free(ls);

    if (output)
        recalculate_usable_area(server, output);
}

void kum_new_layer_surface(struct wl_listener *listener, void *data)
{
    struct kum_server           *server =
        wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *wlr_ls = data;

    if (!wlr_ls->output) {
        struct kum_output *o;
        wl_list_for_each(o, &server->outputs, link) {
            wlr_ls->output = o->wlr_output;
            break;
        }
    }

    struct kum_layer_surface *ls = calloc(1, sizeof(*ls));
    ls->server            = server;
    ls->wlr_layer_surface = wlr_ls;
    ls->scene_layer       = wlr_scene_layer_surface_v1_create(
        &server->scene->tree, wlr_ls);

    ls->map.notify     = layer_map;
    ls->unmap.notify   = layer_unmap;
    ls->commit.notify  = layer_commit;
    ls->destroy.notify = layer_destroy;

    wl_signal_add(&wlr_ls->surface->events.map,    &ls->map);
    wl_signal_add(&wlr_ls->surface->events.unmap,  &ls->unmap);
    wl_signal_add(&wlr_ls->surface->events.commit, &ls->commit);
    wl_signal_add(&wlr_ls->events.destroy,         &ls->destroy);

    wl_list_insert(&server->layer_surfaces, &ls->link);

    struct wlr_box full, usable;
    wlr_output_layout_get_box(server->output_layout, wlr_ls->output, &full);
    usable = full;
    wlr_scene_layer_surface_v1_configure(ls->scene_layer, &full, &usable);

    struct kum_output *output = output_from_wlr(server, wlr_ls->output);
    if (output)
        recalculate_usable_area(server, output);
}
