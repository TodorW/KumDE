#ifndef KUMBAR_H
#define KUMBAR_H

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define KUMBAR_VERSION      "0.2.0"
#define KUMBAR_HEIGHT       28
#define KUMBAR_FONT         "monospace"
#define KUMBAR_FONT_SIZE    13

#define KUMBAR_BG_R     0.10f
#define KUMBAR_BG_G     0.10f
#define KUMBAR_BG_B     0.13f
#define KUMBAR_FG_R     0.85f
#define KUMBAR_FG_G     0.85f
#define KUMBAR_FG_B     0.90f
#define KUMBAR_ACC_R    0.45f
#define KUMBAR_ACC_G    0.60f
#define KUMBAR_ACC_B    0.95f
#define KUMBAR_ALPHA    1.0f

#define KUMBAR_WS_COUNT  9
#define KUMBAR_PADDING   10
#define KUMBAR_IPC_BUF   256

struct kumbar_output {
    struct wl_list               link;
    struct wl_output            *wl_output;
    char                         name[64];
    int                          width;
    int                          height;
    int                          scale;
    int                          active_ws;
    struct wl_surface           *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_buffer            *buffer;
    uint8_t                     *data;
    bool                         configured;
    bool                         dirty;
};

struct kumbar {
    struct wl_display          *display;
    struct wl_registry         *registry;
    struct wl_compositor       *compositor;
    struct wl_shm              *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zxdg_output_manager_v1 *xdg_output_manager;

    struct wl_list  outputs;
    bool            running;

    int             ipc_fd;
    char            ipc_ibuf[KUMBAR_IPC_BUF];
    int             ipc_ibuf_len;
};

void kumbar_render_output(struct kumbar *bar, struct kumbar_output *output);
void kumbar_ipc_connect(struct kumbar *bar);
void kumbar_ipc_dispatch(struct kumbar *bar);

#endif
