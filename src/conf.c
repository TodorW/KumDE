#include "kumde.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void kum_config_defaults(struct kum_runtime_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->terminal,       "foot",      sizeof(cfg->terminal)       - 1);
    strncpy(cfg->kb_layout,      KUM_KB_LAYOUT,   sizeof(cfg->kb_layout)  - 1);
    strncpy(cfg->kb_variant,     KUM_KB_VARIANT,  sizeof(cfg->kb_variant) - 1);
    strncpy(cfg->kb_model,       KUM_KB_MODEL,    sizeof(cfg->kb_model)   - 1);
    strncpy(cfg->kb_options,     KUM_KB_OPTIONS,  sizeof(cfg->kb_options) - 1);

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
    cfg->focus_follows_mouse= false;
    cfg->tap_to_click       = KUM_TAP_TO_CLICK;
    cfg->natural_scroll     = KUM_NATURAL_SCROLL;
    cfg->disable_while_typing = KUM_DISABLE_WHILE_TYPE;
    cfg->pointer_accel      = KUM_POINTER_ACCEL;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e-1))) e--;
    *e = '\0';
    return s;
}

static bool parse_bool(const char *v)
{
    return !strcmp(v,"true") || !strcmp(v,"1") || !strcmp(v,"yes");
}

static void apply_global(struct kum_runtime_config *cfg,
    const char *key, const char *val)
{
    if      (!strcmp(key,"terminal"))          strncpy(cfg->terminal,   val, 255);
    else if (!strcmp(key,"border_width"))      cfg->border_width    = atoi(val);
    else if (!strcmp(key,"border_active"))     sscanf(val,"%f %f %f",
        &cfg->border_active[0], &cfg->border_active[1], &cfg->border_active[2]);
    else if (!strcmp(key,"border_inactive"))   sscanf(val,"%f %f %f",
        &cfg->border_inactive[0], &cfg->border_inactive[1], &cfg->border_inactive[2]);
    else if (!strcmp(key,"anim_open_ms"))      cfg->anim_open_ms    = (float)atof(val);
    else if (!strcmp(key,"anim_close_ms"))     cfg->anim_close_ms   = (float)atof(val);
    else if (!strcmp(key,"anim_focus_ms"))     cfg->anim_focus_ms   = (float)atof(val);
    else if (!strcmp(key,"cursor_size"))       cfg->cursor_size     = atoi(val);
    else if (!strcmp(key,"animations"))        cfg->animations      = parse_bool(val);
    else if (!strcmp(key,"xwayland"))          cfg->xwayland        = parse_bool(val);
    else if (!strcmp(key,"gap"))               cfg->gap             = atoi(val);
    else if (!strcmp(key,"master_ratio"))      cfg->master_ratio    = (float)atof(val);
    else if (!strcmp(key,"corner_radius"))     cfg->corner_radius   = atoi(val);
    else if (!strcmp(key,"shadow_radius"))     cfg->shadow_radius   = atoi(val);
    else if (!strcmp(key,"shadow_alpha"))      cfg->shadow_alpha    = (float)atof(val);
    else if (!strcmp(key,"shadow_offset_x"))   cfg->shadow_offset_x = atoi(val);
    else if (!strcmp(key,"shadow_offset_y"))   cfg->shadow_offset_y = atoi(val);
    else if (!strcmp(key,"shadows"))           cfg->shadows         = parse_bool(val);
    else if (!strcmp(key,"rounded_corners"))   cfg->rounded_corners = parse_bool(val);
    else if (!strcmp(key,"focus_follows_mouse"))cfg->focus_follows_mouse = parse_bool(val);
    else if (!strcmp(key,"autostart")) {
        if (cfg->autostart_count < 16)
            strncpy(cfg->autostart[cfg->autostart_count++], val, 255);
    }
}

static void apply_input(struct kum_runtime_config *cfg,
    const char *key, const char *val)
{
    if      (!strcmp(key,"tap_to_click"))         cfg->tap_to_click        = parse_bool(val);
    else if (!strcmp(key,"natural_scroll"))        cfg->natural_scroll      = parse_bool(val);
    else if (!strcmp(key,"disable_while_typing"))  cfg->disable_while_typing= parse_bool(val);
    else if (!strcmp(key,"pointer_accel"))         cfg->pointer_accel       = (float)atof(val);
    else if (!strcmp(key,"kb_layout"))             strncpy(cfg->kb_layout,  val, 63);
    else if (!strcmp(key,"kb_variant"))            strncpy(cfg->kb_variant, val, 63);
    else if (!strcmp(key,"kb_model"))              strncpy(cfg->kb_model,   val, 63);
    else if (!strcmp(key,"kb_options"))            strncpy(cfg->kb_options, val, 127);
}

static void apply_output(kum_output_config *oc,
    const char *key, const char *val)
{
    oc->scale   = oc->scale   > 0 ? oc->scale   : 1.0f;
    oc->enabled = true;

    if      (!strcmp(key,"mode")) {
        int r = 0;
        sscanf(val, "%dx%d@%d", &oc->width, &oc->height, &r);
        if (r == 0) sscanf(val, "%dx%d", &oc->width, &oc->height);
        oc->refresh = r;
    }
    else if (!strcmp(key,"pos"))       sscanf(val, "%d,%d", &oc->x, &oc->y);
    else if (!strcmp(key,"scale"))     oc->scale   = (float)atof(val);
    else if (!strcmp(key,"transform")) strncpy(oc->transform, val, 15);
    else if (!strcmp(key,"enabled"))   oc->enabled = parse_bool(val);
}

bool kum_config_load(struct kum_runtime_config *cfg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[512];
    int  lineno   = 0;
    int  section  = 0;
    int  out_idx  = -1;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *s = trim(line);
        if (!s[0] || s[0] == '#') continue;

        if (s[0] == '[') {
            section = 0; out_idx = -1;
            if (!strcmp(s,"[input]")) { section=1; }
            else if (!strcmp(s,"[rules]")) { section=2; }
            else if (!strncmp(s,"[output ",8)) {
                section = 3;
                if (cfg->output_config_count < 8) {
                    out_idx = cfg->output_config_count++;
                    memset(&cfg->output_configs[out_idx],0,sizeof(kum_output_config));
                    cfg->output_configs[out_idx].scale = 1.0f;
                    char oname[64]={0};
                    sscanf(s,"[output %63[^]]", oname);
                    strncpy(cfg->output_configs[out_idx].name, oname, 63);
                }
            }
            continue;
        }

        if (section == 2) continue;

        char *eq = strchr(s,'=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq+1);

        if (section == 0)      apply_global(cfg, key, val);
        else if (section == 1) apply_input(cfg, key, val);
        else if (section == 3 && out_idx >= 0)
            apply_output(&cfg->output_configs[out_idx], key, val);
    }

    fclose(f);
    wlr_log(WLR_INFO, "loaded config: %s", path);
    return true;
}
