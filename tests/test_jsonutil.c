#include "jsonutil.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_passthrough(void)
{
    char buf[64];
    kum_json_escape(buf, sizeof(buf), "hello world");
    assert(strcmp(buf, "hello world") == 0);
}

static void test_empty(void)
{
    char buf[64] = "leftover";
    kum_json_escape(buf, sizeof(buf), "");
    assert(strcmp(buf, "") == 0);
}

static void test_quote_and_backslash(void)
{
    char buf[64];
    kum_json_escape(buf, sizeof(buf), "say \"hi\" \\ bye");
    assert(strcmp(buf, "say \\\"hi\\\" \\\\ bye") == 0);
}

static void test_control_chars(void)
{
    char buf[64];
    kum_json_escape(buf, sizeof(buf), "a\nb\rc\td");
    assert(strcmp(buf, "a\\nb\\rc\\td") == 0);

    /* Other control characters (e.g. bell) are dropped, not escaped,
     * since the wire protocol has no \u-escape support. */
    char buf2[64];
    char with_bell[] = { 'x', 0x07, 'y', '\0' };
    kum_json_escape(buf2, sizeof(buf2), with_bell);
    assert(strcmp(buf2, "xy") == 0);
}

static void test_truncates_safely_within_bounds(void)
{
    /* A title far longer than the destination buffer must not
     * overflow, and the output must still be a valid, terminated,
     * fully-escaped-up-to-the-cut string (this is exactly the class
     * of bug the original inline window_title snprintf had before it
     * grew this helper -- an unescaped quote breaking IPC framing). */
    char longsrc[100];
    for (int i = 0; i < 99; i++)
        longsrc[i] = (i % 5 == 0) ? '"' : 'x';
    longsrc[99] = '\0';

    char small[16];
    kum_json_escape(small, sizeof(small), longsrc);

    size_t len = strlen(small);
    assert(len < sizeof(small));

    /* No dangling escape at the very end: a truncation must not cut
     * between a backslash and the character it was escaping. */
    if (len > 0)
        assert(small[len - 1] != '\\');
}

static void test_zero_size_dst_is_a_safe_noop(void)
{
    char buf[4] = { 'A', 'B', 'C', '\0' };
    kum_json_escape(buf, 0, "whatever");
    /* Must not touch buf at all. */
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
}

int main(void)
{
    test_passthrough();
    test_empty();
    test_quote_and_backslash();
    test_control_chars();
    test_truncates_safely_within_bounds();
    test_zero_size_dst_is_a_safe_noop();
    printf("test_jsonutil: all tests passed\n");
    return 0;
}
