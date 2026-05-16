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
    uint32_t mod = KUM_MOD_KEY;

    kum_keybind_register(server, mod, XKB_KEY_q,
        action_quit, NULL);
    kum_keybind_register(server, mod, XKB_KEY_Tab,
        action_cycle_focus, NULL);
    kum_keybind_register(server, mod | WLR_MODIFIER_SHIFT, XKB_KEY_c,
        action_close_focused, NULL);
}
