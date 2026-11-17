/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_TEMPERATURE_H
#define T3P_TEMPERATURE_H
#include "t3p_meter.h"
struct t3p_temperature {
    uint32_t attempted, started, updated;
    unsigned pending;
    uint8_t calibration[6];
#ifdef T3P_TEMPERATURE_ENS220
    uint32_t cleanup_attempted;
    unsigned needs_idle, reset_done;
#endif
};
void t3p_temperature_poll(struct t3p_temperature *, struct t3p_meter_bus *,
                          struct t3p_system_stats *, uint32_t now, unsigned enabled);
int t3p_temperature_compensate(const uint8_t calibration[6], uint32_t adc, int32_t *centi);
void t3p_temperature_bus_init(struct t3p_bsp *, struct t3p_meter_bus *);
int t3p_temperature_read(struct t3p_meter_bus *, uint8_t, uint8_t *, unsigned);
int t3p_temperature_write(struct t3p_meter_bus *, uint8_t, uint8_t);
#ifdef T3P_TEMPERATURE_ENS220
int t3p_ens220_read(struct t3p_meter_bus *, uint8_t, uint8_t *, unsigned);
int t3p_ens220_write(struct t3p_meter_bus *, uint8_t, uint8_t);
int t3p_ens220_centidegrees(uint32_t, int32_t *);
void t3p_ens220_poll(struct t3p_temperature *, struct t3p_meter_bus *,
                     struct t3p_system_stats *, uint32_t, unsigned);
#endif
#endif
