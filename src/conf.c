#include "kumde.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void kum_config_defaults(struct kum_runtime_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->terminal, "foot", sizeof(cfg->terminal) - 1);

    cfg->border_width       = KUM_BORDER_WIDTH;
    cfg->border_active[0]   = KUM_BORDER_ACTIVE_R;
    cfg->border_active[1]   = KUM_BORDER_ACTIVE_G;
    cfg->border_active[2]   = KUM_BORDER_ACTIVE_B;
    cfg->border_inactive[0] = KUM_BORDER_INACTIVE_R;
    cfg->border_inactive[1] = KUM_BORDER_INACTIVE_G;
    cfg->border_inactive[2] = KUM_BORDER_INACTIVE_B;
    cfg->anim_open_ms       = KUM_ANIM_OPEN_MS;
    cfg->anim_close_ms      = KUM_ANIM_CLOSE_MS;
    cfg->anim_focus_ms      = KUM_ANIM_FOCUS_MS;
    cfg->cursor_size        = KUM_CURSOR_SIZE;
    cfg->animations         = true;
    cfg->xwayland           = false;
    cfg->gap                = KUM_GAP;
    cfg->master_ratio       = KUM_MASTER_RATIO;
    cfg->corner_radius      = KUM_CORNER_RADIUS;
    cfg->shadow_radius      = KUM_SHADOW_RADIUS;
    cfg->shadow_alpha       = KUM_SHADOW_ALPHA;
    cfg->shadow_offset_x    = KUM_SHADOW_OFFSET_X;
    cfg->shadow_offset_y    = KUM_SHADOW_OFFSET_Y;
    cfg->shadows            = false;
    cfg->rounded_corners    = false;
    cfg->focus_follows_mouse = false;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        *end-- = '\0';
    return s;
}

static bool parse_bool(const char *val)
{
    return strcmp(val, "true") == 0 || strcmp(val, "1") == 0;
}

static void apply_key_value(struct kum_runtime_config *cfg,
                            const char *key, const char *val)
{
    if (strcmp(key, "terminal") == 0) {
        strncpy(cfg->terminal, val, sizeof(cfg->terminal) - 1);
    } else if (strcmp(key, "border_width") == 0) {
        cfg->border_width = atoi(val);
    } else if (strcmp(key, "border_active") == 0) {
        sscanf(val, "%f %f %f",
            &cfg->border_active[0],
            &cfg->border_active[1],
            &cfg->border_active[2]);
    } else if (strcmp(key, "border_inactive") == 0) {
        sscanf(val, "%f %f %f",
            &cfg->border_inactive[0],
            &cfg->border_inactive[1],
            &cfg->border_inactive[2]);
    } else if (strcmp(key, "anim_open_ms") == 0) {
        cfg->anim_open_ms = (float)atof(val);
    } else if (strcmp(key, "anim_close_ms") == 0) {
        cfg->anim_close_ms = (float)atof(val);
    } else if (strcmp(key, "anim_focus_ms") == 0) {
        cfg->anim_focus_ms = (float)atof(val);
    } else if (strcmp(key, "cursor_size") == 0) {
        cfg->cursor_size = atoi(val);
    } else if (strcmp(key, "animations") == 0) {
        cfg->animations = parse_bool(val);
    } else if (strcmp(key, "xwayland") == 0) {
        cfg->xwayland = parse_bool(val);
    } else if (strcmp(key, "gap") == 0) {
        cfg->gap = atoi(val);
    } else if (strcmp(key, "master_ratio") == 0) {
        cfg->master_ratio = (float)atof(val);
    } else if (strcmp(key, "corner_radius") == 0) {
        cfg->corner_radius = atoi(val);
    } else if (strcmp(key, "shadow_radius") == 0) {
        cfg->shadow_radius = atoi(val);
    } else if (strcmp(key, "shadow_alpha") == 0) {
        cfg->shadow_alpha = (float)atof(val);
    } else if (strcmp(key, "shadow_offset_x") == 0) {
        cfg->shadow_offset_x = atoi(val);
    } else if (strcmp(key, "shadow_offset_y") == 0) {
        cfg->shadow_offset_y = atoi(val);
    } else if (strcmp(key, "shadows") == 0) {
        cfg->shadows = parse_bool(val);
    } else if (strcmp(key, "focus_follows_mouse") == 0) {
        cfg->focus_follows_mouse = parse_bool(val);
    } else if (strcmp(key, "rounded_corners") == 0) {
        cfg->rounded_corners = parse_bool(val);
    } else {
        wlr_log(WLR_DEBUG, "conf: unknown key '%s'", key);
    }
}

bool kum_config_load(struct kum_runtime_config *cfg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    char line[512];
    int  lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *s = trim(line);

        if (s[0] == '\0' || s[0] == '#')
            continue;

        char *eq = strchr(s, '=');
        if (!eq) {
            wlr_log(WLR_DEBUG, "conf:%d: missing '='", lineno);
            continue;
        }

        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        apply_key_value(cfg, key, val);
    }

    fclose(f);
    wlr_log(WLR_INFO, "loaded config: %s", path);
    return true;
}
