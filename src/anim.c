#include "kumde.h"
#include <math.h>
#include <string.h>

float kum_ease_out_cubic(float t)
{
    float f = 1.0f - t;
    return 1.0f - f * f * f;
}

float kum_ease_in_cubic(float t)
{
    return t * t * t;
}

float kum_ease_in_out_cubic(float t)
{
    if (t < 0.5f)
        return 4.0f * t * t * t;
    float f = 2.0f * t - 2.0f;
    return 0.5f * f * f * f + 1.0f;
}

float kum_ease_out_back(float t)
{
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float f = t - 1.0f;
    return 1.0f + c3 * f * f * f + c1 * f * f;
}

static float ms_elapsed(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double s = (double)(now.tv_sec  - start->tv_sec);
    double n = (double)(now.tv_nsec - start->tv_nsec);
    return (float)(s * 1000.0 + n / 1.0e6);
}

void kum_anim_start(kum_animation *anim, kum_anim_type type,
                    float from, float to, float duration_ms)
{
    anim->type        = type;
    anim->from        = from;
    anim->to          = to;
    anim->duration_ms = duration_ms;
    anim->current     = from;
    anim->done        = false;
    clock_gettime(CLOCK_MONOTONIC, &anim->start);
}

void kum_anim_tick(kum_animation *anim)
{
    if (anim->done || anim->type == ANIM_NONE)
        return;

    float t = ms_elapsed(&anim->start) / anim->duration_ms;
    if (t >= 1.0f) {
        t = 1.0f;
        anim->done = true;
    }

    float eased;
    switch (anim->type) {
    case ANIM_OPENING:  eased = kum_ease_out_back(t);    break;
    case ANIM_CLOSING:  eased = kum_ease_in_cubic(t);    break;
    case ANIM_FOCUSING: eased = kum_ease_out_cubic(t);   break;
    default:            eased = t;                        break;
    }

    anim->current = anim->from + (anim->to - anim->from) * eased;
}
