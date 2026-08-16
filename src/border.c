#include "kumde.h"

static void border_create_rects(struct wlr_scene_tree *tree,
    struct kum_runtime_config *cfg, struct wlr_scene_rect *border[4])
{
    float color[4] = {
        cfg->border_inactive[0], cfg->border_inactive[1],
        cfg->border_inactive[2], KUM_BORDER_ALPHA,
    };
    for (int i = 0; i < 4; i++)
        border[i] = wlr_scene_rect_create(tree, 0, 0, color);
}

static void border_position(struct wlr_scene_rect *border[4],
    int w, int h, int b)
{
    wlr_scene_node_set_position(&border[0]->node, -b, -b);
    wlr_scene_rect_set_size(border[0], w + 2 * b, b);

    wlr_scene_node_set_position(&border[1]->node, -b, h);
    wlr_scene_rect_set_size(border[1], w + 2 * b, b);

    wlr_scene_node_set_position(&border[2]->node, -b, 0);
    wlr_scene_rect_set_size(border[2], b, h);

    wlr_scene_node_set_position(&border[3]->node, w, 0);
    wlr_scene_rect_set_size(border[3], b, h);
}

static void border_color(struct wlr_scene_rect *border[4],
    struct kum_runtime_config *cfg, bool focused, float t)
{
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
        wlr_scene_rect_set_color(border[i], color);
}

static void border_destroy_rects(struct wlr_scene_rect *border[4])
{
    for (int i = 0; i < 4; i++) {
        if (border[i]) {
            wlr_scene_node_destroy(&border[i]->node);
            border[i] = NULL;
        }
    }
}

void kum_border_create(struct kum_toplevel *toplevel)
{
    border_create_rects(toplevel->scene_tree, &toplevel->server->cfg,
        toplevel->border);
}

void kum_border_update(struct kum_toplevel *toplevel, bool focused)
{
    if (!toplevel->xdg_toplevel->base->surface->mapped)
        return;

    struct kum_runtime_config *cfg = &toplevel->server->cfg;

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo);

    border_position(toplevel->border, geo.width, geo.height, cfg->border_width);

    float t = 1.0f;
    if (focused && toplevel->anim.type == ANIM_FOCUSING && !toplevel->anim.done)
        t = toplevel->anim.current;

    border_color(toplevel->border, cfg, focused, t);
}

void kum_border_destroy(struct kum_toplevel *toplevel)
{
    border_destroy_rects(toplevel->border);
}

#ifdef KUM_XWAYLAND
void kum_xwayland_border_create(struct kum_xwayland_surface *xs)
{
    border_create_rects(xs->scene_tree, &xs->server->cfg, xs->border);
}

void kum_xwayland_border_update(struct kum_xwayland_surface *xs, bool focused)
{
    struct kum_runtime_config *cfg = &xs->server->cfg;

    border_position(xs->border, xs->xwayland_surface->width,
        xs->xwayland_surface->height, cfg->border_width);

    float t = 1.0f;
    if (focused && xs->anim.type == ANIM_FOCUSING && !xs->anim.done)
        t = xs->anim.current;

    border_color(xs->border, cfg, focused, t);
}

void kum_xwayland_border_destroy(struct kum_xwayland_surface *xs)
{
    border_destroy_rects(xs->border);
}
#endif
