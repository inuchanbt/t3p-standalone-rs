/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_CAPTURE_PROBE_H
#define T3P_CAPTURE_PROBE_H
#include "t3p_bsp.h"
#include "t3p_boot.h"
/* 36 raw words; word4 flags: root valid bit0, auxiliary valid bit1, header bit2.
 * No physical Flash address is inferred from these opaque values. */
void t3p_capture_probe_read(struct t3p_bsp *, const struct t3p_boot_snapshot *,
                            const uint32_t parameters[4], uint32_t out[36]);
void meter_capture_probe_render(const uint32_t words[36], uint32_t section,
                               uint32_t top, uint16_t pixels[1920]);
#endif
