#include "kumde.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t *build_corner_mask(int w, int h, int radius)
{
    uint32_t *pixels = malloc((size_t)w * h * sizeof(uint32_t));
    if (!pixels)
        return NULL;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int dx = 0;
            int dy = 0;
            bool in_corner = false;

            if (x < radius && y < radius) {
                dx = radius - x;
                dy = radius - y;
                in_corner = true;
            } else if (x >= w - radius && y < radius) {
                dx = x - (w - radius - 1);
                dy = radius - y;
                in_corner = true;
            } else if (x < radius && y >= h - radius) {
                dx = radius - x;
                dy = y - (h - radius - 1);
                in_corner = true;
            } else if (x >= w - radius && y >= h - radius) {
                dx = x - (w - radius - 1);
                dy = y - (h - radius - 1);
                in_corner = true;
            }

            uint8_t alpha;
            if (!in_corner) {
                alpha = 255;
            } else {
                float dist = sqrtf((float)(dx * dx + dy * dy));
                if (dist >= (float)radius) {
                    alpha = 0;
                } else {
                    float t = dist / (float)radius;
                    float smooth = t * t * (3.0f - 2.0f * t);
                    alpha = (uint8_t)((1.0f - smooth) * 255.0f);
                }
            }

            pixels[y * w + x] = ((uint32_t)alpha << 24)
                | ((uint32_t)alpha << 16)
                | ((uint32_t)alpha <<  8)
                |  (uint32_t)alpha;
        }
    }

    return pixels;
}

void kum_corners_apply(struct kum_toplevel *toplevel)
{
    if (!toplevel->server->cfg.rounded_corners)
        return;
    if (!toplevel->xdg_toplevel->base->surface->mapped)
        return;

    int radius = toplevel->server->cfg.corner_radius;
    if (radius <= 0)
        return;

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo);
    int w = geo.width;
    int h = geo.height;

    if (w <= 0 || h <= 0)
        return;

    uint32_t *pixels = build_corner_mask(w, h, radius);
    if (!pixels)
        return;

    struct wlr_renderer *renderer = toplevel->server->renderer;

    struct wlr_texture *mask = wlr_texture_from_pixels(renderer,
        DRM_FORMAT_ARGB8888, (uint32_t)w * 4, (uint32_t)w, (uint32_t)h,
        pixels);
    free(pixels);

    if (!mask)
        return;

    if (toplevel->corner_mask_buf) {
        wlr_scene_node_destroy(&toplevel->corner_mask_buf->node);
        toplevel->corner_mask_buf = NULL;
    }

    toplevel->corner_mask_buf =
        wlr_scene_buffer_create(toplevel->scene_tree, NULL);
    if (!toplevel->corner_mask_buf) {
        wlr_texture_destroy(mask);
        return;
    }

    wlr_scene_buffer_set_texture(toplevel->corner_mask_buf, mask);
    wlr_texture_destroy(mask);

    wlr_scene_node_set_position(
        &toplevel->corner_mask_buf->node, 0, 0);
    wlr_scene_node_raise_to_top(
        &toplevel->corner_mask_buf->node);
}

void kum_corners_destroy(struct kum_toplevel *toplevel)
{
    if (toplevel->corner_mask_buf) {
        wlr_scene_node_destroy(&toplevel->corner_mask_buf->node);
        toplevel->corner_mask_buf = NULL;
    }
}
