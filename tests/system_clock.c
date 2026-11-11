/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stddef.h>
#include "../native/t3p_system.h"

int main(void)
{
    _Static_assert(sizeof(struct t3p_system_stats) == 23 * 4, "Rust ABI size");
    _Static_assert(offsetof(struct t3p_system_stats, key_ms) == 20 * 4, "Rust ABI order");
    struct t3p_system_stats s = { .uptime_low = UINT32_MAX - 10 };
    uint32_t last = UINT32_MAX - 10;
    t3p_system_tick(&s, &last, 5);
    assert(s.uptime_low == 5 && s.uptime_high == 1 && last == 5);
    t3p_system_tick(&s, &last, 10);
    assert(s.uptime_low == 10 && s.uptime_high == 1);
    t3p_system_count(&s.frames);
    assert(s.frames == 1);
    s.frames = UINT32_MAX - 1;
    t3p_system_count(&s.frames);
    t3p_system_count(&s.frames);
    assert(s.frames == UINT32_MAX);
    return 0;
}
