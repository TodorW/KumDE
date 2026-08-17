#ifndef KUM_EASING_H
#define KUM_EASING_H

#include <stdbool.h>
#include <time.h>

/* Pure animation-timing logic: no wlroots dependency, safe to unit test
 * directly (see tests/test_easing.c). */

typedef enum {
    ANIM_NONE = 0,
    ANIM_OPENING,
    ANIM_CLOSING,
    ANIM_FOCUSING,
} kum_anim_type;

typedef struct {
    kum_anim_type   type;
    struct timespec start;
    float           duration_ms;
    float           from;
    float           to;
    float           current;
    bool            done;
} kum_animation;

float kum_ease_out_cubic(float t);
float kum_ease_in_cubic(float t);
float kum_ease_in_out_cubic(float t);
float kum_ease_out_back(float t);

void kum_anim_start(kum_animation *anim, kum_anim_type type,
                    float from, float to, float duration_ms);
void kum_anim_tick(kum_animation *anim);

#endif
