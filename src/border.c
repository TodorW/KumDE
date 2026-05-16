#include "kumde.h"

static const float COLOR_INACTIVE[4] = {
    KUM_BORDER_INACTIVE_R,
    KUM_BORDER_INACTIVE_G,
    KUM_BORDER_INACTIVE_B,
    KUM_BORDER_ALPHA,
};

void kum_border_create(struct kum_toplevel *toplevel)
{
    for (int i = 0; i < 4; i++) {
        toplevel->border[i] = wlr_scene_rect_create(
            toplevel->scene_tree, 0, 0, COLOR_INACTIVE);
    }
}

void kum_border_update(struct kum_toplevel *toplevel, bool focused)
{
    if (!toplevel->xdg_toplevel->base->surface->mapped)
        return;

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo);

    int w = geo.width;
    int h = geo.height;
    int b = KUM_BORDER_WIDTH;

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
        color[0] = KUM_BORDER_INACTIVE_R + (KUM_BORDER_ACTIVE_R - KUM_BORDER_INACTIVE_R) * t;
        color[1] = KUM_BORDER_INACTIVE_G + (KUM_BORDER_ACTIVE_G - KUM_BORDER_INACTIVE_G) * t;
        color[2] = KUM_BORDER_INACTIVE_B + (KUM_BORDER_ACTIVE_B - KUM_BORDER_INACTIVE_B) * t;
        color[3] = KUM_BORDER_ALPHA;
    } else {
        color[0] = KUM_BORDER_INACTIVE_R;
        color[1] = KUM_BORDER_INACTIVE_G;
        color[2] = KUM_BORDER_INACTIVE_B;
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
