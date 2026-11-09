/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_rd_test.h"

#define OFF_TIMEOUT_MS 30000u
#define ON_TIMEOUT_MS 3000u
#define SAMPLE_MS 100u
/* The HID-only setup has shown an apparent 3 V on the uncalibrated meter. */
#define VBUS_PRESENT_MV 4000u
#define VBUS_5V_MIN_MV 4500u
#define VBUS_5V_MAX_MV 5500u

static void finish(struct t3p_rd_test *test, struct t3p_meter_bus *bus, enum t3p_rd_state state)
{
    if (t3p_pd_set_rd(bus, T3P_RD_DEFAULT) && t3p_pd_set_rd(bus, T3P_RD_DEFAULT))
        state = T3P_RD_RESTORE_FAILED;
    test->state = state;
}

void t3p_rd_test_cancel(struct t3p_rd_test *test, struct t3p_meter_bus *bus)
{
    if (test->state == T3P_RD_OFF || test->state == T3P_RD_ON)
        finish(test, bus, T3P_RD_CANCELLED);
}

void t3p_rd_test_double_press(struct t3p_rd_test *test, struct t3p_meter_bus *bus, uint32_t now)
{
    if (test->state == T3P_RD_ON || test->state == T3P_RD_RESTORE_FAILED) return;

    uint16_t mv;
    int16_t ma;
    if (test->state == T3P_RD_OFF) {
        if (now - test->started >= OFF_TIMEOUT_MS) {
            finish(test, bus, T3P_RD_TIMED_OUT);
            return;
        }
        if (t3p_meter_sample(bus, &mv, &ma)) {
            finish(test, bus, T3P_RD_BUS_ERROR);
            return;
        }
        test->off_mv = mv;
        if (mv >= VBUS_PRESENT_MV) {
            finish(test, bus, T3P_RD_VBUS_PRESENT);
            return;
        }
        if (t3p_pd_set_rd(bus, T3P_RD_CC1_ON)) {
            finish(test, bus, T3P_RD_BUS_ERROR);
            return;
        }
        test->state = T3P_RD_ON;
        test->started = test->sampled = now;
        return;
    }

    *test = (struct t3p_rd_test){ .off_mv = UINT16_MAX, .on_mv = UINT16_MAX };
    int baseline = t3p_pd_rd_baseline(bus);
    if (baseline) {
        test->state = baseline < 0 ? T3P_RD_BUS_ERROR : T3P_RD_BAD_CONFIG;
    } else if (t3p_meter_identify(bus) || t3p_meter_sample(bus, &mv, &ma)) {
        test->state = T3P_RD_BUS_ERROR;
    } else if (mv >= VBUS_PRESENT_MV) {
        test->state = T3P_RD_VBUS_PRESENT;
    } else if (t3p_pd_set_rd(bus, T3P_RD_CC1_OFF)) {
        finish(test, bus, T3P_RD_BUS_ERROR);
    } else {
        test->state = T3P_RD_OFF;
        test->started = now;
        test->sampled = now - SAMPLE_MS;
    }
}

void t3p_rd_test_poll(struct t3p_rd_test *test, struct t3p_meter_bus *bus, uint32_t now)
{
    int off = test->state == T3P_RD_OFF;
    if (!off && test->state != T3P_RD_ON) return;
    if (now - test->started >= (off ? OFF_TIMEOUT_MS : ON_TIMEOUT_MS)) {
        finish(test, bus, off ? T3P_RD_TIMED_OUT : T3P_RD_NO_5V);
        return;
    }
    if (now - test->sampled < SAMPLE_MS) return;
    test->sampled = now;

    uint16_t mv;
    int16_t ma;
    if (t3p_meter_sample(bus, &mv, &ma)) {
        finish(test, bus, T3P_RD_BUS_ERROR);
    } else if (off) {
        test->off_mv = mv;
        if (mv >= VBUS_PRESENT_MV) finish(test, bus, T3P_RD_VBUS_PRESENT);
    } else {
        test->on_mv = mv;
        if (mv > VBUS_5V_MAX_MV) {
            finish(test, bus, T3P_RD_VBUS_HIGH);
        } else {
            test->good_samples = mv >= VBUS_5V_MIN_MV ? test->good_samples + 1 : 0;
            if (test->good_samples == 3) finish(test, bus, T3P_RD_5V_SEEN);
        }
    }
}
