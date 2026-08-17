#include "easing.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

static int approx(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

static void test_easing_boundaries(void)
{
    /* Every easing curve used by kum_anim_tick() must map t=0 -> 0
     * and t=1 -> 1, or animations would visibly jump at either end. */
    assert(approx(kum_ease_out_cubic(0.0f), 0.0f));
    assert(approx(kum_ease_out_cubic(1.0f), 1.0f));

    assert(approx(kum_ease_in_cubic(0.0f), 0.0f));
    assert(approx(kum_ease_in_cubic(1.0f), 1.0f));

    assert(approx(kum_ease_in_out_cubic(0.0f), 0.0f));
    assert(approx(kum_ease_in_out_cubic(0.5f), 0.5f));
    assert(approx(kum_ease_in_out_cubic(1.0f), 1.0f));

    assert(approx(kum_ease_out_back(0.0f), 0.0f));
    assert(approx(kum_ease_out_back(1.0f), 1.0f));
}

static void test_anim_start_sets_initial_state(void)
{
    kum_animation anim;
    kum_anim_start(&anim, ANIM_OPENING, 0.0f, 1.0f, 200.0f);

    assert(anim.type == ANIM_OPENING);
    assert(anim.from == 0.0f);
    assert(anim.to == 1.0f);
    assert(anim.duration_ms == 200.0f);
    assert(anim.current == 0.0f);
    assert(!anim.done);
}

static void test_anim_tick_progresses_and_completes(void)
{
    kum_animation anim;
    /* A near-zero duration means the very first tick should already
     * observe elapsed/duration >= 1.0 and mark the animation done. */
    kum_anim_start(&anim, ANIM_FOCUSING, 0.0f, 1.0f, 0.001f);

    struct timespec ts = {0, 2000000}; /* 2ms */
    nanosleep(&ts, NULL);

    kum_anim_tick(&anim);
    assert(anim.done);
    assert(approx(anim.current, 1.0f));
}

static void test_anim_tick_noop_on_none_or_done(void)
{
    kum_animation anim;
    kum_anim_start(&anim, ANIM_NONE, 0.0f, 1.0f, 100.0f);
    kum_anim_tick(&anim);
    assert(!anim.done);
    assert(anim.current == 0.0f);

    kum_anim_start(&anim, ANIM_OPENING, 0.0f, 1.0f, 100.0f);
    anim.done = true;
    anim.current = 0.42f;
    kum_anim_tick(&anim);
    assert(approx(anim.current, 0.42f));
}

int main(void)
{
    test_easing_boundaries();
    test_anim_start_sets_initial_state();
    test_anim_tick_progresses_and_completes();
    test_anim_tick_noop_on_none_or_done();
    printf("test_easing: all tests passed\n");
    return 0;
}
