#ifndef KUM_RUNTIME_CONFIG_H
#define KUM_RUNTIME_CONFIG_H

#include <stdbool.h>

/* kumde.conf parsing: pure data + string/file parsing, no wlroots
 * dependency, safe to unit test directly (see tests/test_conf.c). */

typedef struct {
    char    name[64];
    int     width;
    int     height;
    int     refresh;
    int     x;
    int     y;
    float   scale;
    bool    enabled;
    char    transform[16];
} kum_output_config;

struct kum_runtime_config {
    char    terminal[256];
    int     border_width;
    float   border_active[3];
    float   border_inactive[3];
    float   anim_open_ms;
    float   anim_close_ms;
    float   anim_focus_ms;
    int     cursor_size;
    bool    xwayland;
    bool    animations;
    int     gap;
    float   master_ratio;
    int     corner_radius;
    int     shadow_radius;
    float   shadow_alpha;
    int     shadow_offset_x;
    int     shadow_offset_y;
    bool    shadows;
    bool    rounded_corners;
    bool    focus_follows_mouse;
    char    kb_layout[64];
    char    kb_variant[64];
    char    kb_model[64];
    char    kb_options[128];
    bool    tap_to_click;
    bool    natural_scroll;
    bool    disable_while_typing;
    float   pointer_accel;
    char    autostart[16][256];
    int     autostart_count;
    kum_output_config output_configs[8];
    int     output_config_count;
};

bool kum_config_load(struct kum_runtime_config *cfg, const char *path);
void kum_config_defaults(struct kum_runtime_config *cfg);

#endif
