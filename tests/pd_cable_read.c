/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_pd.h"

enum scenario { REPLY, ACTIVE, LEGACY, SILENT, ACK_ONLY, NO_RA, REVERSED, SHORT_CC2, RD_ON_CC2, FAR_SINK,
                SRC_ON_CC2, VBUS_DURING_PROBE, LATE_VBUS, OVERCURRENT, TX_ERROR, RESTORE_ERROR };

struct step {
    uint16_t header;
    uint32_t payload;
    enum t3p_cable_query query;
    unsigned index;
    enum t3p_cable_result result;
};

static const struct step passive[] = {
    { 0x108f, 0xff00a001, CABLE_ID, 0, CABLE_ACK },
    { 0x128f, 0xff00a002, CABLE_SVIDS, 0, CABLE_BUSY },
    { 0x148f, 0xff00a002, CABLE_SVIDS, 0, CABLE_MORE },
    { 0x168f, 0xff00a002, CABLE_SVIDS, 0, CABLE_ACK },
    { 0x188f, 0x1e4ea003, CABLE_MODES, 0, CABLE_ACK },
    { 0x1a8f, 0x8087a003, CABLE_MODES, 1, CABLE_NO_REPLY },
    { 0x9c86, 0x00008002, CABLE_MFR, 0, CABLE_ACK },
};

static const struct step active[] = {
    { 0x108f, 0xff00a001, CABLE_ID, 0, CABLE_ACK },
    { 0x9286, 0x00008002, CABLE_MFR, 0, CABLE_UNSUPPORTED },
    { 0x0492, 0, CABLE_STATUS, 0, CABLE_ACK },
    { 0x008d, 0, CABLE_SOP2_RESET, 0, CABLE_ACK },
    { 0x0292, 0, CABLE_SOP2_STATUS, 0, CABLE_ACK },
};

static const struct step legacy[] = {
    { 0x108f, 0xff00a001, CABLE_ID, 0, CABLE_NO_CRC },
    { 0x104f, 0xff008001, CABLE_ID, 0, CABLE_ACK },
};

static const struct step silent[] = {
    { 0x108f, 0xff00a001, CABLE_ID, 0, CABLE_NO_CRC },
    { 0x104f, 0xff008001, CABLE_ID, 0, CABLE_NO_CRC },
};

static const struct step ack_only[] = {
    { 0x108f, 0xff00a001, CABLE_ID, 0, CABLE_NO_REPLY },
    { 0x124f, 0xff008001, CABLE_ID, 0, CABLE_NO_REPLY },
};

struct fixture {
    struct t3p_bsp board;
    uint8_t regs[0x44];
    uint16_t mv;
    enum scenario scenario;
    unsigned powered, sent, reported, resets, tx_reset, vconn_at, tx_at, polls;
    const struct step *steps;
    unsigned step_count, fifo_pos, fifo_len;
    uint8_t fifo[14];
};

/* Test the native query loop with two FIFO records. Rust validates the real payloads. */
static const uint8_t reply[] = { 0xc0, 0x81, 1, 0, 0, 0, 0, 0xc0, 0x83, 1, 0, 0, 0, 0 };
static const uint8_t baseline[0x44] = {
    [0x01] = 0x91, [0x02] = 0x03, [0x03] = 0x20, [0x04] = 0x31, [0x06] = 0x04,
    [0x08] = 0x02, [0x09] = 0x06, [0x0b] = 0x01, [0x0d] = 0x0f, [0x41] = 0x28,
};
static unsigned cc_probe[2];
static struct fixture *fixture;

void meter_pd_cable_cc(uint32_t cc1, uint32_t cc2)
{
    assert(cc1 <= 64 && cc2 <= 64);
    cc_probe[0] = cc1;
    cc_probe[1] = cc2;
}

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
        f->polls = f->fifo_pos = f->fifo_len = 0;
        return 0;
    }
    if (reg == 0x02 && (value & 0x30)) {
        assert(value == 0x64 && f->regs[0x03] == baseline[0x03] && f->regs[0x0d] == 0x0a);
        assert(f->regs[0x04] == 0x3d && f->regs[0x06] == 0x2c);
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

int t3p_pd_cable_tx(struct t3p_meter_bus *bus, unsigned second, uint16_t header,
                   const uint8_t *payload, unsigned length)
{
    struct fixture *f = bus->context;
    assert(f->sent < f->step_count && f->sent == f->reported && f->fifo_pos == f->fifo_len);
    const struct step *step = &f->steps[f->sent];
    assert(header == step->header && second == (step->query >= CABLE_SOP2_RESET));
    assert(length == (step->payload ? 4u : 0u));
    for (unsigned i = 0; i < length; ++i) assert(payload[i] == (uint8_t)(step->payload >> (i * 8)));
    assert(f->regs[0x02] == 0x64 && f->regs[0x03] == 0xb5 && f->regs[0x07] == (second ? 2 : 1));
    assert(f->regs[0x09] == 0x05 && f->board.milliseconds - f->vconn_at >= 50);
    if (!f->sent) assert(f->resets > f->tx_reset);
    if (f->sent && f->steps[f->sent - 1].result == CABLE_BUSY)
        assert(f->board.milliseconds - f->tx_at >= 50);
    f->tx_reset = f->resets;
    f->tx_at = f->board.milliseconds;
    ++f->sent;
    f->polls = f->fifo_pos = 0;
    f->fifo_len = step->result == CABLE_NO_CRC ? 0 : step->result == CABLE_NO_REPLY ? 7 : sizeof(reply);
    memcpy(f->fifo, reply, sizeof(reply));
    if (second) f->fifo[0] = f->fifo[7] = 0xa0;
    return f->scenario == TX_ERROR ? -1 : 0;
}

int t3p_pd_rx_read(struct t3p_meter_bus *bus, uint8_t reg, uint8_t *out, unsigned count)
{
    struct fixture *f = bus->context;
    if (reg == 0x43) {
        assert(f->polls >= 2 && f->fifo_pos + count <= f->fifo_len);
        memcpy(out, f->fifo + f->fifo_pos, count);
        f->fifo_pos += count;
        return 0;
    }
    memcpy(out, f->regs + reg, count);
    if (reg == 0x40) {
        assert(count == 1);
        uint8_t sw = f->regs[0x02];
        unsigned mv = 0;
        if (sw & 0xc0) {
            assert((sw == 0xc4 || sw == 0xc8) && f->regs[0x06] == 0x2c);
            if (sw == 0xc4) {
                if (f->scenario == REVERSED) mv = 330;
                else if (f->scenario == FAR_SINK) mv = 1980;
                else mv = 3300;
            } else if (f->scenario == REVERSED || f->scenario == NO_RA) {
                mv = 3300;
            } else if (f->scenario == RD_ON_CC2) {
                mv = 1690;
            } else if (f->scenario != SHORT_CC2) {
                mv = 330;
            }
        } else if (sw == 0x0b && f->scenario == SRC_ON_CC2) {
            mv = 450;
        }
        out[0] = mv > (f->regs[0x04] + 1u) * 42u ? 0x20 : 0;
        if (sw == 0xc8 && f->scenario == VBUS_DURING_PROBE) out[0] |= 0x80;
    }
    if (reg == 0x3e && f->regs[0x02] == 0x64) {
        assert(count == 5);
        out[2] = f->scenario == LATE_VBUS ? 0xa0 : 0x20;
        if (f->scenario == OVERCURRENT) out[0] |= 0x80;
        if (f->regs[0x03] == 0xb5 && f->fifo_pos < f->fifo_len) {
            ++f->polls;
            if (f->polls == 1) {
                out[0] |= 0x04; /* The controller received GoodCRC. */
                out[4] |= 0x10; /* CRC event before RX_EMPTY changes */
            }
            if (f->polls >= 2) out[3] &= ~0x20;
        }
    }
    return 0;
}

int32_t meter_pd_record(const uint8_t *frame, uint32_t len, uint32_t mode, uint32_t now)
{
    (void)frame; (void)len; (void)mode; (void)now;
    assert(0 && "Standalone reads use the query parser");
    return -1;
}

int32_t meter_pd_cable_reply(uint32_t query, uint32_t index, const uint8_t *frame, uint32_t len)
{
    const struct step *step = &fixture->steps[fixture->reported];
    assert(len == 7 && query == (uint32_t)step->query && index == step->index);
    assert(frame[0] == (query >= CABLE_SOP2_RESET ? 0xa0 : 0xc0));
    return frame[1] == 0x81 ? CABLE_IGNORE : step->result;
}

void meter_pd_cable_result(uint32_t query, uint32_t index, uint32_t result)
{
    assert(fixture->reported < fixture->sent);
    const struct step *step = &fixture->steps[fixture->reported++];
    assert(query == (uint32_t)step->query && index == step->index && result == (uint32_t)step->result);
}

uint32_t meter_pd_cable_parameters(void)
{
    switch (fixture->scenario) {
    case LEGACY: return 0x40;
    case ACTIVE: return 0x2080 | CABLE_ACTIVE | CABLE_SECOND;
    default: return 0x2080 | CABLE_MODAL;
    }
}

uint32_t meter_pd_cable_svid(uint32_t index)
{
    static const uint16_t svids[] = { 0x1e4e, 0x8087 };
    return index < 2 ? svids[index] : 0;
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
    fixture = &f;
    memcpy(f.regs, baseline, sizeof(baseline));
    struct t3p_meter_bus bus = { .context = &f };
    static const struct {
        enum scenario scenario;
        uint16_t mv;
        enum t3p_pd_state expected;
        unsigned power, send;
        unsigned cc1, cc2;
    } cases[] = {
        { REPLY, 5000, T3P_PD_CABLE_VBUS, 0, 0, 255, 255 },
        { SRC_ON_CC2, 3010, T3P_PD_CABLE_CC_BUSY, 0, 0, 255, 255 },
        { FAR_SINK, 3010, T3P_PD_CABLE_CC_BUSY, 0, 0, 47, 7 },
        { NO_RA, 3010, T3P_PD_CABLE_NO_RA, 0, 0, 64, 64 },
        { REVERSED, 3010, T3P_PD_CABLE_NO_RA, 0, 0, 7, 64 },
        { SHORT_CC2, 3010, T3P_PD_CABLE_NO_RA, 0, 0, 64, 0 },
        { RD_ON_CC2, 3010, T3P_PD_CABLE_NO_RA, 0, 0, 64, 40 },
        { VBUS_DURING_PROBE, 3010, T3P_PD_CABLE_VBUS, 0, 0, 255, 255 },
        { LATE_VBUS, 3010, T3P_PD_CABLE_VBUS, 1, 0, 64, 7 },
        { OVERCURRENT, 3010, T3P_PD_CABLE_VCONN_FAULT, 1, 0, 64, 7 },
        { REPLY, 3010, T3P_PD_CAPTURED, 1, 7, 64, 7 },
        { REPLY, 3010, T3P_PD_CAPTURED, 1, 7, 64, 7 },
        { ACTIVE, 3010, T3P_PD_CAPTURED, 1, 5, 64, 7 },
        { LEGACY, 3010, T3P_PD_CAPTURED, 1, 2, 64, 7 },
        { SILENT, 3010, T3P_PD_TIMEOUT, 1, 2, 64, 7 },
        { ACK_ONLY, 3010, T3P_PD_CABLE_NO_ID, 1, 2, 64, 7 },
        { TX_ERROR, 3010, T3P_PD_BUS_ERROR, 1, 1, 64, 7 },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        f.scenario = cases[i].scenario;
        f.mv = cases[i].mv;
        f.powered = f.sent = f.reported = 0;
        f.steps = passive;
        switch (f.scenario) {
        case ACTIVE: f.steps = active; break;
        case LEGACY: f.steps = legacy; break;
        case SILENT: f.steps = silent; break;
        case ACK_ONLY: f.steps = ack_only; break;
        default: break;
        }
        f.step_count = cases[i].send;
        cc_probe[0] = cc_probe[1] = 255;
        assert(t3p_cable_read(&f.board, &bus) == cases[i].expected);
        assert(f.powered == cases[i].power && f.sent == cases[i].send);
        assert(f.reported == (f.scenario == TX_ERROR ? 0 : f.sent));
        assert(cc_probe[0] == cases[i].cc1 && cc_probe[1] == cases[i].cc2);
        assert(!memcmp(f.regs, baseline, sizeof(baseline)));
    }

    f.scenario = RESTORE_ERROR;
    f.sent = f.reported = 0;
    f.steps = passive;
    f.step_count = sizeof(passive) / sizeof(passive[0]);
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
