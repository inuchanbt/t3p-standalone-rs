/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_charger.h"
#include "../native/t3p_pd.h"
#include "../native/t3p_rd_test.h"

enum scenario { COMPLETE, NO_EXT, NO_INITIAL_ACK, VBUS_PRESENT };

static const uint8_t stock[0x44] = {
    [0x02] = T3P_RD_DEFAULT, [0x03] = 0x20, [0x06] = 0x04,
    [0x09] = 0x06, [0x0b] = 0x01,
};
/* FIFO framing only. The Pi32v2 tests check the real PD payloads and CRC. */
static const uint8_t offer[] = {0xe0, 0xa1, 0x11, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t contract[] = {
    0xe0, 0xa1, 0x01, 0, 0, 0, 0, /* GoodCRC */
    0xe0, 0xa3, 0x03, 0, 0, 0, 0, /* Accept */
    0xe0, 0xa6, 0x05, 0, 0, 0, 0, /* PS_RDY */
};
static const uint8_t extended[] = {
    0xe0, 0xa1, 0x03, 0, 0, 0, 0, /* GoodCRC */
    0xe0, 0xa1, 0x8f, 0, 0, 0, 0, /* Extended response */
};

struct fixture {
    struct t3p_bsp board;
    enum scenario scenario;
    uint8_t regs[sizeof(stock)];
    const uint8_t *fifo;
    unsigned left, polls, sent, detached_at;
};

static unsigned phase_seen, vbus;

static void queue(struct fixture *f, const uint8_t *data, unsigned length)
{
    assert(!f->left);
    f->fifo = data;
    f->left = length;
    f->polls = 0;
}

uint32_t t3p_bsp_millis(struct t3p_bsp *board) { return board->milliseconds; }
int t3p_bsp_delay(struct t3p_bsp *board, uint32_t ms) { board->milliseconds += ms; return 0; }
int t3p_meter_identify(struct t3p_meter_bus *bus) { (void)bus; return 0; }

int t3p_meter_sample(struct t3p_meter_bus *bus, uint16_t *mv, int16_t *ma)
{
    struct fixture *f = bus->context;
    *mv = f->scenario == VBUS_PRESENT || f->regs[0x02] == T3P_RD_CC1_ON ? 5085 : 3000;
    *ma = 0;
    return 0;
}

int t3p_pd_rd_baseline(struct t3p_meter_bus *bus)
{
    struct fixture *f = bus->context;
    return memcmp(f->regs, stock, sizeof(stock)) != 0;
}

int t3p_pd_set_rd(struct t3p_meter_bus *bus, uint8_t switches)
{
    struct fixture *f = bus->context;
    if (switches == T3P_RD_CC1_ON) {
        assert(f->regs[0x03] == 0x25 && f->regs[0x09] == 0x05);
        queue(f, offer, sizeof(offer));
    }
    if (switches == T3P_RD_CC1_OFF && f->regs[0x02] == T3P_RD_CC1_ON) {
        assert(f->regs[0x03] == stock[0x03]);
        f->detached_at = f->board.milliseconds;
    }
    if (switches == T3P_RD_DEFAULT) {
        assert(f->board.milliseconds - f->detached_at >= 350);
        assert(f->regs[0x03] == stock[0x03] && f->regs[0x0b] == stock[0x0b]);
    }
    f->regs[0x02] = switches;
    return 0;
}

int t3p_pd_arm_ack(struct t3p_meter_bus *bus, uint8_t *saved, unsigned *restore_phy)
{
    struct fixture *f = bus->context;
    assert(f->regs[0x02] == T3P_RD_CC1_OFF && f->regs[0x03] == stock[0x03]);
    *saved = f->regs[0x03];
    *restore_phy = 1;
    f->regs[0x03] = 0x25;
    f->regs[0x0b] = 0x0f;
    return 0;
}

int t3p_pd_read_register(struct t3p_meter_bus *bus, uint8_t reg, uint8_t *out)
{
    struct fixture *f = bus->context;
    assert(reg == 0x40 && f->regs[0x02] == T3P_RD_CC1_OFF);
    *out = 0;
    return 0;
}

int t3p_pd_cable_write(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value)
{
    struct fixture *f = bus->context;
    if (reg == 0x0c) {
        assert(value == 2 && f->regs[0x0b] == 0x09 && f->regs[0x02] == T3P_RD_CC1_OFF);
        f->left = 0;
        return 0;
    }
    if (reg == 0x06) value &= ~0x40;
    if (reg == 0x07) value &= ~0x04;
    f->regs[reg] = value;
    return 0;
}

int t3p_pd_rx_read(struct t3p_meter_bus *bus, uint8_t reg, uint8_t *out, unsigned length)
{
    struct fixture *f = bus->context;
    if (reg == 0x43) {
        assert(f->polls >= 2 && f->left >= length);
        memcpy(out, f->fifo, length);
        f->fifo += length;
        f->left -= length;
    } else if (reg == 0x3e) {
        assert(length == 5);
        memset(out, 0, length);
        out[2] = 0x83; /* VBUSOK, SinkTxOK */
        out[3] = 0x28; /* RX and TX empty */
        if (f->left && ++f->polls == 1) {
            if (f->sent) out[0] = 0x04; /* GoodCRC for our transmission */
            if (f->scenario != NO_INITIAL_ACK) out[1] = 0x01;
            out[4] = 0x10; /* RX event precedes RX_EMPTY clearing */
        }
        if (f->left && f->polls >= 2) out[3] = 0x08;
    } else {
        assert(length == 1);
        *out = f->regs[reg];
    }
    return 0;
}

int t3p_pd_sop_tx(struct t3p_meter_bus *bus, uint16_t header, const uint8_t *payload, unsigned length)
{
    struct fixture *f = bus->context;
    assert(f->scenario != NO_INITIAL_ACK && f->regs[0x02] == T3P_RD_CC1_ON);
    if (++f->sent == 1) {
        const uint8_t rdo[] = {0x32, 0xc8, 0x00, 0x11};
        assert(phase_seen == 1 && header == 0x1082 && length == sizeof(rdo));
        assert(!memcmp(payload, rdo, sizeof(rdo)));
        queue(f, contract, sizeof(contract));
    } else {
        assert(phase_seen == 3 && f->sent == 2 && header == 0x0291 && !length);
        queue(f, extended, f->scenario == NO_EXT ? 7 : sizeof(extended));
    }
    return 0;
}

uint32_t meter_charger_frame(const uint8_t *frame, uint32_t length, uint32_t phase)
{
    static const uint8_t expected[] = {0xa1, 0xa3, 0xa6, 0xa1};
    assert(frame[0] == 0xe0 && phase <= 3 && length == (phase == 0 ? 11 : 7));
    if (phase && frame[1] == 0xa1 && !(frame[2] & 0x80)) return 0; /* GoodCRC */
    assert(frame[1] == expected[phase] && phase == phase_seen);
    ++phase_seen;
    return 1;
}

uint32_t meter_charger_request(void) { assert(phase_seen == 1); return 0xb2; }
void meter_charger_vbus(uint32_t mv) { vbus = mv; }

int main(void)
{
    struct fixture f = {0};
    struct t3p_meter_bus bus = {.context = &f};
    memcpy(f.regs, stock, sizeof(stock));

    for (unsigned i = 0; i < 2; ++i) {
        phase_seen = f.sent = vbus = 0;
        assert(t3p_charger_arm(&bus) == CHARGER_ARMED);
        assert(t3p_charger_scan(&f.board, &bus) == CHARGER_COMPLETE);
        assert(phase_seen == 4 && f.sent == 2 && vbus == 5085);
        assert(!t3p_pd_rd_baseline(&bus));
    }
    phase_seen = f.sent = 0;
    f.scenario = NO_EXT;
    assert(t3p_charger_arm(&bus) == CHARGER_ARMED);
    assert(t3p_charger_scan(&f.board, &bus) == CHARGER_NO_EXT);
    assert(phase_seen == 3 && f.sent == 2 && !t3p_pd_rd_baseline(&bus));

    phase_seen = f.sent = 0;
    f.scenario = NO_INITIAL_ACK;
    assert(t3p_charger_arm(&bus) == CHARGER_ARMED);
    assert(t3p_charger_scan(&f.board, &bus) == CHARGER_NO_GOODCRC);
    assert(phase_seen == 1 && !f.sent && !t3p_pd_rd_baseline(&bus));

    f.scenario = VBUS_PRESENT;
    assert(t3p_charger_arm(&bus) == CHARGER_VBUS_PRESENT);
    assert(!f.sent && !t3p_pd_rd_baseline(&bus));
    return 0;
}
