#include "kumde.h"
#include <stdlib.h>
#include <xkbcommon/xkbcommon.h>

static void keyboard_modifiers(struct wl_listener *listener, void *data)
{
    struct kum_keyboard *kb = wl_container_of(listener, kb, modifiers);
    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat,
        &kb->wlr_keyboard->modifiers);
}

static void keyboard_key(struct wl_listener *listener, void *data)
{
    struct kum_keyboard *kb     = wl_container_of(listener, kb, key);
    struct kum_server   *server = kb->server;
    struct wlr_keyboard_key_event *ev = data;

    uint32_t keycode = ev->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(
        kb->wlr_keyboard->xkb_state, keycode, &syms);

    uint32_t modifiers = wlr_keyboard_get_modifiers(kb->wlr_keyboard);
    bool handled = false;

    if (ev->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++)
            handled |= kum_keybind_handle(server, modifiers, syms[i]);
    }

    if (!handled) {
        wlr_seat_set_keyboard(server->seat, kb->wlr_keyboard);
        wlr_seat_keyboard_notify_key(server->seat,
            ev->time_msec, ev->keycode, ev->state);
    }
}

static void keyboard_destroy(struct wl_listener *listener, void *data)
{
    struct kum_keyboard *kb = wl_container_of(listener, kb, destroy);
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->destroy.link);
    wl_list_remove(&kb->link);
    free(kb);
}

static void new_keyboard(struct kum_server *server,
                         struct wlr_input_device *device)
{
    struct wlr_keyboard *wlr_kb = wlr_keyboard_from_input_device(device);

    struct kum_keyboard *kb = calloc(1, sizeof(*kb));
    kb->server       = server;
    kb->wlr_keyboard = wlr_kb;

    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap  *km  = xkb_keymap_new_from_names(ctx, NULL,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(wlr_kb, km);
    xkb_keymap_unref(km);
    xkb_context_unref(ctx);
    wlr_keyboard_set_repeat_info(wlr_kb, 25, 600);

    kb->modifiers.notify = keyboard_modifiers;
    wl_signal_add(&wlr_kb->events.modifiers, &kb->modifiers);
    kb->key.notify = keyboard_key;
    wl_signal_add(&wlr_kb->events.key, &kb->key);
    kb->destroy.notify = keyboard_destroy;
    wl_signal_add(&device->events.destroy, &kb->destroy);

    wlr_seat_set_keyboard(server->seat, wlr_kb);
    wl_list_insert(&server->keyboards, &kb->link);
}

static void new_pointer(struct kum_server *server,
                        struct wlr_input_device *device)
{
    wlr_cursor_attach_input_device(server->cursor, device);
}

void kum_new_input(struct wl_listener *listener, void *data)
{
    struct kum_server       *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: new_keyboard(server, device); break;
    case WLR_INPUT_DEVICE_POINTER:  new_pointer(server, device);  break;
    default: break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(server->seat, caps);
}

static const char *cursor_name_for_edges(enum wlr_edges edges)
{
    switch (edges) {
    case WLR_EDGE_TOP:                             return "n-resize";
    case WLR_EDGE_BOTTOM:                          return "s-resize";
    case WLR_EDGE_LEFT:                            return "w-resize";
    case WLR_EDGE_RIGHT:                           return "e-resize";
    case WLR_EDGE_TOP    | WLR_EDGE_LEFT:          return "nw-resize";
    case WLR_EDGE_TOP    | WLR_EDGE_RIGHT:         return "ne-resize";
    case WLR_EDGE_BOTTOM | WLR_EDGE_LEFT:          return "sw-resize";
    case WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT:         return "se-resize";
    default:                                       return "default";
    }
}

static enum wlr_edges edges_at_point(struct kum_toplevel *tl,
    double lx, double ly)
{
    int nx, ny;
    wlr_scene_node_coords(&tl->scene_tree->node, &nx, &ny);

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(tl->xdg_toplevel->base, &geo);

    int edge_px = 8;
    int rx = (int)lx - nx;
    int ry = (int)ly - ny;
    int w  = geo.width;
    int h  = geo.height;

    enum wlr_edges edges = WLR_EDGE_NONE;
    if (rx < edge_px)          edges |= WLR_EDGE_LEFT;
    else if (rx > w - edge_px) edges |= WLR_EDGE_RIGHT;
    if (ry < edge_px)          edges |= WLR_EDGE_TOP;
    else if (ry > h - edge_px) edges |= WLR_EDGE_BOTTOM;
    return edges;
}

static void process_cursor_motion(struct kum_server *server, uint32_t time)
{
    struct kum_toplevel *grabbed = NULL;
    struct kum_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link) {
        if (tl->grabbed) {
            grabbed = tl;
            break;
        }
    }

    if (grabbed) {
        double dx = server->cursor->x - grabbed->grab_x;
        double dy = server->cursor->y - grabbed->grab_y;

        if (grabbed->resize_edges == WLR_EDGE_NONE) {
            wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "grabbing");
            wlr_scene_node_set_position(&grabbed->scene_tree->node,
                grabbed->grab_geobox.x + (int)dx,
                grabbed->grab_geobox.y + (int)dy);
        } else {
            wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
                cursor_name_for_edges(grabbed->resize_edges));

            int new_x = grabbed->grab_geobox.x;
            int new_y = grabbed->grab_geobox.y;
            int new_w = grabbed->grab_geobox.width;
            int new_h = grabbed->grab_geobox.height;

            if (grabbed->resize_edges & WLR_EDGE_LEFT) {
                new_x += (int)dx;
                new_w -= (int)dx;
            } else if (grabbed->resize_edges & WLR_EDGE_RIGHT) {
                new_w += (int)dx;
            }
            if (grabbed->resize_edges & WLR_EDGE_TOP) {
                new_y += (int)dy;
                new_h -= (int)dy;
            } else if (grabbed->resize_edges & WLR_EDGE_BOTTOM) {
                new_h += (int)dy;
            }

            if (new_w < KUM_RESIZE_MIN_W) new_w = KUM_RESIZE_MIN_W;
            if (new_h < KUM_RESIZE_MIN_H) new_h = KUM_RESIZE_MIN_H;

            wlr_scene_node_set_position(&grabbed->scene_tree->node,
                new_x, new_y);
            wlr_xdg_toplevel_set_size(grabbed->xdg_toplevel, new_w, new_h);
            kum_border_update(grabbed, grabbed == server->focused);
        }
        return;
    }

    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct kum_toplevel *under = kum_toplevel_at(server,
        server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (under) {
        enum wlr_edges edges = edges_at_point(under,
            server->cursor->x, server->cursor->y);
        if (edges != WLR_EDGE_NONE) {
            wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
                cursor_name_for_edges(edges));
        } else {
            wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
        }

        if (server->cfg.focus_follows_mouse && under != server->focused)
            kum_focus_toplevel(under, surface);
    } else {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }

    if (surface) {
        wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(server->seat);
    }
}

void kum_cursor_motion(struct wl_listener *listener, void *data)
{
    struct kum_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *ev = data;
    wlr_cursor_move(server->cursor, &ev->pointer->base, ev->delta_x, ev->delta_y);
    process_cursor_motion(server, ev->time_msec);
}

void kum_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
    struct kum_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *ev = data;
    wlr_cursor_warp_absolute(server->cursor, &ev->pointer->base, ev->x, ev->y);
    process_cursor_motion(server, ev->time_msec);
}

void kum_cursor_button(struct wl_listener *listener, void *data)
{
    struct kum_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *ev = data;

    wlr_seat_pointer_notify_button(server->seat,
        ev->time_msec, ev->button, ev->state);

    if (ev->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        struct kum_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link)
            tl->grabbed = false;
        return;
    }

    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct kum_toplevel *tl = kum_toplevel_at(server,
        server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (tl)
        kum_focus_toplevel(tl, surface);
}

void kum_cursor_axis(struct wl_listener *listener, void *data)
{
    struct kum_server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *ev = data;
    wlr_seat_pointer_notify_axis(server->seat, ev->time_msec,
        ev->orientation, ev->delta, ev->delta_discrete,
        ev->source, ev->relative_direction);
}

void kum_cursor_frame(struct wl_listener *listener, void *data)
{
    struct kum_server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

void kum_request_cursor(struct wl_listener *listener, void *data)
{
    struct kum_server *server = wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *ev = data;
    if (ev->seat_client == server->seat->pointer_state.focused_client)
        wlr_cursor_set_surface(server->cursor, ev->surface,
            ev->hotspot_x, ev->hotspot_y);
}

void kum_request_set_selection(struct wl_listener *listener, void *data)
{
    struct kum_server *server =
        wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *ev = data;
    wlr_seat_set_selection(server->seat, ev->source, ev->serial);
}
