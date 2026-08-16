#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void usage(const char *a)
{
    fprintf(stderr,
        "usage: %s <command>\n"
        "  workspace <n>       switch to workspace N (1-9)\n"
        "  move-to <n>         move focused window to workspace N\n"
        "  close               close focused window\n"
        "  layout tile|float|monocle\n"
        "  quit                quit kumde\n"
        "  reload              reload config\n"
        "  subscribe           stream events to stdout\n"
        "  notify <app> <summary> [body] [urgency 0-2] [timeout_ms]\n"
        "\nKUMDE_IPC must be set (exported by kumde).\n", a);
}

static int connect_ipc(void)
{
    const char *path = getenv("KUMDE_IPC");
    if (!path) { fprintf(stderr,"kumde-msg: KUMDE_IPC not set\n"); return -1; }
    int fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
    if (fd < 0) { perror("kumde-msg: socket"); return -1; }
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("kumde-msg: connect"); close(fd); return -1;
    }
    return fd;
}

static int send_recv(int fd, const char *msg)
{
    send(fd, msg, strlen(msg), 0);
    char buf[512];
    ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
    if (n > 0) { buf[n]='\0'; printf("%s", buf); }
    return 0;
}

static int do_subscribe(int fd)
{
    char msg[] = "{\"cmd\":\"subscribe\"}\n";
    send(fd, msg, strlen(msg), 0);

    struct pollfd pfd = { .fd=fd, .events=POLLIN };
    char buf[1024];
    while (1) {
        if (poll(&pfd, 1, -1) < 0) break;
        if (pfd.revents & (POLLHUP|POLLERR)) break;
        if (pfd.revents & POLLIN) {
            ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
            if (n <= 0) break;
            buf[n] = '\0';
            printf("%s", buf);
            fflush(stdout);
        }
    }
    return 0;
}

static void send_notify(const char *app_n, const char *sum,
    const char *body, int urg, int timeout_ms)
{
    char path_buf[256];
    const char *path = getenv("KUMNOTIFY_SOCK");
    if (!path) {
        const char *runtime = getenv("XDG_RUNTIME_DIR");
        if (!runtime) {
            fprintf(stderr,"kumde-msg: XDG_RUNTIME_DIR not set\n");
            return;
        }
        snprintf(path_buf, sizeof(path_buf), "%s/kumnotify.sock", runtime);
        path = path_buf;
    }
    int fd = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);
    char msg[900];
    snprintf(msg, sizeof(msg), "NOTIFY %s|%s|%s|%d|%d",
        app_n?app_n:"kumde-msg", sum?sum:"", body?body:"", urg, timeout_ms);
    sendto(fd, msg, strlen(msg), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    const char *cmd = argv[1];

    if (!strcmp(cmd,"notify")) {
        if (argc < 4) { usage(argv[0]); return 1; }
        int urg = argc > 5 ? atoi(argv[5]) : 1;
        int tms = argc > 6 ? atoi(argv[6]) : -1;
        send_notify(argv[2], argv[3], argc>4?argv[4]:"", urg, tms);
        return 0;
    }

    char msg[256];
    if (!strcmp(cmd,"workspace") && argc>=3) {
        snprintf(msg,sizeof(msg),"{\"cmd\":\"workspace\",\"index\":%d}\n",atoi(argv[2])-1);
    } else if (!strcmp(cmd,"move-to") && argc>=3) {
        snprintf(msg,sizeof(msg),"{\"cmd\":\"move_to\",\"index\":%d}\n",atoi(argv[2])-1);
    } else if (!strcmp(cmd,"close")) {
        snprintf(msg,sizeof(msg),"{\"cmd\":\"close\"}\n");
    } else if (!strcmp(cmd,"layout") && argc>=3) {
        snprintf(msg,sizeof(msg),"{\"cmd\":\"layout\",\"mode\":\"%s\"}\n",argv[2]);
    } else if (!strcmp(cmd,"quit")) {
        snprintf(msg,sizeof(msg),"{\"cmd\":\"quit\"}\n");
    } else if (!strcmp(cmd,"reload")) {
        snprintf(msg,sizeof(msg),"{\"cmd\":\"reload\"}\n");
    } else if (!strcmp(cmd,"subscribe")) {
        int fd = connect_ipc();
        if (fd < 0) return 1;
        int r = do_subscribe(fd);
        close(fd); return r;
    } else {
        fprintf(stderr,"kumde-msg: unknown command '%s'\n",cmd);
        usage(argv[0]); return 1;
    }

    int fd = connect_ipc();
    if (fd < 0) return 1;
    int r = send_recv(fd, msg);
    close(fd); return r;
}
