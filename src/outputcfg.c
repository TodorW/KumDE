#include "kumde.h"
#include <string.h>

static const kum_output_config *find_config(struct kum_server *server,
    const char *name)
{
    for (int i = 0; i < server->cfg.output_config_count; i++) {
        if (strcmp(server->cfg.output_configs[i].name, name) == 0)
            return &server->cfg.output_configs[i];
    }
    return NULL;
}

static enum wl_output_transform parse_transform(const char *s)
{
    if (!s || !s[0])              return WL_OUTPUT_TRANSFORM_NORMAL;
    if (!strcmp(s, "90"))         return WL_OUTPUT_TRANSFORM_90;
    if (!strcmp(s, "180"))        return WL_OUTPUT_TRANSFORM_180;
    if (!strcmp(s, "270"))        return WL_OUTPUT_TRANSFORM_270;
    if (!strcmp(s, "flipped"))    return WL_OUTPUT_TRANSFORM_FLIPPED;
    if (!strcmp(s, "flipped-90")) return WL_OUTPUT_TRANSFORM_FLIPPED_90;
    return WL_OUTPUT_TRANSFORM_NORMAL;
}

void kum_output_configure_all(struct kum_server *server)
{
    struct kum_output *o;
    wl_list_for_each(o, &server->outputs, link) {
        const char *name = o->wlr_output->name;
        const kum_output_config *cfg = find_config(server, name);
        if (!cfg)
            continue;

        struct wlr_output_state state;
        wlr_output_state_init(&state);

        if (!cfg->enabled) {
            wlr_output_state_set_enabled(&state, false);
            wlr_output_commit_state(o->wlr_output, &state);
            wlr_output_state_finish(&state);
            continue;
        }

        wlr_output_state_set_enabled(&state, true);

        if (cfg->width > 0 && cfg->height > 0) {
            struct wlr_output_mode *mode = NULL;
            struct wlr_output_mode *m;
            wl_list_for_each(m, &o->wlr_output->modes, link) {
                if (m->width == cfg->width && m->height == cfg->height) {
                    if (!mode || (cfg->refresh > 0 &&
                            abs(m->refresh - cfg->refresh * 1000) <
                            abs(mode->refresh - cfg->refresh * 1000)))
                        mode = m;
                }
            }
            if (mode)
                wlr_output_state_set_mode(&state, mode);
        }

        if (cfg->scale > 0.0f)
            wlr_output_state_set_scale(&state, cfg->scale);

        if (cfg->transform[0])
            wlr_output_state_set_transform(&state,
                parse_transform(cfg->transform));

        wlr_output_commit_state(o->wlr_output, &state);
        wlr_output_state_finish(&state);

        if (cfg->x >= 0 || cfg->y >= 0) {
            wlr_output_layout_add(server->output_layout,
                o->wlr_output, cfg->x, cfg->y);
            wlr_output_layout_get_box(server->output_layout,
                o->wlr_output, &o->usable_area);
        }
    }
}
