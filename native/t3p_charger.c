/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_charger.h"
#include "t3p_pd.h"
#include "t3p_rd_test.h"

#define SWITCHES1 0x03
#define CONTROL0 0x06
#define CONTROL1 0x07
#define CONTROL3 0x09
#define POWER 0x0b
#define RESET 0x0c
#define INTERRUPTA 0x3e
#define STATUS0 0x40
#define FIFO 0x43

#define TX_FLUSH 0x40
#define RX_FLUSH 0x04
#define PD_RESET 0x02
#define RETRY_TWICE 0x05
#define RETRY_THRICE 0x07
#define STOCK_RETRY 0x06
#define OSC_POWER 0x09
#define STOCK_POWER 0x01
#define I_RETRYFAIL 0x10
#define I_TXSENT 0x04
#define I_HARDRST 0x01
#define I_GCRCSENT 0x01
#define I_COLLISION 0x02
#define I_CRC_CHK 0x10
#define VBUSOK 0x80
#define ACTIVITY 0x40
#define CRC_CHK 0x10
#define SINK_TX_OK 0x03
#define RX_EMPTY 0x20
#define RX_FULL 0x10
#define TX_EMPTY 0x08
#define SOP_TOKEN 7u

#define VBUS_PRESENT_MV 4000u /* The HID-only meter can read about 3 V. */
#define VBUS_5V_MIN_MV 4500u
#define VBUS_5V_MAX_MV 5500u
#define OFFER_MS 1500u
#define REPLY_MS 45u
#define PS_RDY_MS 550u
#define SINK_TX_OK_MS 500u
#define DETACH_MS 350u

enum { IRQ_A, IRQ_B, LIVE0, LIVE1, IRQ, STATUS_COUNT };
enum { OFFER, ACCEPT, PS_RDY, EXTENDED };
enum { IGNORE, MATCH, REJECT, WAIT, UNSUPPORTED, LINK_RESET, INVALID };

uint32_t meter_charger_frame(const uint8_t *, uint32_t, uint32_t phase);
uint32_t meter_charger_request(void);
void meter_charger_vbus(uint32_t mv);

struct pd_link {
    unsigned rx_ready, tx_acked;
};

static int configure(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value, uint8_t expected)
{
    uint8_t actual;
    return t3p_pd_cable_write(bus, reg, value) ||
           t3p_pd_rx_read(bus, reg, &actual, 1) || actual != expected;
}

static int restore(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value, uint8_t expected)
{
    return configure(bus, reg, value, expected) && configure(bus, reg, value, expected);
}

enum t3p_charger_state t3p_charger_cancel(struct t3p_meter_bus *bus, enum t3p_charger_state state)
{
    if (t3p_pd_set_rd(bus, T3P_RD_DEFAULT) && t3p_pd_set_rd(bus, T3P_RD_DEFAULT))
        return CHARGER_RESTORE;
    return state;
}

static enum t3p_charger_state check_vbus(struct t3p_meter_bus *bus, unsigned attached)
{
    uint16_t mv;
    int16_t ma;
    if (t3p_meter_sample(bus, &mv, &ma)) return CHARGER_IO;
    if (attached) {
        meter_charger_vbus(mv);
        if (mv < VBUS_5V_MIN_MV || mv > VBUS_5V_MAX_MV) return CHARGER_VBUS_RANGE;
    } else if (mv >= VBUS_PRESENT_MV) {
        meter_charger_vbus(mv);
        return CHARGER_VBUS_PRESENT;
    }
    return CHARGER_READY;
}

enum t3p_charger_state t3p_charger_arm(struct t3p_meter_bus *bus)
{
    int baseline = t3p_pd_rd_baseline(bus);
    if (baseline) return baseline < 0 ? CHARGER_IO : CHARGER_CONFIG;
    if (t3p_meter_identify(bus)) return CHARGER_IO;
    enum t3p_charger_state state = check_vbus(bus, 0);
    if (state != CHARGER_READY) return state;
    if (t3p_pd_set_rd(bus, T3P_RD_CC1_OFF)) return t3p_charger_cancel(bus, CHARGER_IO);
    return CHARGER_ARMED;
}

static enum t3p_charger_state read_status(struct t3p_meter_bus *bus, uint8_t status[STATUS_COUNT])
{
    if (t3p_pd_rx_read(bus, INTERRUPTA, status, STATUS_COUNT)) return CHARGER_IO;
    if (status[IRQ_A] & I_HARDRST) return CHARGER_RESET;
    if (status[IRQ_A] & I_RETRYFAIL) return CHARGER_NO_GOODCRC;
    if (status[IRQ] & I_COLLISION) return CHARGER_BUSY;
    if (status[LIVE1] & RX_FULL) return CHARGER_RX_ERROR;
    return CHARGER_READY;
}

static enum t3p_charger_state receive(struct t3p_bsp *board, struct t3p_meter_bus *bus,
                                      struct pd_link *link, unsigned phase, uint32_t timeout,
                                      enum t3p_charger_state expired)
{
    uint32_t start = t3p_bsp_millis(board);
    unsigned needs_ack = phase == ACCEPT || phase == EXTENDED;
    unsigned matched = 0, rx_acked = 0, ignored = 0;
    do {
        uint8_t status[STATUS_COUNT];
        enum t3p_charger_state state = read_status(bus, status);
        if (state != CHARGER_READY) return state;
        link->tx_acked |= status[IRQ_A] & I_TXSENT;
        rx_acked |= status[IRQ_B] & I_GCRCSENT;
        /* An interrupt can be reported before RX_EMPTY changes. */
        link->rx_ready |= link->tx_acked || rx_acked ||
                          (status[IRQ] & I_CRC_CHK) || (status[LIVE0] & CRC_CHK);
        if (!matched && link->rx_ready && !(status[LIVE1] & RX_EMPTY) && !(status[LIVE0] & ACTIVITY)) {
            uint8_t frame[35];
            if (t3p_pd_rx_read(bus, FIFO, frame, 3)) return CHARGER_IO;
            if (frame[0] >> 5 != SOP_TOKEN) return CHARGER_RX_ERROR;
            unsigned body = 4u + 4u * ((frame[2] >> 4) & 7u);
            if (t3p_pd_rx_read(bus, FIFO, frame + 3, body)) return CHARGER_IO;
            switch (meter_charger_frame(frame, 3 + body, phase)) {
            case MATCH: matched = 1; break;
            case REJECT: return CHARGER_REJECTED;
            case WAIT: return CHARGER_WAIT;
            case UNSUPPORTED: return CHARGER_UNSUPPORTED;
            case LINK_RESET: return CHARGER_RESET;
            case INVALID: return CHARGER_RX_ERROR;
            default:
                /* A response can already be queued behind its GoodCRC. */
                if (++ignored == 16) return CHARGER_BUSY;
                continue;
            }
        }
        if (matched && (phase != OFFER || rx_acked) && (!needs_ack || link->tx_acked))
            return CHARGER_READY;
        if (t3p_bsp_delay(board, 1)) return CHARGER_TIMER;
    } while (t3p_bsp_millis(board) - start < timeout);
    if ((phase == OFFER && matched && !rx_acked) || (needs_ack && !link->tx_acked))
        return CHARGER_NO_GOODCRC;
    return expired;
}

static enum t3p_charger_state wait_sink_tx_ok(struct t3p_bsp *board, struct t3p_meter_bus *bus)
{
    uint32_t start = t3p_bsp_millis(board);
    do {
        uint8_t status[STATUS_COUNT];
        enum t3p_charger_state state = read_status(bus, status);
        if (state != CHARGER_READY) return state;
        if (!(status[LIVE0] & VBUSOK)) return CHARGER_VBUS_RANGE;
        if ((status[LIVE0] & (ACTIVITY | 3)) == SINK_TX_OK &&
            (status[LIVE1] & (RX_EMPTY | TX_EMPTY)) == (RX_EMPTY | TX_EMPTY))
            return CHARGER_READY;
        if (t3p_bsp_delay(board, 1)) return CHARGER_TIMER;
    } while (t3p_bsp_millis(board) - start < SINK_TX_OK_MS);
    return CHARGER_EXT_SKIPPED;
}

static enum t3p_charger_state exchange(struct t3p_bsp *board, struct t3p_meter_bus *bus)
{
    struct pd_link link = {0};
    enum t3p_charger_state state = receive(board, bus, &link, OFFER, OFFER_MS, CHARGER_NO_CAPS);
    if (state != CHARGER_READY) return state;
    state = check_vbus(bus, 1);
    if (state != CHARGER_READY) return state;

    uint32_t parameters = meter_charger_request();
    uint16_t revision = parameters & 0xc0u;
    unsigned current = parameters & 0x3fu; /* In 10 mA units, capped by the decoder. */
    if (revision == 0x40 && configure(bus, CONTROL3, RETRY_THRICE, RETRY_THRICE))
        return CHARGER_IO;
    uint32_t rdo = 0x11000000u | (current << 10) | current; /* PDO 1, No USB Suspend */
    const uint8_t payload[] = {rdo, rdo >> 8, rdo >> 16, rdo >> 24};
    link = (struct pd_link){0};
    if (t3p_pd_sop_tx(bus, 0x1002 | revision, payload, sizeof(payload))) return CHARGER_IO;
    state = receive(board, bus, &link, ACCEPT, REPLY_MS, CHARGER_NO_ACCEPT);
    if (state != CHARGER_READY) return state;
    state = receive(board, bus, &link, PS_RDY, PS_RDY_MS, CHARGER_NO_PS_RDY);
    if (state != CHARGER_READY) return state;
    state = check_vbus(bus, 1);
    if (state != CHARGER_READY) return state;
    if (revision != 0x80) return CHARGER_PD2;

    state = wait_sink_tx_ok(board, bus);
    if (state != CHARGER_READY) return state;
    link = (struct pd_link){0};
    /* Message ID 1 after the acknowledged Request, Get_Source_Cap_Extended. */
    if (t3p_pd_sop_tx(bus, (1u << 9) | revision | 17u, 0, 0)) return CHARGER_IO;
    state = receive(board, bus, &link, EXTENDED, REPLY_MS, CHARGER_NO_EXT);
    if (state != CHARGER_READY) return state;
    /* Allow the PHY to finish its automatic GoodCRC before disconnecting. */
    return t3p_bsp_delay(board, 1) ? CHARGER_TIMER : CHARGER_COMPLETE;
}

enum t3p_charger_state t3p_charger_scan(struct t3p_bsp *board, struct t3p_meter_bus *bus)
{
    enum t3p_charger_state state = check_vbus(bus, 0);
    if (state != CHARGER_READY) return t3p_charger_cancel(bus, state);
    uint8_t control0, switches1 = 0, status;
    if (t3p_pd_rx_read(bus, CONTROL0, &control0, 1))
        return t3p_charger_cancel(bus, CHARGER_IO);
    unsigned restore_phy = 0, attached = 0;
    int armed = t3p_pd_arm_ack(bus, &switches1, &restore_phy);
    if (armed) state = armed < 0 ? CHARGER_IO : CHARGER_CONFIG;
    else if (configure(bus, CONTROL3, RETRY_TWICE, RETRY_TWICE) ||
             t3p_pd_read_register(bus, STATUS0, &status))
        state = CHARGER_IO;
    else if (status & VBUSOK) state = CHARGER_VBUS_PRESENT;
    else {
        attached = 1; /* A failed readback can follow a successful write. */
        state = t3p_pd_set_rd(bus, T3P_RD_CC1_ON) ? CHARGER_IO : exchange(board, bus);
    }

    int failed = 0;
    if (restore_phy) {
        failed = restore(bus, SWITCHES1, switches1, switches1);
        if (attached)
            failed |= t3p_pd_set_rd(bus, T3P_RD_CC1_OFF) && t3p_pd_set_rd(bus, T3P_RD_CC1_OFF);
        failed |= restore(bus, POWER, OSC_POWER, OSC_POWER);
        failed |= t3p_pd_cable_write(bus, RESET, PD_RESET) &&
                  t3p_pd_cable_write(bus, RESET, PD_RESET);
        failed |= restore(bus, CONTROL0, control0 | TX_FLUSH, control0);
        failed |= restore(bus, CONTROL1, RX_FLUSH, 0);
        failed |= restore(bus, CONTROL3, STOCK_RETRY, STOCK_RETRY);
        failed |= restore(bus, POWER, STOCK_POWER, STOCK_POWER);
    }
    if (attached && t3p_bsp_delay(board, DETACH_MS)) state = CHARGER_TIMER;
    return t3p_charger_cancel(bus, failed ? CHARGER_RESTORE : state);
}
