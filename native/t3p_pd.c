/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_pd.h"

#define SWITCHES0 0x02
#define CONTROL1 0x07
#define POWER 0x0b
#define INTERRUPTA 0x3e
#define STATUS1 0x41
#define FIFO 0x43
#define RX_EMPTY 0x20
#define RX_FULL 0x10
#define I_ALERT 0x08
#define TX_IDLE_MASK 0x0f
#define TX_IDLE 0x08
#define RX_BURST_LIMIT 4u

/* 1: complete, 2: partial EPR, 3: first EPR chunk, 0: ignored, -1: bad frame. */
int32_t meter_pd_record(const uint8_t *, uint32_t, uint32_t, uint32_t);

static int configured(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value)
{
    uint8_t actual;
    uint8_t expected = reg == CONTROL1 ? 0 : value; /* RX_FLUSH self clears */
    return !t3p_pd_rx_config(bus, reg, value) &&
           !t3p_pd_rx_read(bus, reg, &actual, 1) && actual == expected;
}

static void stop_capture(struct t3p_pd_capture *capture, struct t3p_meter_bus *bus,
                         enum t3p_pd_state state)
{
    if (!configured(bus, POWER, 0x01) || !configured(bus, SWITCHES0, 0x03))
        state = T3P_PD_BUS_ERROR;
    capture->state = state;
}

void t3p_pd_start_capture(struct t3p_pd_capture *capture, struct t3p_meter_bus *bus, uint32_t now,
                          enum t3p_pd_mode mode)
{
    if (capture->state == T3P_PD_LISTENING) stop_capture(capture, bus, T3P_PD_READY);
    if (capture->state == T3P_PD_BUS_ERROR || capture->state == T3P_PD_CONFIG ||
        capture->state == T3P_PD_TIMER_ERROR)
        return;
    capture->fifo_warning = 0;
    /* Only CC1 reception has been observed on the tested T3P. */
    capture->cc = 1;
    capture->mode = mode;

    uint8_t registers[9], power, status[5];
    capture->state = T3P_PD_BUS_ERROR;
    if (t3p_pd_rx_read(bus, 0x01, registers, sizeof(registers)) ||
        t3p_pd_rx_read(bus, POWER, &power, 1) ||
        t3p_pd_rx_read(bus, INTERRUPTA, status, sizeof(status)))
        return;

    /* Require the observed startup routing and a quiescent transmitter.
     * Gate AUTO_CRC, TXCC1/2, MEAS_VBUS, AUTO_PRE/TX_START, BIST,
     * TOGGLE and autonomous reset/retry before enabling any RX circuitry. */
    if (registers[0] != 0x91 || registers[1] != 0x03 || (registers[2] & 7) ||
        (registers[3] & 0x40) || (registers[5] & 3) || registers[6] ||
        registers[7] != 0x02 || registers[8] != 0x06 || power != 0x01 ||
        (status[3] & TX_IDLE_MASK) != TX_IDLE || (status[0] & 0xfc) ||
        (status[1] & 1) || (status[4] & 2)) {
        capture->state = T3P_PD_CONFIG;
        return;
    }

    /* Flush with the oscillator on and the receiver off, then select one CC.
     * Once a write or readback fails, leave further configuration to a reboot. */
    uint8_t fifo_status;
    if (!configured(bus, POWER, 0x09) || !configured(bus, CONTROL1, 0x04) ||
        t3p_pd_rx_read(bus, STATUS1, &fifo_status, 1))
        return;
    if ((fifo_status & 0x3f) != (RX_EMPTY | TX_IDLE)) {
        stop_capture(capture, bus, T3P_PD_FIFO_FULL);
        return;
    }
    if (!configured(bus, SWITCHES0, 0x07) ||
        !configured(bus, POWER, 0x07))
        return;

    capture->started = now;
    capture->polled = now - 4;
    capture->draining = 0;
    capture->partial = 0;
    capture->catch_up = 0;
    capture->state = T3P_PD_LISTENING;
}

/* Return one only after consuming a complete FIFO record. */
static int poll_one(struct t3p_pd_capture *capture, struct t3p_meter_bus *bus, uint32_t now)
{
    uint32_t interval = capture->mode == T3P_PD_SPR ? 4u : 1u;
    if (capture->state != T3P_PD_LISTENING || now - capture->polled < interval) return 0;
    capture->polled = now;
    uint32_t timeout = capture->mode == T3P_PD_SPR ? 10000 : 30000;
    if (now - capture->started >= timeout) {
        stop_capture(capture, bus, capture->partial ? T3P_PD_PARTIAL : T3P_PD_TIMEOUT);
        return 0;
    }

    /* INTERRUPTA/B, STATUS0/1 and INTERRUPT are consecutive. The CRC interrupt
     * is a completion hint. The decoder checks the CRC of each FIFO record. */
    uint8_t status[5];
    if (t3p_pd_rx_read(bus, INTERRUPTA, status, sizeof(status))) {
        capture->state = T3P_PD_BUS_ERROR;
        return 0;
    }
    if ((status[3] & TX_IDLE_MASK) != TX_IDLE || (status[0] & 0xfc) ||
        (status[1] & 1) || (status[4] & 2)) {
        capture->state = T3P_PD_CONFIG;
        return 0;
    }
    if (status[0] & 1) {
        stop_capture(capture, bus, T3P_PD_LINK_RESET);
        return 0;
    }
    /* I_ALERT is a read-clear warning; a transient alert does not prove the
     * FIFO contents were lost. Continue as before, retaining a visible warning.
     * Stop if RX_FULL is actually asserted in the STATUS1 snapshot. */
    if (status[4] & I_ALERT) capture->fifo_warning = 1;
    if (status[3] & RX_FULL) {
        capture->fifo_warning = 1;
        stop_capture(capture, bus, T3P_PD_FIFO_FULL);
        return 0;
    }
    if (status[3] & RX_EMPTY) {
        /* INTERRUPT is sampled after STATUS1. Keep a completion that arrived
         * between those reads for the next poll. */
        capture->draining = status[4] & 0x10;
        return 0;
    }
    capture->draining |= status[4] & 0x10;
    if (!capture->draining || (status[2] & 0x40)) return 0; /* CC still active */

    uint8_t frame[35];
    if (t3p_pd_rx_read(bus, FIFO, frame, 3)) {
        capture->state = T3P_PD_BUS_ERROR;
        return 0;
    }
    unsigned body = 4u + 4u * ((frame[2] >> 4) & 7u);
    if ((frame[0] >> 5) != 7) {
        stop_capture(capture, bus, T3P_PD_BAD_FRAME);
        return 0;
    }
    if (t3p_pd_rx_read(bus, FIFO, frame + 3, body)) {
        capture->state = T3P_PD_BUS_ERROR;
        return 0;
    }
    /* A queued follow-up need not wait for the normal poll interval. */
    capture->polled = now - interval;
    int32_t result = meter_pd_record(frame, 3 + body, capture->mode, now);
    if (result == 2) capture->partial = 1;
    if (result == 3) capture->partial = 0;
    if (result == 1 || result < 0)
        stop_capture(capture, bus, result > 0 ? T3P_PD_CAPTURED : T3P_PD_BAD_FRAME);
    return 1;
}

void t3p_pd_poll_capture(struct t3p_pd_capture *capture, struct t3p_meter_bus *bus, uint32_t now)
{
    /* Catch up on queued EPR or sniff traffic without a display strip or sleep
     * between records. Bound work so other tasks still run regularly. */
    capture->catch_up = 0;
    for (unsigned i = 0; i < RX_BURST_LIMIT; ++i) {
        if (!poll_one(capture, bus, now) || capture->mode == T3P_PD_SPR)
            break;
        if (i + 1 == RX_BURST_LIMIT && capture->state == T3P_PD_LISTENING)
            capture->catch_up = 1;
    }
}
