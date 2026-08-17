#include "pixelbuffer.h"

#include <drm_fourcc.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>

struct kum_pixel_buffer {
    struct wlr_buffer base;
    uint32_t *data;
    size_t    stride;
};

static void pixel_buffer_destroy(struct wlr_buffer *wlr_buf)
{
    struct kum_pixel_buffer *buf = wl_container_of(wlr_buf, buf, base);
    free(buf->data);
    free(buf);
}

static bool pixel_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buf,
    uint32_t flags, void **data, uint32_t *format, size_t *stride)
{
    struct kum_pixel_buffer *buf = wl_container_of(wlr_buf, buf, base);
    *data   = buf->data;
    *format = DRM_FORMAT_ARGB8888;
    *stride = buf->stride;
    return true;
}

static void pixel_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buf)
{
}

static const struct wlr_buffer_impl pixel_buffer_impl = {
    .destroy               = pixel_buffer_destroy,
    .begin_data_ptr_access = pixel_buffer_begin_data_ptr_access,
    .end_data_ptr_access   = pixel_buffer_end_data_ptr_access,
};

struct wlr_buffer *kum_pixel_buffer_create(int width, int height,
    uint32_t *pixels)
{
    struct kum_pixel_buffer *buf = calloc(1, sizeof(*buf));
    if (!buf) {
        free(pixels);
        return NULL;
    }

    wlr_buffer_init(&buf->base, &pixel_buffer_impl, width, height);
    buf->data   = pixels;
    buf->stride = (size_t)width * 4;

    return &buf->base;
}
