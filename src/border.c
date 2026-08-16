#include "kumde.h"

void kum_border_create(struct kum_toplevel *toplevel)
{
    struct kum_runtime_config *cfg = &toplevel->server->cfg;
    float color[4] = {
        cfg->border_inactive[0], cfg->border_inactive[1],
        cfg->border_inactive[2], KUM_BORDER_ALPHA,
    };
    for (int i = 0; i < 4; i++) {
        toplevel->border[i] = wlr_scene_rect_create(
            toplevel->scene_tree, 0, 0, color);
    }
}

void kum_border_update(struct kum_toplevel *toplevel, bool focused)
{
    if (!toplevel->xdg_toplevel->base->surface->mapped)
        return;

    struct kum_runtime_config *cfg = &toplevel->server->cfg;

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo);

    int w = geo.width;
    int h = geo.height;
    int b = cfg->border_width;

    wlr_scene_node_set_position(&toplevel->border[0]->node, -b, -b);
    wlr_scene_rect_set_size(toplevel->border[0], w + 2 * b, b);

    wlr_scene_node_set_position(&toplevel->border[1]->node, -b, h);
    wlr_scene_rect_set_size(toplevel->border[1], w + 2 * b, b);

    wlr_scene_node_set_position(&toplevel->border[2]->node, -b, 0);
    wlr_scene_rect_set_size(toplevel->border[2], b, h);

    wlr_scene_node_set_position(&toplevel->border[3]->node, w, 0);
    wlr_scene_rect_set_size(toplevel->border[3], b, h);

    float t = 1.0f;
    if (focused && toplevel->anim.type == ANIM_FOCUSING && !toplevel->anim.done)
        t = toplevel->anim.current;

    float color[4];
    if (focused) {
        color[0] = cfg->border_inactive[0] + (cfg->border_active[0] - cfg->border_inactive[0]) * t;
        color[1] = cfg->border_inactive[1] + (cfg->border_active[1] - cfg->border_inactive[1]) * t;
        color[2] = cfg->border_inactive[2] + (cfg->border_active[2] - cfg->border_inactive[2]) * t;
        color[3] = KUM_BORDER_ALPHA;
    } else {
        color[0] = cfg->border_inactive[0];
        color[1] = cfg->border_inactive[1];
        color[2] = cfg->border_inactive[2];
        color[3] = KUM_BORDER_ALPHA;
    }

    for (int i = 0; i < 4; i++)
        wlr_scene_rect_set_color(toplevel->border[i], color);
}

void kum_border_destroy(struct kum_toplevel *toplevel)
{
    for (int i = 0; i < 4; i++) {
        if (toplevel->border[i]) {
            wlr_scene_node_destroy(&toplevel->border[i]->node);
            toplevel->border[i] = NULL;
        }
    }
}
