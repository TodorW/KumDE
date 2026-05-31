#include "kumde.h"
#include <stdlib.h>
#include <unistd.h>

void kum_autostart_run(struct kum_server *server)
{
    for (int i = 0; i < server->cfg.autostart_count; i++) {
        const char *cmd = server->cfg.autostart[i];
        if (!cmd[0]) continue;
        if (fork() == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", cmd, NULL);
            _exit(1);
        }
    }
}
