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

static struct wlr_scene_buffer *corners_build(struct wlr_scene_tree *tree,
    struct wlr_renderer *renderer, int radius, int w, int h)
{
    if (radius <= 0 || w <= 0 || h <= 0)
        return NULL;

    uint32_t *pixels = build_corner_mask(w, h, radius);
    if (!pixels)
        return NULL;

    struct wlr_texture *mask = wlr_texture_from_pixels(renderer,
        DRM_FORMAT_ARGB8888, (uint32_t)w * 4, (uint32_t)w, (uint32_t)h,
        pixels);
    free(pixels);
    if (!mask)
        return NULL;

    struct wlr_scene_buffer *buf = wlr_scene_buffer_create(tree, NULL);
    if (!buf) {
        wlr_texture_destroy(mask);
        return NULL;
    }

    wlr_scene_buffer_set_texture(buf, mask);
    wlr_texture_destroy(mask);

    wlr_scene_node_set_position(&buf->node, 0, 0);
    wlr_scene_node_raise_to_top(&buf->node);
    return buf;
}

static void corners_destroy_buf(struct wlr_scene_buffer **buf)
{
    if (*buf) {
        wlr_scene_node_destroy(&(*buf)->node);
        *buf = NULL;
    }
}

void kum_corners_apply(struct kum_toplevel *toplevel)
{
    if (!toplevel->server->cfg.rounded_corners)
        return;
    if (!toplevel->xdg_toplevel->base->surface->mapped)
        return;

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo);

    corners_destroy_buf(&toplevel->corner_mask_buf);
    struct wlr_scene_buffer *buf = corners_build(toplevel->scene_tree,
        toplevel->server->renderer, toplevel->server->cfg.corner_radius,
        geo.width, geo.height);
    if (buf)
        toplevel->corner_mask_buf = buf;
}

void kum_corners_destroy(struct kum_toplevel *toplevel)
{
    corners_destroy_buf(&toplevel->corner_mask_buf);
}

#ifdef KUM_XWAYLAND
void kum_xwayland_corners_apply(struct kum_xwayland_surface *xs)
{
    if (!xs->server->cfg.rounded_corners)
        return;

    corners_destroy_buf(&xs->corner_mask_buf);
    struct wlr_scene_buffer *buf = corners_build(xs->scene_tree,
        xs->server->renderer, xs->server->cfg.corner_radius,
        xs->xwayland_surface->width, xs->xwayland_surface->height);
    if (buf)
        xs->corner_mask_buf = buf;
}

void kum_xwayland_corners_destroy(struct kum_xwayland_surface *xs)
{
    corners_destroy_buf(&xs->corner_mask_buf);
}
#endif
