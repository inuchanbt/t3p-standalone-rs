/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_rd_test.h"

#define OFF_TIMEOUT_MS 30000u
#define ON_TIMEOUT_MS 3000u
#define SAMPLE_MS 100u
/* The HID-only setup has shown an apparent 3 V on the uncalibrated meter. */
#define VBUS_PRESENT_MV 4000u
#define VBUS_5V_MIN_MV 4500u
#define VBUS_5V_MAX_MV 5500u

#ifdef T3P_PD_ACK_TEST
#define INTERRUPTA 0x3e
#define FIFO 0x43
#define I_GCRCSENT 0x01
#define I_CRC_CHK 0x10
#define RX_FULL 0x10
#define RX_EMPTY 0x20
#define ACTIVITY 0x40

int32_t meter_pd_ack_pdo_count(const uint8_t *, uint32_t);

static enum t3p_rd_state poll_ack(struct t3p_rd_test *test, struct t3p_meter_bus *bus)
{
    if (test->pdos && test->goodcrc) return T3P_RD_ON;

    /* Read INTERRUPTA/B, STATUS0/1 and INTERRUPT together. Keep read-clear
       bits even if the FIFO is still empty when STATUS1 is read. */
    uint8_t status[5];
    if (t3p_pd_rx_read(bus, INTERRUPTA, status, sizeof(status))) return T3P_RD_BUS_ERROR;
    test->goodcrc |= status[1] & I_GCRCSENT;
    test->rx_ready |= (status[1] & I_GCRCSENT) || (status[4] & I_CRC_CHK);
    if (status[3] & RX_FULL) return T3P_RD_RX_ERROR;
    if (test->pdos || !test->rx_ready || (status[3] & RX_EMPTY) || (status[2] & ACTIVITY))
        return T3P_RD_ON;

    uint8_t frame[35];
    if (t3p_pd_rx_read(bus, FIFO, frame, 3)) return T3P_RD_BUS_ERROR;
    if (frame[0] >> 5 != 7) return T3P_RD_RX_ERROR;
    unsigned body = 4u + 4u * ((frame[2] >> 4) & 7u);
    if (t3p_pd_rx_read(bus, FIFO, frame + 3, body)) return T3P_RD_BUS_ERROR;
    int32_t pdos = meter_pd_ack_pdo_count(frame, 3 + body);
    if (pdos < 0) return T3P_RD_RX_ERROR;
    test->pdos = (uint8_t)pdos;
    test->rx_ready = 0;
    return T3P_RD_ON;
}
#endif

static void finish(struct t3p_rd_test *test, struct t3p_meter_bus *bus, enum t3p_rd_state state)
{
#ifdef T3P_PD_ACK_TEST
    if (test->restore_ack && t3p_pd_disarm_ack(bus, test->saved_switches1))
        state = T3P_RD_RESTORE_FAILED;
#endif
    if (t3p_pd_set_rd(bus, T3P_RD_DEFAULT) && t3p_pd_set_rd(bus, T3P_RD_DEFAULT))
        state = T3P_RD_RESTORE_FAILED;
    test->state = state;
}

void t3p_rd_test_cancel(struct t3p_rd_test *test, struct t3p_meter_bus *bus)
{
    if (test->state == T3P_RD_OFF || test->state == T3P_RD_ON)
        finish(test, bus, T3P_RD_CANCELLED);
}

void t3p_rd_test_step(struct t3p_rd_test *test, struct t3p_meter_bus *bus, uint32_t now)
{
    if (test->state == T3P_RD_ON || test->state == T3P_RD_RESTORE_FAILED ||
        test->state == T3P_RD_TIMER_ERROR) return;

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
#ifdef T3P_PD_ACK_TEST
        int armed = t3p_pd_arm_ack(bus, &test->saved_switches1, &test->restore_ack);
        if (armed) {
            finish(test, bus, armed < 0 ? T3P_RD_BUS_ERROR : T3P_RD_BAD_CONFIG);
            return;
        }
#endif
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
        enum t3p_rd_state state = off ? T3P_RD_TIMED_OUT : T3P_RD_NO_5V;
#ifdef T3P_PD_ACK_TEST
        if (!off && test->on_mv != UINT16_MAX && test->on_mv >= VBUS_5V_MIN_MV)
            state = test->pdos ? T3P_RD_NO_GOODCRC : T3P_RD_NO_CAPS;
#endif
        finish(test, bus, state);
        return;
    }
#ifdef T3P_PD_ACK_TEST
    if (!off) {
        enum t3p_rd_state state = poll_ack(test, bus);
        if (state != T3P_RD_ON) {
            finish(test, bus, state);
            return;
        }
    }
#endif
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
        }
#ifdef T3P_PD_ACK_TEST
        else if (mv >= VBUS_5V_MIN_MV && test->pdos && test->goodcrc) {
            finish(test, bus, T3P_RD_5V_SEEN);
        }
#else
        else {
            test->good_samples = mv >= VBUS_5V_MIN_MV ? test->good_samples + 1 : 0;
            if (test->good_samples == 3) finish(test, bus, T3P_RD_5V_SEEN);
        }
#endif
    }
}
