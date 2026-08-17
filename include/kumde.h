#ifndef KUMDE_H
#define KUMDE_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle_notify_v1.h>
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
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#ifdef KUM_XWAYLAND
#include <wlr/xwayland.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "config.h"
#include "easing.h"
#include "jsonutil.h"
#include "runtime_config.h"

#define KUM_WORKSPACE_COUNT 9

typedef enum {
    LAYOUT_FLOATING = 0,
    LAYOUT_TILE,
    LAYOUT_MONOCLE,
} kum_layout_mode;

/* Leading member of both kum_toplevel and kum_xwayland_surface so any
 * scene_tree->node.data can be safely identified before it is cast --
 * the two structs are never stored in the same wl_list. */
typedef enum {
    KUM_NODE_XDG = 0,
    KUM_NODE_XWAYLAND,
} kum_node_kind;

struct kum_workspace {
    int              index;
    kum_layout_mode  layout;
    struct wl_list   toplevels;
    struct wlr_scene_tree *scene_tree;
};

struct kum_output {
    struct wl_list           link;
    struct kum_server       *server;
    struct wlr_output       *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wlr_box           usable_area;
    int                      active_workspace;
    int                      previous_workspace;
    struct wl_listener       frame;
    struct wl_listener       request_state;
    struct wl_listener       destroy;
};

struct kum_ipc {
    int              fd;
    char             path[256];
    struct wl_event_source *source;
};

struct kum_server {
    struct wl_display               *display;
    struct wlr_backend              *backend;
    struct wlr_renderer             *renderer;
    struct wlr_allocator            *allocator;
    struct wlr_compositor            *compositor;
    struct wlr_scene                *scene;
    struct wlr_scene_output_layout  *scene_layout;
    struct wlr_output_layout        *output_layout;
    struct wlr_xdg_shell            *xdg_shell;
    struct wlr_layer_shell_v1       *layer_shell;
    struct wlr_cursor               *cursor;
    struct wlr_xcursor_manager      *cursor_mgr;
    struct wlr_seat                 *seat;
    struct wlr_idle_notifier_v1     *idle_notifier;

    struct wl_list  outputs;
    struct wl_list  toplevels;
    struct wl_list  keyboards;
    struct wl_list  pointers;
    struct wl_list  keybinds;
    struct wl_list  ipc_clients;
    struct wl_list  layer_surfaces;

    struct kum_workspace  workspaces[KUM_WORKSPACE_COUNT];
    struct kum_ipc        ipc;
    struct kum_runtime_config cfg;

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
    struct wl_list       rules;
    int                  scratchpad_ws;
    uint64_t             next_focus_serial;

#ifdef KUM_XWAYLAND
    struct wlr_xwayland      *xwayland;
    struct wl_listener        xwayland_ready;
    struct wl_listener        new_xwayland_surface;
    struct wl_list            xwayland_surfaces;
    struct kum_xwayland_surface *focused_xwayland;
#endif
};

struct kum_toplevel {
    kum_node_kind            kind;
    uint64_t                 focus_serial;
    struct wl_list           link;
    struct wl_list           workspace_link;
    struct kum_server       *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree   *scene_tree;
    struct wlr_scene_rect   *border[4];
    struct wlr_scene_buffer *shadow_buf;
    struct wlr_scene_buffer *corner_mask_buf;
    kum_animation            anim;
    float                    opacity;
    float                    scale;
    bool                     grabbed;
    bool                     floating;
    int                      workspace;
    enum wlr_edges           resize_edges;
    int                      grab_x;
    int                      grab_y;
    struct wlr_box           grab_geobox;
    struct wlr_box           saved_geom;
    struct wlr_box           last_geo;
    struct wl_listener       map;
    struct wl_listener       unmap;
    struct wl_listener       destroy;
    struct wl_listener       request_move;
    struct wl_listener       request_resize;
    struct wl_listener       request_maximize;
    struct wl_listener       request_fullscreen;
    struct wl_listener       set_title;
    struct wl_listener       commit;
};

struct kum_keyboard {
    struct wl_list       link;
    struct kum_server   *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener   modifiers;
    struct wl_listener   key;
    struct wl_listener   destroy;
};

struct kum_pointer {
    struct wl_list            link;
    struct kum_server        *server;
    struct wlr_input_device  *device;
    struct wl_listener        destroy;
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

struct kum_ipc_client {
    struct wl_list          link;
    struct kum_server      *server;
    int                     fd;
    struct wl_event_source *source;
};

#ifdef KUM_XWAYLAND
struct kum_xwayland_surface {
    kum_node_kind                 kind;
    uint64_t                      focus_serial;
    struct wl_list                link;
    struct kum_server            *server;
    struct wlr_xwayland_surface  *xwayland_surface;
    struct wlr_scene_tree        *scene_tree;
    struct wlr_scene_rect        *border[4];
    struct wlr_scene_buffer      *shadow_buf;
    struct wlr_scene_buffer      *corner_mask_buf;
    kum_animation                 anim;
    int                           workspace;
    bool                          grabbed;
    enum wlr_edges                resize_edges;
    int                           grab_x;
    int                           grab_y;
    struct wlr_box                grab_geobox;
    struct wlr_box                saved_geom;
    int                           last_width;
    int                           last_height;
    bool                          associated;
    struct wl_listener            associate;
    struct wl_listener            dissociate;
    struct wl_listener            map;
    struct wl_listener            unmap;
    struct wl_listener            commit;
    struct wl_listener            destroy;
    struct wl_listener            request_move;
    struct wl_listener            request_resize;
    struct wl_listener            request_maximize;
    struct wl_listener            request_fullscreen;
    struct wl_listener            set_title;
};
#endif

void kum_server_init(struct kum_server *server);
void kum_server_run(struct kum_server *server);
void kum_server_finish(struct kum_server *server);
void kum_server_reload_config(struct kum_server *server);

void kum_new_output(struct wl_listener *listener, void *data);
struct kum_output *kum_output_focused(struct kum_server *server);

void kum_new_xdg_toplevel(struct wl_listener *listener, void *data);
void kum_new_xdg_popup(struct wl_listener *listener, void *data);
void kum_focus_toplevel(struct kum_toplevel *toplevel, struct wlr_surface *surface);
void kum_toplevel_set_fullscreen(struct kum_toplevel *tl, bool fs);
void kum_toplevel_toggle_fullscreen(struct kum_toplevel *tl);
struct kum_toplevel *kum_toplevel_at(struct kum_server *server,
    double lx, double ly, struct wlr_surface **surface,
    double *sx, double *sy);
void *kum_scene_node_at(struct kum_server *server,
    double lx, double ly, struct wlr_surface **surface,
    double *sx, double *sy, kum_node_kind *kind_out);

void kum_new_input(struct wl_listener *listener, void *data);
void kum_cursor_motion(struct wl_listener *listener, void *data);
void kum_cursor_motion_absolute(struct wl_listener *listener, void *data);
void kum_cursor_button(struct wl_listener *listener, void *data);
void kum_cursor_axis(struct wl_listener *listener, void *data);
void kum_cursor_frame(struct wl_listener *listener, void *data);
void kum_request_cursor(struct wl_listener *listener, void *data);
void kum_request_set_selection(struct wl_listener *listener, void *data);

void kum_border_create(struct kum_toplevel *toplevel);
void kum_border_update(struct kum_toplevel *toplevel, bool focused);
void kum_border_destroy(struct kum_toplevel *toplevel);

void kum_shadow_create(struct kum_toplevel *toplevel);
void kum_shadow_update(struct kum_toplevel *toplevel);
void kum_shadow_destroy(struct kum_toplevel *toplevel);

void kum_corners_apply(struct kum_toplevel *toplevel);
void kum_corners_destroy(struct kum_toplevel *toplevel);

void kum_new_layer_surface(struct wl_listener *listener, void *data);

void kum_keybind_register(struct kum_server *server, uint32_t modifiers,
                          xkb_keysym_t sym,
                          void (*action)(struct kum_server *, void *),
                          void *data);
bool kum_keybind_handle(struct kum_server *server, uint32_t modifiers,
                        xkb_keysym_t sym);
void kum_keybind_setup_defaults(struct kum_server *server);

void kum_workspace_init(struct kum_server *server);
void kum_workspace_switch(struct kum_server *server,
    struct kum_output *output, int index);
void kum_workspace_move_toplevel(struct kum_server *server,
    struct kum_toplevel *tl, int index);
void kum_workspace_arrange(struct kum_server *server,
    struct kum_output *output, int ws_index);
void kum_workspace_toggle_layout(struct kum_server *server,
    struct kum_output *output);

void kum_ipc_init(struct kum_server *server);
void kum_ipc_finish(struct kum_server *server);
void kum_ipc_broadcast(struct kum_server *server, const char *msg, int len);
void kum_ipc_broadcast_occupancy(struct kum_server *server);
void kum_ipc_broadcast_window_title(struct kum_server *server,
    int workspace, const char *title);

#ifdef KUM_XWAYLAND
void kum_xwayland_init(struct kum_server *server);
void kum_xwayland_finish(struct kum_server *server);
void kum_focus_xwayland_surface(struct kum_xwayland_surface *xs);
void kum_xwayland_border_create(struct kum_xwayland_surface *xs);
void kum_xwayland_border_update(struct kum_xwayland_surface *xs, bool focused);
void kum_xwayland_border_destroy(struct kum_xwayland_surface *xs);
void kum_xwayland_shadow_create(struct kum_xwayland_surface *xs);
void kum_xwayland_shadow_destroy(struct kum_xwayland_surface *xs);
void kum_xwayland_corners_apply(struct kum_xwayland_surface *xs);
void kum_xwayland_corners_destroy(struct kum_xwayland_surface *xs);
void kum_xwayland_set_fullscreen(struct kum_xwayland_surface *xs, bool fs);
void kum_xwayland_toggle_fullscreen(struct kum_xwayland_surface *xs);
void kum_workspace_move_xwayland_surface(struct kum_server *server,
    struct kum_xwayland_surface *xs, int index);
#endif

struct kum_rule {
    struct wl_list  link;
    char            app_id[128];
    char            title[128];
    int             workspace;
    bool            floating;
    bool            fullscreen;
    int             w, h;
    int             x, y;
};

void kum_rules_init(struct kum_server *server);
void kum_rules_free(struct kum_server *server);
void kum_rules_load(struct kum_server *server, const char *path);
void kum_rules_apply(struct kum_server *server, struct kum_toplevel *tl);

void kum_output_focus_adjacent(struct kum_server *server, int dx, int dy);

void kum_workspace_scratchpad_toggle(struct kum_server *server);
void kum_workspace_monocle_arrange(struct kum_server *server,
    struct kum_output *output, int ws_index);
void kum_keybind_setup_extended(struct kum_server *server);

void kum_input_configure(struct kum_server *server);
void kum_input_configure_device(struct kum_server *server,
    struct wlr_input_device *device);
void kum_output_configure_all(struct kum_server *server);
void kum_autostart_run(struct kum_server *server);
void kum_kb_layout_next(struct kum_server *server);
void kum_tiling_swap_master(struct kum_server *server);
void kum_tiling_resize(struct kum_server *server, int dw, int dh);
void kum_kb_layout_init(struct kum_server *server);
void kum_keybind_setup_session(struct kum_server *server);

#ifdef KUM_XWAYLAND
void kum_rules_apply_xwayland(struct kum_server *server,
    struct kum_xwayland_surface *xs);
#endif

#endif
