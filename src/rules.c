#include "kumde.h"
#include <ctype.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void kum_rules_init(struct kum_server *server)
{
    wl_list_init(&server->rules);
}

void kum_rules_free(struct kum_server *server)
{
    struct kum_rule *r, *tmp;
    wl_list_for_each_safe(r, tmp, &server->rules, link) {
        wl_list_remove(&r->link);
        free(r);
    }
}

static char *rtrim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e-1))) e--;
    *e = '\0';
    return s;
}

void kum_rules_load(struct kum_server *server, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    bool in_rules = false;

    while (fgets(line, sizeof(line), f)) {
        char *s = rtrim(line);
        if (!s[0] || s[0] == '#') continue;

        if (strcmp(s, "[rules]") == 0) { in_rules = true; continue; }
        if (s[0] == '[') { in_rules = false; continue; }
        if (!in_rules) continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = rtrim(s);
        char *val = rtrim(eq + 1);
        if (strcmp(key, "rule") != 0) continue;

        struct kum_rule *r = calloc(1, sizeof(*r));
        r->workspace = -1;

        char valcopy[512];
        strncpy(valcopy, val, sizeof(valcopy) - 1);
        char *tok = strtok(valcopy, ",");
        while (tok) {
            tok = rtrim(tok);
            char *sep = strchr(tok, ':');
            if (sep) {
                *sep = '\0';
                char *rk = rtrim(tok);
                char *rv = rtrim(sep + 1);
                if      (!strcmp(rk,"app_id"))    strncpy(r->app_id, rv, 127);
                else if (!strcmp(rk,"title"))     strncpy(r->title,  rv, 127);
                else if (!strcmp(rk,"workspace")) r->workspace = atoi(rv) - 1;
                else if (!strcmp(rk,"floating"))  r->floating   = !strcmp(rv,"true");
                else if (!strcmp(rk,"fullscreen"))r->fullscreen = !strcmp(rv,"true");
                else if (!strcmp(rk,"size"))      sscanf(rv,"%dx%d",&r->w,&r->h);
                else if (!strcmp(rk,"pos"))       sscanf(rv,"%d,%d",&r->x,&r->y);
            }
            tok = strtok(NULL, ",");
        }

        if (r->app_id[0] || r->title[0])
            wl_list_insert(&server->rules, &r->link);
        else
            free(r);
    }
    fclose(f);
}

void kum_rules_apply(struct kum_server *server, struct kum_toplevel *tl)
{
    if (wl_list_empty(&server->rules)) return;

    const char *app_id = tl->xdg_toplevel->app_id  ? tl->xdg_toplevel->app_id  : "";
    const char *title  = tl->xdg_toplevel->title    ? tl->xdg_toplevel->title   : "";

    struct kum_rule *r;
    wl_list_for_each(r, &server->rules, link) {
        bool ma = !r->app_id[0] || fnmatch(r->app_id, app_id, 0) == 0;
        bool mt = !r->title[0]  || fnmatch(r->title,  title,  0) == 0;
        if (!ma || !mt) continue;

        if (r->workspace >= 0 && r->workspace < KUM_WORKSPACE_COUNT
                && r->workspace != tl->workspace)
            kum_workspace_move_toplevel(server, tl, r->workspace);

        if (r->floating)   tl->floating = true;
        if (r->w > 0 && r->h > 0)
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel, r->w, r->h);
        if (r->x || r->y)
            wlr_scene_node_set_position(&tl->scene_tree->node, r->x, r->y);
        if (r->fullscreen)
            wlr_xdg_toplevel_set_fullscreen(tl->xdg_toplevel, true);
        break;
    }
}

#ifdef KUM_XWAYLAND
void kum_rules_apply_xwayland(struct kum_server *server,
    struct kum_xwayland_surface *xs)
{
    if (wl_list_empty(&server->rules)) return;

    const char *app_id = xs->xwayland_surface->class ? xs->xwayland_surface->class : "";
    const char *title  = xs->xwayland_surface->title ? xs->xwayland_surface->title : "";

    struct kum_rule *r;
    wl_list_for_each(r, &server->rules, link) {
        bool ma = !r->app_id[0] || fnmatch(r->app_id, app_id, 0) == 0;
        bool mt = !r->title[0]  || fnmatch(r->title,  title,  0) == 0;
        if (!ma || !mt) continue;

        if (r->workspace >= 0 && r->workspace < KUM_WORKSPACE_COUNT
                && r->workspace != xs->workspace)
            kum_workspace_move_xwayland_surface(server, xs, r->workspace);

        if (r->w > 0 || r->h > 0 || r->x || r->y) {
            int x = r->x ? r->x : xs->xwayland_surface->x;
            int y = r->y ? r->y : xs->xwayland_surface->y;
            int w = r->w > 0 ? r->w : xs->xwayland_surface->width;
            int h = r->h > 0 ? r->h : xs->xwayland_surface->height;
            wlr_xwayland_surface_configure(xs->xwayland_surface, x, y, w, h);
            wlr_scene_node_set_position(&xs->scene_tree->node, x, y);
        }
        if (r->fullscreen)
            kum_xwayland_set_fullscreen(xs, true);
        break;
    }
}
#endif
