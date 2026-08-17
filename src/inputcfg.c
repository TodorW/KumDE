#include "kumde.h"
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <libinput.h>

static void configure_libinput_device(struct kum_server *server,
    struct libinput_device *dev)
{
    struct kum_runtime_config *cfg = &server->cfg;

    if (libinput_device_config_tap_get_finger_count(dev) > 0) {
        libinput_device_config_tap_set_enabled(dev,
            cfg->tap_to_click
                ? LIBINPUT_CONFIG_TAP_ENABLED
                : LIBINPUT_CONFIG_TAP_DISABLED);
    }

    if (libinput_device_config_scroll_has_natural_scroll(dev)) {
        libinput_device_config_scroll_set_natural_scroll_enabled(dev,
            cfg->natural_scroll);
    }

    if (libinput_device_config_dwt_is_available(dev)) {
        libinput_device_config_dwt_set_enabled(dev,
            cfg->disable_while_typing
                ? LIBINPUT_CONFIG_DWT_ENABLED
                : LIBINPUT_CONFIG_DWT_DISABLED);
    }

    if (libinput_device_config_accel_is_available(dev)) {
        libinput_device_config_accel_set_speed(dev, cfg->pointer_accel);
        libinput_device_config_accel_set_profile(dev,
            LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
    }

    if (libinput_device_config_send_events_get_modes(dev) &
            LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE) {
        libinput_device_config_send_events_set_mode(dev,
            LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
    }
}

void kum_input_configure_device(struct kum_server *server,
    struct wlr_input_device *device)
{
    if (device->type != WLR_INPUT_DEVICE_POINTER)
        return;
    if (!wlr_input_device_is_libinput(device))
        return;

    struct libinput_device *li = wlr_libinput_get_device_handle(device);
    if (li)
        configure_libinput_device(server, li);
}

void kum_input_configure(struct kum_server *server)
{
    struct kum_pointer *p;
    wl_list_for_each(p, &server->pointers, link)
        kum_input_configure_device(server, p->device);
}
