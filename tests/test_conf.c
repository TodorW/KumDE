#include "runtime_config.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int approx(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

/* Writes `content` to a temp file and runs kum_config_load() against
 * it. Caller must unlink the returned path when done. */
static void write_and_load(struct kum_runtime_config *cfg,
    const char *content, char *path_out, size_t path_out_size)
{
    snprintf(path_out, path_out_size, "/tmp/kumde_test_conf_%d.conf", getpid());
    FILE *f = fopen(path_out, "w");
    assert(f);
    fputs(content, f);
    fclose(f);

    kum_config_defaults(cfg);
    bool ok = kum_config_load(cfg, path_out);
    assert(ok);
}

static void test_defaults_are_sane(void)
{
    struct kum_runtime_config cfg;
    kum_config_defaults(&cfg);

    assert(strcmp(cfg.terminal, "foot") == 0);
    assert(cfg.animations == true);
    assert(cfg.xwayland == false);
    assert(cfg.gap >= 0);
    assert(cfg.master_ratio > 0.0f && cfg.master_ratio < 1.0f);
    assert(cfg.autostart_count == 0);
    assert(cfg.output_config_count == 0);
}

static void test_missing_file_fails_cleanly(void)
{
    struct kum_runtime_config cfg;
    kum_config_defaults(&cfg);
    bool ok = kum_config_load(&cfg, "/nonexistent/path/kumde.conf");
    assert(!ok);
    /* Must not have clobbered the defaults already in cfg. */
    assert(strcmp(cfg.terminal, "foot") == 0);
}

static void test_global_keys_parsed(void)
{
    struct kum_runtime_config cfg;
    char path[128];
    write_and_load(&cfg,
        "terminal = alacritty\n"
        "gap = 12\n"
        "master_ratio = 0.6\n"
        "animations = false\n"
        "border_active = 0.1 0.2 0.3\n",
        path, sizeof(path));

    assert(strcmp(cfg.terminal, "alacritty") == 0);
    assert(cfg.gap == 12);
    assert(approx(cfg.master_ratio, 0.6f));
    assert(cfg.animations == false);
    assert(approx(cfg.border_active[0], 0.1f));
    assert(approx(cfg.border_active[1], 0.2f));
    assert(approx(cfg.border_active[2], 0.3f));

    unlink(path);
}

static void test_multiple_autostart_lines_accumulate(void)
{
    struct kum_runtime_config cfg;
    char path[128];
    write_and_load(&cfg,
        "autostart = kumbar\n"
        "autostart = kumclip\n"
        "autostart = kumnotify\n",
        path, sizeof(path));

    assert(cfg.autostart_count == 3);
    assert(strcmp(cfg.autostart[0], "kumbar") == 0);
    assert(strcmp(cfg.autostart[1], "kumclip") == 0);
    assert(strcmp(cfg.autostart[2], "kumnotify") == 0);

    unlink(path);
}

static void test_input_section_scoped_correctly(void)
{
    struct kum_runtime_config cfg;
    char path[128];
    write_and_load(&cfg,
        "gap = 5\n"
        "[input]\n"
        "tap_to_click = false\n"
        "natural_scroll = true\n"
        "kb_layout = us,rs\n",
        path, sizeof(path));

    assert(cfg.gap == 5);
    assert(cfg.tap_to_click == false);
    assert(cfg.natural_scroll == true);
    assert(strcmp(cfg.kb_layout, "us,rs") == 0);

    unlink(path);
}

static void test_output_section_scoped_correctly(void)
{
    struct kum_runtime_config cfg;
    char path[128];
    write_and_load(&cfg,
        "[output DP-1]\n"
        "mode = 1920x1080@60\n"
        "pos = 0,0\n"
        "scale = 1.5\n"
        "enabled = true\n",
        path, sizeof(path));

    assert(cfg.output_config_count == 1);
    assert(strcmp(cfg.output_configs[0].name, "DP-1") == 0);
    assert(cfg.output_configs[0].width == 1920);
    assert(cfg.output_configs[0].height == 1080);
    assert(cfg.output_configs[0].refresh == 60);
    assert(approx(cfg.output_configs[0].scale, 1.5f));
    assert(cfg.output_configs[0].enabled == true);

    unlink(path);
}

static void test_rules_section_is_not_parsed_as_global(void)
{
    /* [rules] lines aren't kum_runtime_config keys at all -- they go
     * through kum_rules_load() separately. Nothing under [rules]
     * should be mistaken for a global/input/output key. */
    struct kum_runtime_config cfg;
    char path[128];
    write_and_load(&cfg,
        "[rules]\n"
        "rule = app_id:firefox, workspace:2\n"
        "gap = 99\n" /* still inside [rules] -- must NOT set cfg.gap */,
        path, sizeof(path));

    assert(cfg.gap != 99);

    unlink(path);
}

static void test_lines_after_a_section_stay_scoped_to_it(void)
{
    /* Regression test for the exact bug found in the shipped example
     * kumde.conf: a key placed after [output NAME] with no following
     * section header to close it gets silently routed into
     * apply_output() (which ignores unknown keys) instead of
     * apply_global() -- so 'autostart' after [output ...] never ran
     * kumbar/kumclip/etc. This asserts that behavior explicitly, so a
     * future contributor can't reintroduce the footgun without a
     * failing test telling them why the section-scoping matters. */
    struct kum_runtime_config cfg;
    char path[128];
    write_and_load(&cfg,
        "[output DP-1]\n"
        "mode = 1920x1080\n"
        "autostart = kumbar\n", /* misplaced: still inside [output DP-1] */
        path, sizeof(path));

    assert(cfg.autostart_count == 0);
    assert(cfg.output_config_count == 1);

    unlink(path);
}

static void test_output_config_count_capped_at_eight(void)
{
    struct kum_runtime_config cfg;
    char content[2048] = {0};
    for (int i = 0; i < 10; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[output O%d]\nmode = 800x600\n", i);
        strcat(content, buf);
    }

    char path[128];
    write_and_load(&cfg, content, path, sizeof(path));

    assert(cfg.output_config_count == 8);

    unlink(path);
}

static void test_comments_and_blank_lines_ignored(void)
{
    struct kum_runtime_config cfg;
    char path[128];
    write_and_load(&cfg,
        "# this is a comment\n"
        "\n"
        "   \n"
        "gap = 7\n"
        "# gap = 999\n",
        path, sizeof(path));

    assert(cfg.gap == 7);

    unlink(path);
}

int main(void)
{
    test_defaults_are_sane();
    test_missing_file_fails_cleanly();
    test_global_keys_parsed();
    test_multiple_autostart_lines_accumulate();
    test_input_section_scoped_correctly();
    test_output_section_scoped_correctly();
    test_rules_section_is_not_parsed_as_global();
    test_lines_after_a_section_stay_scoped_to_it();
    test_output_config_count_capped_at_eight();
    test_comments_and_blank_lines_ignored();
    printf("test_conf: all tests passed\n");
    return 0;
}
