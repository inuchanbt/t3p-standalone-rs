/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_pd.h"

enum scenario { REPLY, SILENT, ACK_ONLY, NO_RA, FAR_SINK, SRC_ON_CC2, LATE_VBUS, OVERCURRENT,
                TX_ERROR, RESTORE_ERROR };

struct fixture {
    struct t3p_bsp board;
    uint8_t regs[0x44];
    uint16_t mv;
    enum scenario scenario;
    unsigned powered, sent, resets, tx_reset, vconn_at, polls, fifo_pos;
};

static const uint8_t identity[] = {
    0xc0, 0x8f, 0x51, 0x41, 0xa8, 0x00, 0xff, 0xc2, 0x20, 0x60, 0x1c,
    0, 0, 0, 0, 0x10, 0x03, 0x14, 0x0a, 0x43, 0x26, 0x0a, 0x46, 0xfc, 0x08, 0x83, 0x76,
};
static const uint8_t baseline[0x44] = {
    [0x01] = 0x91, [0x02] = 0x03, [0x03] = 0x20, [0x04] = 0x31, [0x06] = 0x04,
    [0x08] = 0x02, [0x09] = 0x06, [0x0b] = 0x01, [0x0d] = 0x0f, [0x41] = 0x28,
};

uint32_t t3p_bsp_millis(struct t3p_bsp *board) { return board->milliseconds; }
int t3p_bsp_delay(struct t3p_bsp *board, uint32_t ms) { board->milliseconds += ms; return 0; }

int t3p_meter_identify(struct t3p_meter_bus *bus) { (void)bus; return 0; }
int t3p_meter_sample(struct t3p_meter_bus *bus, uint16_t *mv, int16_t *ma)
{
    *mv = ((struct fixture *)bus->context)->mv;
    *ma = 0;
    return 0;
}

int t3p_pd_cable_write(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value)
{
    struct fixture *f = bus->context;
    if (reg == 0x0c) {
        assert(value == 0x02); /* Local PHY reset only. */
        ++f->resets;
        f->polls = f->fifo_pos = 0;
        return 0;
    }
    if (reg == 0x02 && value == 0x64) {
        assert(f->regs[0x03] == baseline[0x03] && f->regs[0x0d] == 0x0a);
        ++f->powered;
        f->vconn_at = f->board.milliseconds;
    }
    if (reg == 0x02 && value == 0x03 && f->scenario == RESTORE_ERROR) return -1;
    if (reg == 0x0d && value == baseline[0x0d]) assert(f->regs[0x02] == baseline[0x02]);
    if (reg == 0x06) value &= ~0x40;
    if (reg == 0x07) value &= ~0x04;
    f->regs[reg] = value;
    return 0;
}

int t3p_pd_send_identity(struct t3p_meter_bus *bus)
{
    struct fixture *f = bus->context;
    assert(f->regs[0x02] == 0x64 && f->regs[0x03] == 0xb5 && f->regs[0x07] == 1);
    assert(f->regs[0x09] == 0x05 && f->board.milliseconds - f->vconn_at >= 50);
    assert(f->resets > f->tx_reset);
    f->tx_reset = f->resets;
    ++f->sent;
    return f->scenario == TX_ERROR ? -1 : 0;
}

int t3p_pd_rx_read(struct t3p_meter_bus *bus, uint8_t reg, uint8_t *out, unsigned count)
{
    struct fixture *f = bus->context;
    if (reg == 0x43) {
        assert(f->polls == 2 && f->fifo_pos + count <= sizeof(identity));
        memcpy(out, identity + f->fifo_pos, count);
        f->fifo_pos += count;
        return 0;
    }
    memcpy(out, f->regs + reg, count);
    if (reg == 0x40) {
        assert(count == 1);
        uint8_t sw = f->regs[0x02];
        out[0] = ((sw == 0x0b && f->scenario == SRC_ON_CC2) ||
                  (sw == 0x88 && f->scenario == NO_RA) ||
                  (sw == 0x44 && f->scenario != FAR_SINK)) ? 0x20 : 0;
    }
    if (reg == 0x3e && f->regs[0x02] == 0x64) {
        assert(count == 5);
        out[2] = f->scenario == LATE_VBUS ? 0xa0 : 0x20;
        if (f->scenario == OVERCURRENT) out[0] |= 0x80;
        if (f->regs[0x03] == 0xb5 && f->scenario != SILENT) {
            ++f->polls;
            if (f->polls == 1) {
                out[0] |= 0x04; /* The controller received GoodCRC. */
                if (f->scenario == REPLY) out[4] |= 0x10; /* CRC event before RX_EMPTY changes */
            }
            if (f->polls == 2 && f->scenario == REPLY) out[3] &= ~0x20;
        }
    }
    return 0;
}

int32_t meter_pd_record(const uint8_t *frame, uint32_t len, uint32_t mode, uint32_t now)
{
    (void)now;
    assert(mode == T3P_PD_CABLE && len == sizeof(identity) && !memcmp(frame, identity, len));
    return 1;
}

int t3p_pd_rx_config(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value)
{
    return t3p_pd_cable_write(bus, reg, value);
}

void meter_pd_status(uint32_t state, uint32_t cc, uint32_t mode)
{
    assert(state == T3P_PD_LISTENING && cc == 1 && mode == T3P_PD_CABLE);
}

int main(void)
{
    struct fixture f = { .mv = 3010 };
    memcpy(f.regs, baseline, sizeof(baseline));
    struct t3p_meter_bus bus = { .context = &f };
    static const struct {
        enum scenario scenario;
        uint16_t mv;
        enum t3p_pd_state expected;
        unsigned power, send;
    } cases[] = {
        { REPLY, 5000, T3P_PD_CABLE_VBUS, 0, 0 },
        { SRC_ON_CC2, 3010, T3P_PD_CABLE_CC_BUSY, 0, 0 },
        { FAR_SINK, 3010, T3P_PD_CABLE_CC_BUSY, 0, 0 },
        { NO_RA, 3010, T3P_PD_CABLE_NO_RA, 0, 0 },
        { LATE_VBUS, 3010, T3P_PD_CABLE_VBUS, 1, 0 },
        { OVERCURRENT, 3010, T3P_PD_CABLE_VCONN_FAULT, 1, 0 },
        { REPLY, 3010, T3P_PD_CAPTURED, 1, 1 },
        { REPLY, 3010, T3P_PD_CAPTURED, 1, 1 },
        { SILENT, 3010, T3P_PD_TIMEOUT, 1, 1 },
        { ACK_ONLY, 3010, T3P_PD_CABLE_NO_ID, 1, 1 },
        { TX_ERROR, 3010, T3P_PD_BUS_ERROR, 1, 1 },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        f.scenario = cases[i].scenario;
        f.mv = cases[i].mv;
        f.powered = f.sent = 0;
        assert(t3p_cable_read(&f.board, &bus) == cases[i].expected);
        assert(f.powered == cases[i].power && f.sent == cases[i].send);
        assert(!memcmp(f.regs, baseline, sizeof(baseline)));
    }

    f.scenario = RESTORE_ERROR;
    struct t3p_pd_capture capture = {0};
    t3p_pd_read_cable(&capture, &bus, &f.board);
    assert(capture.state == T3P_PD_CABLE_RESTORE && f.regs[0x03] == baseline[0x03]);
    assert(f.regs[0x0d] == 0x0a); /* Keep the lower current limit if VCONN-off is unconfirmed. */
    unsigned sent = f.sent;
    t3p_pd_read_cable(&capture, &bus, &f.board);
    t3p_pd_start_capture(&capture, &bus, 0, T3P_PD_SNIFF);
    assert(capture.state == T3P_PD_CABLE_RESTORE && f.sent == sent);
    return 0;
}
