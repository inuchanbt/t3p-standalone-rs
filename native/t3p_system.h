/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_SYSTEM_H
#define T3P_SYSTEM_H
#include <stdint.h>

/* Word-for-word ABI with src/system.rs. Owned counters saturate; uptime carries.
 * dbi_ops is the BSP's existing wrapping 32-bit operation counter. */
struct t3p_system_stats {
    uint32_t uptime_low, uptime_high;
    uint32_t reads, read_errors, writes, write_errors;
    uint32_t meter_id, meter_config, meter_flags, meter_state;
    uint32_t samples, sample_errors;
    uint32_t frames, frame_ms, frame_max_ms, dbi_ops;
    uint32_t short_presses, double_presses, long_presses, key_event, key_ms;
    int32_t temp_centi;
    uint32_t temp_valid;
};

static inline void t3p_system_count(uint32_t *value)
{
    if (*value != UINT32_MAX) ++*value;
}

static inline void t3p_system_tick(struct t3p_system_stats *s, uint32_t *last, uint32_t now)
{
    uint32_t previous = s->uptime_low;
    s->uptime_low += now - *last;
    if (s->uptime_low < previous) ++s->uptime_high;
    *last = now;
}

void meter_system_status(const struct t3p_system_stats *);
uint32_t meter_on_system_page(void);
#endif
