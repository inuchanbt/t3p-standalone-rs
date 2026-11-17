/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_temperature.h"
static const uint8_t calibration[6] = {0x70,0x6b,0x43,0x67,0x18,0xfc};
static unsigned reads, writes, fail, busy, bad_id, bad_calibration, skipped;
static unsigned fail_reg, fail_write;
int t3p_temperature_read(struct t3p_meter_bus *bus, uint8_t reg, uint8_t *out, unsigned count)
{
    (void)bus; ++reads;
    if (fail || reg==fail_reg) return -1;
    if (reg == 0xd0) { assert(count == 1); *out = bad_id ? 0x60 : 0x58; }
    else if (reg == 0xf3) { assert(count == 1); *out = busy ? 8 : 0; }
    else if (reg == 0x88) { assert(count == 6); memcpy(out, calibration, 6); if(bad_calibration) memset(out,255,6); }
    else { assert(reg == 0xfa && count == 3); out[0] = skipped ? 0x80 : 0x7e; out[1] = skipped ? 0 : 0xed; out[2] = 0; }
    return 0;
}
int t3p_temperature_write(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value)
{ (void)bus; assert(reg == 0xf4 && value == 0x21); ++writes; return fail_write ? -1 : 0; }

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
#ifdef T3P_TEMPERATURE_DIAGNOSTICS
    struct t3p_temperature_diag d={0};bus.temperature_diag=&d;
#endif
    t3p_temperature_poll(&t, &bus, &s, 1000, 1);
    assert(t.pending && writes == 1 && !s.temp_valid);
    t3p_temperature_poll(&t, &bus, &s, 1010, 1);
    assert(reads == 3);
    t3p_temperature_poll(&t, &bus, &s, 1020, 1);
    assert(s.temp_valid && s.temp_centi == 2508);
#ifdef T3P_TEMPERATURE_DIAGNOSTICS
    assert(d.phase==TEMP_DONE&&d.successes==1&&!d.failures&&d.valid==31&&d.id==0x58);
    assert(d.adc==519888&&d.temp_valid&&d.temp_centi==2508);
#endif
    unsigned before = reads;
    t3p_temperature_poll(&t, &bus, &s, 1100, 0);
    assert(!s.temp_valid && reads == before); /* No sensor traffic during PD/cable scan. */
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        t = (struct t3p_temperature){0}; s.temp_valid = 1;
#ifdef T3P_TEMPERATURE_DIAGNOSTICS
        d=(struct t3p_temperature_diag){0};
#endif
        fail = scenario == 0; bad_id = scenario == 1;
        bad_calibration = scenario == 2; busy = 0; skipped = scenario == 4;
        t3p_temperature_poll(&t, &bus, &s, 1000, 1);
        if (scenario >= 3) {
            busy = scenario == 3;
            t3p_temperature_poll(&t, &bus, &s, 1300, 1);
        }
        assert(!s.temp_valid);
#ifdef T3P_TEMPERATURE_DIAGNOSTICS
        unsigned phases[]={TEMP_ID,TEMP_ID,TEMP_CALIBRATION,TEMP_POST_STATUS,TEMP_COMPENSATE};
        unsigned results[]={TEMP_IO,TEMP_BAD_ID,TEMP_BAD_CALIBRATION,TEMP_TIMEOUT,TEMP_SKIPPED};
        assert(d.phase==phases[scenario]&&d.result==results[scenario]&&d.failures==1&&!d.temp_valid);
        if(!scenario)assert(!d.valid); /* Failed transfers must not invent an ID. */
#endif
    }
    fail = bad_id = bad_calibration = busy = skipped = 0;
    t = (struct t3p_temperature){0};
    t3p_temperature_poll(&t, &bus, &s, 1000, 1);
    t3p_temperature_poll(&t, &bus, &s, 1020, 1);
    assert(s.temp_valid);
    t3p_temperature_poll(&t, &bus, &s, 3600, 1);
    assert(!s.temp_valid); /* An old result must not survive indefinitely. */
#ifdef T3P_TEMPERATURE_DIAGNOSTICS
    assert(!d.temp_valid);
    for(unsigned scenario=0;scenario<6;scenario++) {
        unsigned regs[]={0xd0,0xf3,0x88,0,0xf3,0xfa};
        unsigned phases[]={TEMP_ID,TEMP_PRE_STATUS,TEMP_CALIBRATION,TEMP_TRIGGER,TEMP_POST_STATUS,TEMP_RAW};
        t=(struct t3p_temperature){0};d=(struct t3p_temperature_diag){0};
        fail_reg=scenario<3?regs[scenario]:0;fail_write=scenario==3;
        t3p_temperature_poll(&t,&bus,&s,1000,1);
        if(scenario>=4){fail_reg=regs[scenario];t3p_temperature_poll(&t,&bus,&s,1020,1);}
        assert(d.phase==phases[scenario]&&d.result==TEMP_IO&&d.failures==1&&!s.temp_valid);
    }
    fail_reg=fail_write=0;
    t=(struct t3p_temperature){0};d=(struct t3p_temperature_diag){0};
    t3p_temperature_poll(&t,&bus,&s,1000,1);
    t3p_temperature_poll(&t,&bus,&s,1010,0);
    assert(d.result==TEMP_CANCELLED&&!t.pending&&!s.temp_valid);
#endif
    return 0;
}
