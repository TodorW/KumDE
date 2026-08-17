#ifndef KUMDE_CONFIG_H
#define KUMDE_CONFIG_H

#define KUM_VERSION             "0.8.0"

#define KUM_BORDER_WIDTH        3
#define KUM_BORDER_ACTIVE_R     0.45f
#define KUM_BORDER_ACTIVE_G     0.60f
#define KUM_BORDER_ACTIVE_B     0.95f
#define KUM_BORDER_INACTIVE_R   0.18f
#define KUM_BORDER_INACTIVE_G   0.18f
#define KUM_BORDER_INACTIVE_B   0.22f
#define KUM_BORDER_ALPHA        1.0f

#define KUM_ANIM_OPEN_MS        220.0f
#define KUM_ANIM_CLOSE_MS       160.0f
#define KUM_ANIM_FOCUS_MS       120.0f

#define KUM_CURSOR_SIZE         24
#define KUM_XDG_SHELL_VERSION   3
#define KUM_LAYER_SHELL_VERSION 4
#define KUM_COMPOSITOR_VERSION  5
#define KUM_LINUX_DMABUF_VERSION 4

#define KUM_RESIZE_MIN_W        120
#define KUM_RESIZE_MIN_H        80

#define KUM_MOD_KEY             WLR_MODIFIER_LOGO

#define KUM_GAP                 8
#define KUM_MASTER_RATIO        0.55f

#define KUM_CORNER_RADIUS       8
#define KUM_SHADOW_RADIUS       18
#define KUM_SHADOW_ALPHA        0.45f
#define KUM_SHADOW_OFFSET_X     0
#define KUM_SHADOW_OFFSET_Y     4

#define KUM_IPC_SOCKET_NAME     "kumde-ipc"

#define KUM_KB_LAYOUT           "us"
#define KUM_KB_VARIANT          ""
#define KUM_KB_MODEL            ""
#define KUM_KB_OPTIONS          ""
#define KUM_TAP_TO_CLICK        true
#define KUM_NATURAL_SCROLL      false
#define KUM_DISABLE_WHILE_TYPE  true
#define KUM_POINTER_ACCEL       0.0f

#endif
