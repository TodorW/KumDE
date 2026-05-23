#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s <command> [args]\n"
        "\n"
        "commands:\n"
        "  workspace <n>          switch focused output to workspace N (1-9)\n"
        "  move-to <n>            move focused window to workspace N\n"
        "  close                  close focused window\n"
        "  layout tile|float      set layout on focused output\n"
        "  quit                   quit kumde\n"
        "  reload                 reload config (same as SIGHUP)\n"
        "\n"
        "KUMDE_IPC must be set (exported automatically by kumde).\n",
        argv0);
}

static int connect_ipc(void)
{
    const char *path = getenv("KUMDE_IPC");
    if (!path) {
        fprintf(stderr, "kumde-msg: KUMDE_IPC not set\n");
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("kumde-msg: socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("kumde-msg: connect");
        close(fd);
        return -1;
    }

    return fd;
}

static int send_msg(int fd, const char *msg)
{
    size_t len = strlen(msg);
    ssize_t n = send(fd, msg, len, 0);
    if (n < 0) {
        perror("kumde-msg: send");
        return 1;
    }

    char buf[256];
    n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    char msg[256];
    const char *cmd = argv[1];

    if (strcmp(cmd, "workspace") == 0) {
        if (argc < 3) { usage(argv[0]); return 1; }
        int n = atoi(argv[2]) - 1;
        snprintf(msg, sizeof(msg),
            "{\"cmd\":\"workspace\",\"index\":%d}\n", n);
    } else if (strcmp(cmd, "move-to") == 0) {
        if (argc < 3) { usage(argv[0]); return 1; }
        int n = atoi(argv[2]) - 1;
        snprintf(msg, sizeof(msg),
            "{\"cmd\":\"move_to\",\"index\":%d}\n", n);
    } else if (strcmp(cmd, "close") == 0) {
        snprintf(msg, sizeof(msg), "{\"cmd\":\"close\"}\n");
    } else if (strcmp(cmd, "layout") == 0) {
        if (argc < 3) { usage(argv[0]); return 1; }
        snprintf(msg, sizeof(msg),
            "{\"cmd\":\"layout\",\"mode\":\"%s\"}\n", argv[2]);
    } else if (strcmp(cmd, "quit") == 0) {
        snprintf(msg, sizeof(msg), "{\"cmd\":\"quit\"}\n");
    } else if (strcmp(cmd, "reload") == 0) {
        snprintf(msg, sizeof(msg), "{\"cmd\":\"reload\"}\n");
    } else {
        fprintf(stderr, "kumde-msg: unknown command '%s'\n", cmd);
        usage(argv[0]);
        return 1;
    }

    int fd = connect_ipc();
    if (fd < 0)
        return 1;

    int ret = send_msg(fd, msg);
    close(fd);
    return ret;
}
