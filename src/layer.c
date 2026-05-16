#include "kumde.h"
#include <stdlib.h>

struct kum_layer_surface {
    struct wlr_layer_surface_v1          *wlr_layer_surface;
    struct wlr_scene_layer_surface_v1    *scene_layer;
    struct kum_server                    *server;
    struct wl_listener                    map;
    struct wl_listener                    unmap;
    struct wl_listener                    destroy;
    struct wl_listener                    commit;
};

static void layer_map(struct wl_listener *listener, void *data)
{
    struct kum_layer_surface *ls = wl_container_of(listener, ls, map);
    wlr_log(WLR_DEBUG, "layer surface mapped: %s",
        ls->wlr_layer_surface->namespace);
}

static void layer_unmap(struct wl_listener *listener, void *data)
{
    (void)listener;
    (void)data;
}

static void layer_commit(struct wl_listener *listener, void *data)
{
    struct kum_layer_surface *ls = wl_container_of(listener, ls, commit);
    if (!ls->wlr_layer_surface->current.committed)
        return;

    struct wlr_output *wlr_output = ls->wlr_layer_surface->output;
    if (!wlr_output)
        return;

    struct wlr_box full, usable;
    wlr_output_layout_get_box(ls->server->output_layout, wlr_output, &full);
    usable = full;

    wlr_scene_layer_surface_v1_configure(ls->scene_layer, &full, &usable);
}

static void layer_destroy(struct wl_listener *listener, void *data)
{
    struct kum_layer_surface *ls = wl_container_of(listener, ls, destroy);
    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    free(ls);
}

void kum_new_layer_surface(struct wl_listener *listener, void *data)
{
    struct kum_server           *server = wl_container_of(listener, server, new_layer_surface);
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

    struct wlr_box full, usable;
    wlr_output_layout_get_box(server->output_layout, wlr_ls->output, &full);
    usable = full;
    wlr_scene_layer_surface_v1_configure(ls->scene_layer, &full, &usable);
}
