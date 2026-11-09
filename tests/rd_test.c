/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "../native/t3p_rd_test.h"

struct fixture {
    uint16_t mv;
    uint8_t switches, failed_readback;
    int baseline, sample_error;
    unsigned writes, restore_failures;
};

int t3p_pd_rd_baseline(struct t3p_meter_bus *bus)
{
    return ((struct fixture *)bus->context)->baseline;
}

int t3p_pd_set_rd(struct t3p_meter_bus *bus, uint8_t switches)
{
    struct fixture *f = bus->context;
    ++f->writes;
    if (switches == T3P_RD_DEFAULT && f->restore_failures) {
        --f->restore_failures;
        return -1;
    }
    f->switches = switches;
    return switches == f->failed_readback ? -1 : 0;
}

int t3p_meter_identify(struct t3p_meter_bus *bus) { (void)bus; return 0; }

int t3p_meter_sample(struct t3p_meter_bus *bus, uint16_t *mv, int16_t *ma)
{
    struct fixture *f = bus->context;
    *mv = f->mv;
    *ma = 0;
    return f->sample_error;
}

int main(void)
{
    struct fixture f = { .mv = 5000, .switches = T3P_RD_DEFAULT };
    struct t3p_meter_bus bus = { .context = &f };
    struct t3p_rd_test rd = {0};

    t3p_rd_test_step(&rd, &bus, 0);
    assert(rd.state == T3P_RD_VBUS_PRESENT && f.writes == 0);
    f.mv = 3010;
    f.baseline = 1;
    t3p_rd_test_step(&rd, &bus, 100);
    assert(rd.state == T3P_RD_BAD_CONFIG && f.writes == 0);
    f.baseline = 0;

    t3p_rd_test_step(&rd, &bus, 200);
    assert(rd.state == T3P_RD_OFF && f.switches == T3P_RD_CC1_OFF);
    t3p_rd_test_poll(&rd, &bus, 200);
    assert(rd.off_mv == 3010 && rd.on_mv == UINT16_MAX);
    t3p_rd_test_step(&rd, &bus, 2200);
    assert(rd.state == T3P_RD_ON && f.switches == T3P_RD_CC1_ON);
    f.mv = 5094;
    t3p_rd_test_poll(&rd, &bus, 2300);
    t3p_rd_test_poll(&rd, &bus, 2400);
    assert(rd.state == T3P_RD_ON);
    t3p_rd_test_poll(&rd, &bus, 2500);
    assert(rd.state == T3P_RD_5V_SEEN && f.switches == T3P_RD_DEFAULT);
    assert(rd.off_mv == 3010 && rd.on_mv == 5094 && f.writes == 3);

    f.mv = 3010;
    t3p_rd_test_step(&rd, &bus, 3000);
    f.mv = 5000; /* Fresh voltage check prevents enabling Rd on an already live bus. */
    t3p_rd_test_step(&rd, &bus, 3100);
    assert(rd.state == T3P_RD_VBUS_PRESENT && f.switches == T3P_RD_DEFAULT && f.writes == 5);

    f.mv = 3010;
    t3p_rd_test_step(&rd, &bus, 3200);
    t3p_rd_test_step(&rd, &bus, 3300);
    t3p_rd_test_poll(&rd, &bus, 6300);
    assert(rd.state == T3P_RD_NO_5V && f.switches == T3P_RD_DEFAULT);

    t3p_rd_test_step(&rd, &bus, 6310);
    f.sample_error = -1;
    t3p_rd_test_poll(&rd, &bus, 6320);
    assert(rd.state == T3P_RD_BUS_ERROR && f.switches == T3P_RD_DEFAULT);
    f.sample_error = 0;

    f.failed_readback = T3P_RD_CC1_OFF;
    t3p_rd_test_step(&rd, &bus, 6400);
    assert(rd.state == T3P_RD_BUS_ERROR && f.switches == T3P_RD_DEFAULT);
    f.failed_readback = 0;
    t3p_rd_test_step(&rd, &bus, UINT32_MAX - 50);
    t3p_rd_test_poll(&rd, &bus, 30000);
    assert(rd.state == T3P_RD_TIMED_OUT && f.switches == T3P_RD_DEFAULT);

    t3p_rd_test_step(&rd, &bus, 31000);
    f.restore_failures = 1;
    t3p_rd_test_cancel(&rd, &bus);
    assert(rd.state == T3P_RD_CANCELLED && f.switches == T3P_RD_DEFAULT);
    t3p_rd_test_step(&rd, &bus, 32000);
    f.restore_failures = 2;
    t3p_rd_test_cancel(&rd, &bus);
    assert(rd.state == T3P_RD_RESTORE_FAILED);
    unsigned writes = f.writes;
    t3p_rd_test_step(&rd, &bus, 33000);
    assert(f.writes == writes);
    return 0;
}
