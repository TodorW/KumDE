#include "kumde.h"
#include <string.h>
#include <xkbcommon/xkbcommon.h>

static char *split_layouts[16];
static int   layout_count = 0;
static int   layout_current = 0;
static char  layout_buf[512];

static void parse_layouts(const char *layouts_str)
{
    layout_count = 0;
    strncpy(layout_buf, layouts_str, sizeof(layout_buf)-1);
    char *tok = strtok(layout_buf, ",");
    while (tok && layout_count < 16) {
        while (*tok == ' ') tok++;
        split_layouts[layout_count++] = tok;
        tok = strtok(NULL, ",");
    }
}

static void apply_layout(struct kum_server *server, int idx)
{
    struct kum_keyboard *kb;
    wl_list_for_each(kb, &server->keyboards, link) {
        struct xkb_rule_names names = {
            .rules   = NULL,
            .model   = server->cfg.kb_model[0]   ? server->cfg.kb_model   : NULL,
            .layout  = layout_count > 0           ? split_layouts[idx]     : server->cfg.kb_layout,
            .variant = server->cfg.kb_variant[0]  ? server->cfg.kb_variant : NULL,
            .options = server->cfg.kb_options[0]  ? server->cfg.kb_options : NULL,
        };
        struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap  *km  = xkb_keymap_new_from_names(ctx, &names,
            XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (km) {
            wlr_keyboard_set_keymap(kb->wlr_keyboard, km);
            xkb_keymap_unref(km);
        }
        xkb_context_unref(ctx);
    }
}

void kum_kb_layout_init(struct kum_server *server)
{
    if (server->cfg.kb_layout[0])
        parse_layouts(server->cfg.kb_layout);
    layout_current = 0;
}

void kum_kb_layout_next(struct kum_server *server)
{
    if (layout_count < 2) return;
    layout_current = (layout_current + 1) % layout_count;
    apply_layout(server, layout_current);

    char msg[64];
    snprintf(msg, sizeof(msg),
        "{\"event\":\"kb_layout\",\"layout\":\"%s\"}\n",
        split_layouts[layout_current]);
    kum_ipc_broadcast(server, msg, (int)strlen(msg));
}
