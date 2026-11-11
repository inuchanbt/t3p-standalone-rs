/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_temperature.h"
static const uint8_t calibration[6] = {0x70,0x6b,0x43,0x67,0x18,0xfc};
static unsigned reads, writes, fail, busy, bad_id, bad_calibration, skipped;
int t3p_temperature_read(struct t3p_meter_bus *bus, uint8_t reg, uint8_t *out, unsigned count)
{
    (void)bus; ++reads;
    if (fail) return -1;
    if (reg == 0xd0) { assert(count == 1); *out = bad_id ? 0x60 : 0x58; }
    else if (reg == 0xf3) { assert(count == 1); *out = busy ? 8 : 0; }
    else if (reg == 0x88) { assert(count == 6); memcpy(out, calibration, 6); if(bad_calibration) memset(out,255,6); }
    else { assert(reg == 0xfa && count == 3); out[0] = skipped ? 0x80 : 0x7e; out[1] = skipped ? 0 : 0xed; out[2] = 0; }
    return 0;
}
int t3p_temperature_write(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value)
{ (void)bus; assert(reg == 0xf4 && value == 0x21); ++writes; return 0; }

int main(void)
{
    int32_t temperature = 123;
    assert(!t3p_temperature_compensate(calibration, 519888, &temperature) && temperature == 2508);
    assert(!t3p_temperature_compensate(calibration, 400000, &temperature) && temperature < 0);
    assert(t3p_temperature_compensate(calibration, 0x80000, &temperature));
    assert(t3p_temperature_compensate(calibration, 0x100000, &temperature));
    uint8_t erased[6] = {255,255,255,255,255,255};
    assert(t3p_temperature_compensate(erased, 519888, &temperature));
    struct t3p_system_stats s = {0};
    struct t3p_temperature t = {0};
    struct t3p_meter_bus bus = {0};
    t3p_temperature_poll(&t, &bus, &s, 1000, 1);
    assert(t.pending && writes == 1 && !s.temp_valid);
    t3p_temperature_poll(&t, &bus, &s, 1010, 1);
    assert(reads == 3);
    t3p_temperature_poll(&t, &bus, &s, 1020, 1);
    assert(s.temp_valid && s.temp_centi == 2508);
    unsigned before = reads;
    t3p_temperature_poll(&t, &bus, &s, 1100, 0);
    assert(!s.temp_valid && reads == before); /* No sensor traffic during PD/cable scan. */
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        t = (struct t3p_temperature){0}; s.temp_valid = 1;
        fail = scenario == 0; bad_id = scenario == 1;
        bad_calibration = scenario == 2; busy = 0; skipped = scenario == 4;
        t3p_temperature_poll(&t, &bus, &s, 1000, 1);
        if (scenario >= 3) {
            busy = scenario == 3;
            t3p_temperature_poll(&t, &bus, &s, 1300, 1);
        }
        assert(!s.temp_valid);
    }
    fail = bad_id = bad_calibration = busy = skipped = 0;
    t = (struct t3p_temperature){0};
    t3p_temperature_poll(&t, &bus, &s, 1000, 1);
    t3p_temperature_poll(&t, &bus, &s, 1020, 1);
    assert(s.temp_valid);
    t3p_temperature_poll(&t, &bus, &s, 3600, 1);
    assert(!s.temp_valid); /* An old result must not survive indefinitely. */
    return 0;
}
