/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_TEMPERATURE_H
#define T3P_TEMPERATURE_H
#include "t3p_meter.h"
struct t3p_temperature {
    uint32_t attempted, started, updated;
    unsigned pending;
    uint8_t calibration[6];
};
void t3p_temperature_poll(struct t3p_temperature *, struct t3p_meter_bus *,
                          struct t3p_system_stats *, uint32_t now, unsigned enabled);
int t3p_temperature_compensate(const uint8_t calibration[6], uint32_t adc, int32_t *centi);
void t3p_temperature_bus_init(struct t3p_bsp *, struct t3p_meter_bus *);
int t3p_temperature_read(struct t3p_meter_bus *, uint8_t, uint8_t *, unsigned);
int t3p_temperature_write(struct t3p_meter_bus *, uint8_t, uint8_t);
#endif
