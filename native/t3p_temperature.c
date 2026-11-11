/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_temperature.h"

/* BMP280 DS001-26 fixed-point compensation, with widened intermediates.
 * This is the pressure sensor's die temperature, not calibrated ambient. */
int t3p_temperature_compensate(const uint8_t c[6], uint32_t adc, int32_t *centi)
{
    unsigned t1 = (unsigned)c[0] | (unsigned)c[1] << 8;
    int16_t t2 = (int16_t)((unsigned)c[2] | (unsigned)c[3] << 8);
    int16_t t3 = (int16_t)((unsigned)c[4] | (unsigned)c[5] << 8);
    if (!centi || !t1 || t1 == 0xffff || adc > 0xfffff || adc == 0x80000) return -1;
    int64_t x = (int64_t)(adc >> 3) - 2 * (int64_t)t1;
    int64_t v1 = (x * t2) >> 11;
    x = (int64_t)(adc >> 4) - t1;
    int64_t v2 = (((x * x) >> 12) * t3) >> 14;
    int64_t temperature = ((v1 + v2) * 5 + 128) >> 8;
    if (temperature < -4000 || temperature > 8500) return -1;
    *centi = (int32_t)temperature;
    return 0;
}

void t3p_temperature_poll(struct t3p_temperature *t, struct t3p_meter_bus *bus,
                          struct t3p_system_stats *s, uint32_t now, unsigned enabled)
{
    if (!enabled) { s->temp_valid = 0; t->pending = 0; return; }
    if (now - t->updated > 2500) s->temp_valid = 0;
    if (!t->pending) {
        if (now - t->attempted < 1000) return;
        t->attempted = now;
        uint8_t id, status;
        if (t3p_temperature_read(bus, 0xd0, &id, 1) || id != 0x58 ||
            t3p_temperature_read(bus, 0xf3, &status, 1) || (status & 9u) ||
            t3p_temperature_read(bus, 0x88, t->calibration, 6)) goto failed;
        unsigned t1 = (unsigned)t->calibration[0] | (unsigned)t->calibration[1] << 8;
        if (!t1 || t1 == 0xffff) goto failed;
        /* Temperature x1, pressure skipped, forced conversion. No reset. */
        if (t3p_temperature_write(bus, 0xf4, 0x21)) goto failed;
        t->started = now;
        t->pending = 1;
        return;
    }
    if (now - t->started < 20) return;
    uint8_t status, raw[3];
    if (t3p_temperature_read(bus, 0xf3, &status, 1) || (status & 1u)) goto failed;
    if (status & 8u) {
        if (now - t->started <= 200) return;
        goto failed;
    }
    if (t3p_temperature_read(bus, 0xfa, raw, 3)) goto failed;
    uint32_t adc = (uint32_t)raw[0] << 12 | (uint32_t)raw[1] << 4 | raw[2] >> 4;
    if (t3p_temperature_compensate(t->calibration, adc, &s->temp_centi)) goto failed;
    s->temp_valid = 1;
    t->updated = now;
    t->pending = 0;
    return;
failed:
    s->temp_valid = 0;
    t->pending = 0;
}
