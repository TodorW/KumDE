#include "kumde.h"
#include <stdlib.h>

/* ext-session-lock-v1 is security-sensitive: once the locked event has been
 * sent, the compositor MUST stop rendering/routing input to normal clients,
 * and if the lock client dies abnormally the session MUST stay locked
 * (fail-secure) rather than falling back to unlocked. See the protocol XML's
 * <interface name="ext_session_lock_v1"> description for the full contract
 * this file implements. */

struct kum_lock_surface {
    struct kum_server                  *server;
    struct wlr_scene_tree              *scene_tree;
    struct wl_listener                  destroy;
};

static void lock_surface_destroy(struct wl_listener *listener, void *data)
{
    struct kum_lock_surface *ls =
        wl_container_of(listener, ls, destroy);
    wl_list_remove(&ls->destroy.link);
    wlr_scene_node_destroy(&ls->scene_tree->node);
    free(ls);
}

/* Split out because kum_server has no listener slot per-lock (only one lock
 * can be active at a time, tracked via server->active_lock), so these are
 * wired up fresh on every new_lock rather than being permanent members. */
struct kum_active_lock {
    struct kum_server   *server;
    struct wl_listener   new_surface;
    struct wl_listener   unlock;
    struct wl_listener   destroy;
};

static void active_lock_new_surface(struct wl_listener *listener, void *data)
{
    struct kum_active_lock *al =
        wl_container_of(listener, al, new_surface);
    struct kum_server *server = al->server;
    struct wlr_session_lock_surface_v1 *lock_surface = data;

    struct kum_output *output = NULL, *o;
    wl_list_for_each(o, &server->outputs, link) {
        if (o->wlr_output == lock_surface->output) {
            output = o;
            break;
        }
    }
    if (!output)
        return;

    struct kum_lock_surface *ls = calloc(1, sizeof(*ls));
    ls->server     = server;
    ls->scene_tree = wlr_scene_tree_create(server->lock_tree);

    struct wlr_scene_surface *scene_surface = wlr_scene_surface_create(
        ls->scene_tree, lock_surface->surface);
    (void)scene_surface;

    struct wlr_box obox;
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &obox);
    wlr_scene_node_set_position(&ls->scene_tree->node, obox.x, obox.y);

    ls->destroy.notify = lock_surface_destroy;
    wl_signal_add(&lock_surface->events.destroy, &ls->destroy);

    /* Attaching/committing a buffer before the first configure event is a
     * protocol error, so this must happen synchronously here rather than
     * waiting for any client commit. */
    wlr_session_lock_surface_v1_configure(lock_surface, obox.width, obox.height);

    if (server->seat->keyboard_state.focused_surface != lock_surface->surface) {
        struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
        wlr_seat_keyboard_notify_enter(server->seat, lock_surface->surface,
            kb ? kb->keycodes : NULL, kb ? kb->num_keycodes : 0,
            kb ? &kb->modifiers : NULL);
    }
}

static void active_lock_unlock(struct wl_listener *listener, void *data)
{
    struct kum_active_lock *al = wl_container_of(listener, al, unlock);
    struct kum_server *server = al->server;

    server->locked = false;

    struct wlr_scene_node *node, *tmp;
    wl_list_for_each_safe(node, tmp, &server->lock_tree->children, link)
        wlr_scene_node_destroy(node);

    /* Deliberately don't try to restore keyboard focus to server->focused
     * here: kum_focus_toplevel()/kum_focus_xwayland_surface() now both
     * refuse to run while server->locked is set, and by the time a caller
     * could safely invoke them again the normal focus-follows-mouse/click
     * paths will naturally refocus whatever's under the cursor. */
    wlr_seat_keyboard_clear_focus(server->seat);
}

static void active_lock_destroy(struct wl_listener *listener, void *data)
{
    struct kum_active_lock *al = wl_container_of(listener, al, destroy);
    struct kum_server *server = al->server;

    wl_list_remove(&al->new_surface.link);
    wl_list_remove(&al->unlock.link);
    wl_list_remove(&al->destroy.link);

    server->active_lock = NULL;

    /* If the client is gone but never sent unlock, this is an abnormal
     * termination while locked: per protocol, the compositor must NOT
     * unlock in response. server->locked and the blanking/lock-surface
     * scene content are deliberately left exactly as they are. */

    free(al);
}

static void new_session_lock(struct wl_listener *listener, void *data)
{
    struct kum_server *server =
        wl_container_of(listener, server, new_session_lock);
    struct wlr_session_lock_v1 *lock = data;

    if (server->active_lock || server->locked) {
        /* Only one lock may be active at a time; refuse the new one
         * immediately (this sends the "finished" event to the client). */
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    struct kum_active_lock *al = calloc(1, sizeof(*al));
    al->server = server;
    al->new_surface.notify = active_lock_new_surface;
    al->unlock.notify      = active_lock_unlock;
    al->destroy.notify     = active_lock_destroy;
    wl_signal_add(&lock->events.new_surface, &al->new_surface);
    wl_signal_add(&lock->events.unlock,      &al->unlock);
    wl_signal_add(&lock->events.destroy,     &al->destroy);

    server->active_lock = lock;
    server->locked      = true;

    struct kum_output *o;
    wl_list_for_each(o, &server->outputs, link) {
        struct wlr_box obox;
        wlr_output_layout_get_box(server->output_layout, o->wlr_output, &obox);

        struct wlr_scene_rect *rect = wlr_scene_rect_create(server->lock_tree,
            obox.width, obox.height, (float[4]){ 0.f, 0.f, 0.f, 1.f });
        wlr_scene_node_set_position(&rect->node, obox.x, obox.y);
    }

    kum_session_lock_raise(server);
    wlr_seat_keyboard_clear_focus(server->seat);

    /* The scene already reflects the blanking synchronously at this point
     * (it will be included in the very next rendered frame), so it's safe
     * to confirm locked now rather than deferring to a frame callback. */
    wlr_session_lock_v1_send_locked(lock);
}

void kum_session_lock_raise(struct kum_server *server)
{
    wlr_scene_node_raise_to_top(&server->lock_tree->node);
}

void kum_session_lock_init(struct kum_server *server)
{
    server->lock_tree = wlr_scene_tree_create(&server->scene->tree);

    server->session_lock_manager =
        wlr_session_lock_manager_v1_create(server->display);
    server->new_session_lock.notify = new_session_lock;
    wl_signal_add(&server->session_lock_manager->events.new_lock,
        &server->new_session_lock);
}
