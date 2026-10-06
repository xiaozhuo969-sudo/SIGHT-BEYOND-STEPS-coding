#pragma once
#include <stdbool.h>
#include <stdint.h>

/* GPIO14/21 are mask bits 2/3. Aircraft alarm overrides gesture direction. */
static inline uint8_t hand_indicator_tilt(uint8_t mask, int roll_direction, bool lit)
{
    if (!roll_direction) return mask;
    mask &= (uint8_t)~((1U<<2)|(1U<<3));
    if (lit) mask |= roll_direction<0 ? 1U<<2 : 1U<<3;
    return mask;
}

static inline bool hand_indicator_obstacle(unsigned mode, bool blink_lit)
{
    return mode==1 || (mode==2 && blink_lit);
}
