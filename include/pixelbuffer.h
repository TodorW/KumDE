#ifndef KUM_PIXELBUFFER_H
#define KUM_PIXELBUFFER_H

#include <stdint.h>

struct wlr_buffer;

/* Wraps a heap-allocated, tightly-packed ARGB8888 pixel buffer (as
 * produced by build_shadow_texture()/build_corner_mask()) as a
 * struct wlr_buffer for use with wlr_scene_buffer_create()/
 * wlr_scene_buffer_set_buffer(). Takes ownership of `pixels` --the
 * caller must not free it, whether or not this call succeeds.
 * Returns a buffer with one reference (as if from wlr_buffer_lock());
 * the caller must eventually wlr_buffer_drop() or wlr_buffer_unlock()
 * it, matching how wlr_scene_buffer_create() locks its own reference. */
struct wlr_buffer *kum_pixel_buffer_create(int width, int height,
    uint32_t *pixels);

#endif
