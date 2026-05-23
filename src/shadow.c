#include "kumde.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t *build_shadow_texture(int w, int h, int radius, float alpha)
{
    uint32_t *pixels = calloc(w * h, sizeof(uint32_t));
    if (!pixels)
        return NULL;

    float r2 = (float)(radius * radius);
    int   a8 = (int)(alpha * 255.0f);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int dx = 0;
            int dy = 0;

            if (x < radius)       dx = radius - x;
            else if (x >= w - radius) dx = x - (w - radius - 1);

            if (y < radius)       dy = radius - y;
            else if (y >= h - radius) dy = y - (h - radius - 1);

            float dist2 = (float)(dx * dx + dy * dy);
            float t;

            if (dx == 0 && dy == 0) {
                t = 1.0f;
            } else if (dist2 >= r2) {
                t = 0.0f;
            } else {
                float d = sqrtf(dist2) / (float)radius;
                t = 1.0f - d * d * (3.0f - 2.0f * d);
            }

            int v = (int)(t * (float)a8);
            pixels[y * w + x] = ((uint32_t)v << 24) |
                                 (0x00 << 16) |
                                 (0x00 <<  8) |
                                  0x00;
        }
    }

    return pixels;
}

void kum_shadow_create(struct kum_toplevel *toplevel)
{
    if (!toplevel->server->cfg.shadows)
        return;

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo);

    int sr  = toplevel->server->cfg.shadow_radius;
    int sw  = geo.width  + sr * 2;
    int sh  = geo.height + sr * 2;

    uint32_t *pixels = build_shadow_texture(sw, sh, sr,
        toplevel->server->cfg.shadow_alpha);
    if (!pixels)
        return;

    struct wlr_renderer *renderer = toplevel->server->renderer;

    struct wlr_texture *tex = wlr_texture_from_pixels(renderer,
        DRM_FORMAT_ARGB8888, sw * 4, sw, sh, pixels);
    free(pixels);

    if (!tex)
        return;

    toplevel->shadow_buf = wlr_scene_buffer_create(toplevel->scene_tree, NULL);
    if (!toplevel->shadow_buf) {
        wlr_texture_destroy(tex);
        return;
    }

    wlr_scene_buffer_set_texture(toplevel->shadow_buf, tex);
    wlr_texture_destroy(tex);

    int ox = toplevel->server->cfg.shadow_offset_x - sr;
    int oy = toplevel->server->cfg.shadow_offset_y - sr;
    wlr_scene_node_set_position(&toplevel->shadow_buf->node, ox, oy);
    wlr_scene_node_lower_to_bottom(&toplevel->shadow_buf->node);
}

void kum_shadow_update(struct kum_toplevel *toplevel)
{
    if (!toplevel->shadow_buf)
        return;

    kum_shadow_destroy(toplevel);
    kum_shadow_create(toplevel);
}

void kum_shadow_destroy(struct kum_toplevel *toplevel)
{
    if (toplevel->shadow_buf) {
        wlr_scene_node_destroy(&toplevel->shadow_buf->node);
        toplevel->shadow_buf = NULL;
    }
}
