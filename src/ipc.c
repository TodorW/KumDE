#include "kumde.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define IPC_BUF 512

static void ipc_reply(struct kum_ipc_client *client, const char *msg)
{
    send(client->fd, msg, strlen(msg), MSG_NOSIGNAL | MSG_DONTWAIT);
}

static void ipc_handle_command(struct kum_ipc_client *client, const char *msg)
{
    struct kum_server *server = client->server;

    char cmd[64] = {0};
    sscanf(msg, "{\"cmd\":\"%63[^\"]", cmd);

    if (strcmp(cmd, "workspace") == 0) {
        int index = -1;
        sscanf(msg, "{\"cmd\":\"workspace\",\"index\":%d}", &index);
        if (index >= 0 && index < KUM_WORKSPACE_COUNT) {
            struct kum_output *output = kum_output_focused(server);
            kum_workspace_switch(server, output, index);
            ipc_reply(client, "{\"ok\":true}\n");
        } else {
            ipc_reply(client, "{\"ok\":false,\"error\":\"bad index\"}\n");
        }
    } else if (strcmp(cmd, "move_to") == 0) {
        int index = -1;
        sscanf(msg, "{\"cmd\":\"move_to\",\"index\":%d}", &index);
        if (index < 0 || index >= KUM_WORKSPACE_COUNT) {
            ipc_reply(client, "{\"ok\":false,\"error\":\"bad index or no focus\"}\n");
        } else if (server->focused) {
            kum_workspace_move_toplevel(server, server->focused, index);
            ipc_reply(client, "{\"ok\":true}\n");
#ifdef KUM_XWAYLAND
        } else if (server->focused_xwayland) {
            kum_workspace_move_xwayland_surface(server, server->focused_xwayland, index);
            ipc_reply(client, "{\"ok\":true}\n");
#endif
        } else {
            ipc_reply(client, "{\"ok\":false,\"error\":\"bad index or no focus\"}\n");
        }
    } else if (strcmp(cmd, "close") == 0) {
        if (server->focused) {
            wlr_xdg_toplevel_send_close(server->focused->xdg_toplevel);
            ipc_reply(client, "{\"ok\":true}\n");
#ifdef KUM_XWAYLAND
        } else if (server->focused_xwayland) {
            wlr_xwayland_surface_close(server->focused_xwayland->xwayland_surface);
            ipc_reply(client, "{\"ok\":true}\n");
#endif
        } else {
            ipc_reply(client, "{\"ok\":false,\"error\":\"no focused window\"}\n");
        }
    } else if (strcmp(cmd, "layout") == 0) {
        char mode[16] = {0};
        sscanf(msg, "{\"cmd\":\"layout\",\"mode\":\"%15[^\"]", mode);
        struct kum_output *output = kum_output_focused(server);
        if (output) {
            int idx = output->active_workspace;
            if (strcmp(mode, "tile") == 0) {
                server->workspaces[idx].layout = LAYOUT_TILE;
                kum_workspace_arrange(server, output, idx);
            } else if (strcmp(mode, "monocle") == 0) {
                server->workspaces[idx].layout = LAYOUT_MONOCLE;
                kum_workspace_arrange(server, output, idx);
            } else {
                server->workspaces[idx].layout = LAYOUT_FLOATING;
            }
            ipc_reply(client, "{\"ok\":true}\n");
        } else {
            ipc_reply(client, "{\"ok\":false,\"error\":\"no output\"}\n");
        }
    } else if (strcmp(cmd, "launch") == 0) {
        char exec[256] = {0};
        sscanf(msg, "{\"cmd\":\"launch\",\"exec\":\"%255[^\"]", exec);
        if (exec[0]) {
            if (fork() == 0) {
                setsid();
                execl("/bin/sh", "sh", "-c", exec, NULL);
                _exit(1);
            }
            ipc_reply(client, "{\"ok\":true}\n");
        } else {
            ipc_reply(client, "{\"ok\":false,\"error\":\"missing exec\"}\n");
        }
    } else if (strcmp(cmd, "quit") == 0) {
        ipc_reply(client, "{\"ok\":true}\n");
        wl_display_terminate(server->display);
    } else if (strcmp(cmd, "reload") == 0) {
        kum_server_reload_config(server);
        ipc_reply(client, "{\"ok\":true}\n");
    } else {
        ipc_reply(client, "{\"ok\":false,\"error\":\"unknown command\"}\n");
    }
}

static void ipc_client_destroy(struct kum_ipc_client *client)
{
    wl_event_source_remove(client->source);
    close(client->fd);
    wl_list_remove(&client->link);
    free(client);
}

static int handle_client_readable(int fd, uint32_t mask, void *data)
{
    struct kum_ipc_client *client = data;

    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        ipc_client_destroy(client);
        return 0;
    }

    char buf[IPC_BUF];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
    if (n <= 0) {
        ipc_client_destroy(client);
        return 0;
    }

    buf[n] = '\0';

    char *line = buf;
    char *nl;
    while ((nl = strchr(line, '\n')) != NULL) {
        *nl = '\0';
        if (*line)
            ipc_handle_command(client, line);
        line = nl + 1;
    }
    if (*line)
        ipc_handle_command(client, line);

    return 0;
}

static int handle_new_connection(int fd, uint32_t mask, void *data)
{
    struct kum_server *server = data;

    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR))
        return 0;

    int cfd = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (cfd < 0)
        return 0;

    struct kum_ipc_client *client = calloc(1, sizeof(*client));
    client->server = server;
    client->fd     = cfd;

    struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
    client->source = wl_event_loop_add_fd(loop, cfd,
        WL_EVENT_READABLE, handle_client_readable, client);

    wl_list_insert(&server->ipc_clients, &client->link);
    return 0;
}

static const char *ipc_socket_dir(void)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");
    return dir ? dir : "/tmp";
}

void kum_ipc_init(struct kum_server *server)
{
    wl_list_init(&server->ipc_clients);

    snprintf(server->ipc.path, sizeof(server->ipc.path),
        "%s/%s", ipc_socket_dir(), KUM_IPC_SOCKET_NAME);

    unlink(server->ipc.path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        wlr_log(WLR_ERROR, "ipc: socket() failed: %s", strerror(errno));
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, server->ipc.path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        wlr_log(WLR_ERROR, "ipc: bind() failed: %s", strerror(errno));
        close(fd);
        return;
    }

    if (listen(fd, 8) < 0) {
        wlr_log(WLR_ERROR, "ipc: listen() failed: %s", strerror(errno));
        close(fd);
        return;
    }

    server->ipc.fd = fd;
    setenv("KUMDE_IPC", server->ipc.path, 1);

    struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
    server->ipc.source = wl_event_loop_add_fd(loop, fd,
        WL_EVENT_READABLE, handle_new_connection, server);

    wlr_log(WLR_INFO, "ipc: %s", server->ipc.path);
}

void kum_ipc_broadcast(struct kum_server *server, const char *msg, int len)
{
    struct kum_ipc_client *client, *tmp;
    wl_list_for_each_safe(client, tmp, &server->ipc_clients, link) {
        ssize_t n = send(client->fd, msg, len, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0 && errno != EAGAIN)
            ipc_client_destroy(client);
    }
}

void kum_ipc_finish(struct kum_server *server)
{
    struct kum_ipc_client *client, *tmp;
    wl_list_for_each_safe(client, tmp, &server->ipc_clients, link)
        ipc_client_destroy(client);

    if (server->ipc.source)
        wl_event_source_remove(server->ipc.source);

    if (server->ipc.fd > 0)
        close(server->ipc.fd);

    unlink(server->ipc.path);
}

void kum_ipc_broadcast_window_title(struct kum_server *server,
    int workspace, const char *title)
{
    struct kum_output *o;
    wl_list_for_each(o, &server->outputs, link) {
        if (o->active_workspace != workspace)
            continue;

        char title_esc[350];
        kum_json_escape(title_esc, sizeof(title_esc), title);

        char msg[400];
        int n = snprintf(msg, sizeof(msg),
            "{\"event\":\"window_title\",\"output\":\"%s\",\"title\":\"%s\"}\n",
            o->wlr_output->name, title_esc);
        kum_ipc_broadcast(server, msg, n);
        break;
    }
}

void kum_ipc_broadcast_occupancy(struct kum_server *server)
{
    bool occupied[KUM_WORKSPACE_COUNT] = {0};
    struct kum_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link) {
        if (tl->workspace >= 0 && tl->workspace < KUM_WORKSPACE_COUNT)
            occupied[tl->workspace] = true;
    }
#ifdef KUM_XWAYLAND
    struct kum_xwayland_surface *xs;
    wl_list_for_each(xs, &server->xwayland_surfaces, link) {
        if (xs->workspace >= 0 && xs->workspace < KUM_WORKSPACE_COUNT)
            occupied[xs->workspace] = true;
    }
#endif

    char msg[256];
    int  pos = 0;
    pos += snprintf(msg + pos, sizeof(msg) - pos,
        "{\"event\":\"occupancy\",\"ws\":[");
    for (int i = 0; i < KUM_WORKSPACE_COUNT; i++) {
        pos += snprintf(msg + pos, sizeof(msg) - pos,
            "%s%d", i == 0 ? "" : ",", occupied[i] ? 1 : 0);
    }
    pos += snprintf(msg + pos, sizeof(msg) - pos, "]}\n");

    kum_ipc_broadcast(server, msg, pos);
}
