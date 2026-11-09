/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_RD_TEST_H
#define T3P_RD_TEST_H
#include "t3p_meter.h"

/* SWITCHES0 values from the stock 3.0.3 startup and CC1 sink sequence. */
enum { T3P_RD_DEFAULT = 0x03, T3P_RD_CC1_OFF = 0x04, T3P_RD_CC1_ON = 0x05 };

/* State numbers are displayed by src/screen/rd_test.rs. */
enum t3p_rd_state {
    T3P_RD_READY, T3P_RD_OFF, T3P_RD_ON, T3P_RD_5V_SEEN, T3P_RD_NO_5V,
    T3P_RD_TIMED_OUT, T3P_RD_CANCELLED, T3P_RD_VBUS_PRESENT, T3P_RD_BAD_CONFIG,
    T3P_RD_BUS_ERROR, T3P_RD_RESTORE_FAILED, T3P_RD_VBUS_HIGH
};

struct t3p_rd_test {
    enum t3p_rd_state state;
    uint32_t started, sampled;
    uint16_t off_mv, on_mv;
    unsigned good_samples;
};

int t3p_pd_rd_baseline(struct t3p_meter_bus *);
int t3p_pd_set_rd(struct t3p_meter_bus *, uint8_t switches);
void t3p_rd_test_step(struct t3p_rd_test *, struct t3p_meter_bus *, uint32_t now);
void t3p_rd_test_cancel(struct t3p_rd_test *, struct t3p_meter_bus *);
void t3p_rd_test_poll(struct t3p_rd_test *, struct t3p_meter_bus *, uint32_t now);
#endif
