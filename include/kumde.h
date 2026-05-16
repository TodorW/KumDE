#ifndef KUMDE_H
#define KUMDE_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "config.h"

typedef enum {
    ANIM_NONE = 0,
    ANIM_OPENING,
    ANIM_CLOSING,
    ANIM_FOCUSING,
} kum_anim_type;

typedef struct {
    kum_anim_type   type;
    struct timespec start;
    float           duration_ms;
    float           from;
    float           to;
    float           current;
    bool            done;
} kum_animation;

struct kum_server {
    struct wl_display               *display;
    struct wlr_backend              *backend;
    struct wlr_renderer             *renderer;
    struct wlr_allocator            *allocator;
    struct wlr_scene                *scene;
    struct wlr_scene_output_layout  *scene_layout;
    struct wlr_output_layout        *output_layout;
    struct wlr_xdg_shell            *xdg_shell;
    struct wlr_layer_shell_v1       *layer_shell;
    struct wlr_cursor               *cursor;
    struct wlr_xcursor_manager      *cursor_mgr;
    struct wlr_seat                 *seat;

    struct wl_list  outputs;
    struct wl_list  toplevels;
    struct wl_list  keyboards;
    struct wl_list  keybinds;

    struct wl_listener  new_output;
    struct wl_listener  new_xdg_toplevel;
    struct wl_listener  new_xdg_popup;
    struct wl_listener  new_layer_surface;
    struct wl_listener  new_input;
    struct wl_listener  cursor_motion;
    struct wl_listener  cursor_motion_absolute;
    struct wl_listener  cursor_button;
    struct wl_listener  cursor_axis;
    struct wl_listener  cursor_frame;
    struct wl_listener  request_cursor;
    struct wl_listener  request_set_selection;

    struct kum_toplevel *focused;
};

struct kum_output {
    struct wl_list           link;
    struct kum_server       *server;
    struct wlr_output       *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wl_listener       frame;
    struct wl_listener       request_state;
    struct wl_listener       destroy;
};

struct kum_toplevel {
    struct wl_list           link;
    struct kum_server       *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree   *scene_tree;
    struct wlr_scene_rect   *border[4];
    kum_animation            anim;
    float                    opacity;
    float                    scale;
    bool                     grabbed;
    enum wlr_edges           resize_edges;
    int                      grab_x;
    int                      grab_y;
    struct wlr_box           grab_geobox;
    struct wl_listener       map;
    struct wl_listener       unmap;
    struct wl_listener       destroy;
    struct wl_listener       request_move;
    struct wl_listener       request_resize;
    struct wl_listener       request_maximize;
    struct wl_listener       request_fullscreen;
    struct wl_listener       set_title;
};

struct kum_keyboard {
    struct wl_list       link;
    struct kum_server   *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener   modifiers;
    struct wl_listener   key;
    struct wl_listener   destroy;
};

struct kum_popup {
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener    commit;
    struct wl_listener    destroy;
};

struct kum_keybind {
    struct wl_list  link;
    uint32_t        modifiers;
    xkb_keysym_t    sym;
    void          (*action)(struct kum_server *server, void *data);
    void           *data;
};

void kum_server_init(struct kum_server *server);
void kum_server_run(struct kum_server *server);
void kum_server_finish(struct kum_server *server);

void kum_new_output(struct wl_listener *listener, void *data);

void kum_new_xdg_toplevel(struct wl_listener *listener, void *data);
void kum_new_xdg_popup(struct wl_listener *listener, void *data);
void kum_focus_toplevel(struct kum_toplevel *toplevel, struct wlr_surface *surface);
struct kum_toplevel *kum_toplevel_at(struct kum_server *server,
    double lx, double ly, struct wlr_surface **surface,
    double *sx, double *sy);

void kum_new_input(struct wl_listener *listener, void *data);
void kum_cursor_motion(struct wl_listener *listener, void *data);
void kum_cursor_motion_absolute(struct wl_listener *listener, void *data);
void kum_cursor_button(struct wl_listener *listener, void *data);
void kum_cursor_axis(struct wl_listener *listener, void *data);
void kum_cursor_frame(struct wl_listener *listener, void *data);
void kum_request_cursor(struct wl_listener *listener, void *data);
void kum_request_set_selection(struct wl_listener *listener, void *data);

void kum_anim_start(kum_animation *anim, kum_anim_type type,
                    float from, float to, float duration_ms);
void kum_anim_tick(kum_animation *anim);
float kum_ease_out_cubic(float t);
float kum_ease_in_cubic(float t);
float kum_ease_in_out_cubic(float t);
float kum_ease_out_back(float t);

void kum_border_create(struct kum_toplevel *toplevel);
void kum_border_update(struct kum_toplevel *toplevel, bool focused);
void kum_border_destroy(struct kum_toplevel *toplevel);

void kum_new_layer_surface(struct wl_listener *listener, void *data);

void kum_keybind_register(struct kum_server *server, uint32_t modifiers,
                          xkb_keysym_t sym,
                          void (*action)(struct kum_server *, void *),
                          void *data);
bool kum_keybind_handle(struct kum_server *server, uint32_t modifiers,
                        xkb_keysym_t sym);
void kum_keybind_setup_defaults(struct kum_server *server);

#endif
