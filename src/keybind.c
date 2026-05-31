#include "kumde.h"
#include <stdlib.h>

static void action_quit(struct kum_server *server, void *data)
{
    wl_display_terminate(server->display);
}

static void action_cycle_focus(struct kum_server *server, void *data)
{
    if (wl_list_length(&server->toplevels) < 2)
        return;

    struct kum_toplevel *next =
        wl_container_of(server->toplevels.prev, next, link);
    kum_focus_toplevel(next, next->xdg_toplevel->base->surface);
}

static void action_close_focused(struct kum_server *server, void *data)
{
    if (!server->focused)
        return;
    wlr_xdg_toplevel_send_close(server->focused->xdg_toplevel);
}

static void action_toggle_floating(struct kum_server *server, void *data)
{
    if (!server->focused)
        return;

    struct kum_toplevel *tl = server->focused;
    struct kum_workspace *ws = &server->workspaces[tl->workspace];

    if (ws->layout != LAYOUT_TILE)
        return;

    tl->floating = !tl->floating;

    struct kum_output *output = kum_output_focused(server);
    kum_workspace_arrange(server, output, tl->workspace);
}

static void action_toggle_layout(struct kum_server *server, void *data)
{
    struct kum_output *output = kum_output_focused(server);
    kum_workspace_toggle_layout(server, output);
}

static void action_switch_workspace(struct kum_server *server, void *data)
{
    int index = (int)(intptr_t)data;
    struct kum_output *output = kum_output_focused(server);
    kum_workspace_switch(server, output, index);
}

static void action_move_to_workspace(struct kum_server *server, void *data)
{
    if (!server->focused)
        return;
    int index = (int)(intptr_t)data;
    kum_workspace_move_toplevel(server, server->focused, index);
}

void kum_keybind_register(struct kum_server *server, uint32_t modifiers,
                          xkb_keysym_t sym,
                          void (*action)(struct kum_server *, void *),
                          void *data)
{
    struct kum_keybind *kb = calloc(1, sizeof(*kb));
    kb->modifiers = modifiers;
    kb->sym       = sym;
    kb->action    = action;
    kb->data      = data;
    wl_list_insert(&server->keybinds, &kb->link);
}

bool kum_keybind_handle(struct kum_server *server, uint32_t modifiers,
                        xkb_keysym_t sym)
{
    struct kum_keybind *kb;
    wl_list_for_each(kb, &server->keybinds, link) {
        if (kb->modifiers == modifiers && kb->sym == sym) {
            kb->action(server, kb->data);
            return true;
        }
    }
    return false;
}

void kum_keybind_setup_defaults(struct kum_server *server)
{
    uint32_t mod   = KUM_MOD_KEY;
    uint32_t mod_s = KUM_MOD_KEY | WLR_MODIFIER_SHIFT;

    kum_keybind_register(server, mod,   XKB_KEY_q,     action_quit,             NULL);
    kum_keybind_register(server, mod,   XKB_KEY_Tab,   action_cycle_focus,      NULL);
    kum_keybind_register(server, mod_s, XKB_KEY_c,     action_close_focused,    NULL);
    kum_keybind_register(server, mod,   XKB_KEY_t,     action_toggle_layout,    NULL);
    kum_keybind_register(server, mod,   XKB_KEY_space, action_toggle_floating,  NULL);

    static const xkb_keysym_t ws_keys[KUM_WORKSPACE_COUNT] = {
        XKB_KEY_1, XKB_KEY_2, XKB_KEY_3, XKB_KEY_4, XKB_KEY_5,
        XKB_KEY_6, XKB_KEY_7, XKB_KEY_8, XKB_KEY_9,
    };

    for (int i = 0; i < KUM_WORKSPACE_COUNT; i++) {
        kum_keybind_register(server, mod,   ws_keys[i],
            action_switch_workspace,  (void *)(intptr_t)i);
        kum_keybind_register(server, mod_s, ws_keys[i],
            action_move_to_workspace, (void *)(intptr_t)i);
    }
}

static void action_focus_left(struct kum_server *s, void *d)
{ kum_output_focus_adjacent(s, -1, 0); }
static void action_focus_right(struct kum_server *s, void *d)
{ kum_output_focus_adjacent(s, 1, 0); }
static void action_focus_up(struct kum_server *s, void *d)
{ kum_output_focus_adjacent(s, 0, -1); }
static void action_focus_down(struct kum_server *s, void *d)
{ kum_output_focus_adjacent(s, 0, 1); }

static void action_scratchpad(struct kum_server *s, void *d)
{ kum_workspace_scratchpad_toggle(s); }

static void action_layout_monocle(struct kum_server *s, void *d)
{
    struct kum_output *o = kum_output_focused(s);
    if (!o) return;
    int idx = o->active_workspace;
    s->workspaces[idx].layout = LAYOUT_MONOCLE;
    kum_workspace_arrange(s, o, idx);
}

void kum_keybind_setup_extended(struct kum_server *server)
{
    uint32_t mod = KUM_MOD_KEY;
    kum_keybind_register(server, mod, XKB_KEY_Left,  action_focus_left,     NULL);
    kum_keybind_register(server, mod, XKB_KEY_Right, action_focus_right,    NULL);
    kum_keybind_register(server, mod, XKB_KEY_Up,    action_focus_up,       NULL);
    kum_keybind_register(server, mod, XKB_KEY_Down,  action_focus_down,     NULL);
    kum_keybind_register(server, mod, XKB_KEY_grave, action_scratchpad,     NULL);
    kum_keybind_register(server, mod, XKB_KEY_m,     action_layout_monocle, NULL);
}

static void action_kb_layout_next(struct kum_server *s, void *d)
{ kum_kb_layout_next(s); }

static void action_swap_master(struct kum_server *s, void *d)
{ kum_tiling_swap_master(s); }

static void action_resize_right(struct kum_server *s, void *d)
{ kum_tiling_resize(s, 40, 0); }
static void action_resize_left(struct kum_server *s, void *d)
{ kum_tiling_resize(s, -40, 0); }
static void action_resize_down(struct kum_server *s, void *d)
{ kum_tiling_resize(s, 0, 40); }
static void action_resize_up(struct kum_server *s, void *d)
{ kum_tiling_resize(s, 0, -40); }

static void action_screenshot(struct kum_server *s, void *d)
{
    if (fork() == 0) {
        setsid();
        execlp("kumshot", "kumshot", NULL);
        _exit(1);
    }
}

static void action_shutdown(struct kum_server *s, void *d)
{
    wl_display_terminate(s->display);
    if (fork() == 0) {
        setsid();
        execlp("systemctl", "systemctl", "poweroff", NULL);
        _exit(1);
    }
}

void kum_keybind_setup_session(struct kum_server *server)
{
    uint32_t mod   = KUM_MOD_KEY;
    uint32_t mod_s = KUM_MOD_KEY | WLR_MODIFIER_SHIFT;

    kum_keybind_register(server, mod,   XKB_KEY_k, action_kb_layout_next, NULL);
    kum_keybind_register(server, mod,   XKB_KEY_Return, action_swap_master, NULL);
    kum_keybind_register(server, mod_s, XKB_KEY_Right, action_resize_right, NULL);
    kum_keybind_register(server, mod_s, XKB_KEY_Left,  action_resize_left,  NULL);
    kum_keybind_register(server, mod_s, XKB_KEY_Down,  action_resize_down,  NULL);
    kum_keybind_register(server, mod_s, XKB_KEY_Up,    action_resize_up,    NULL);
    kum_keybind_register(server, mod,   XKB_KEY_Print, action_screenshot,   NULL);
    kum_keybind_register(server, mod_s, XKB_KEY_q,     action_shutdown,     NULL);
}
