/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_charger.h"
#include "../native/t3p_pd.h"
#include "../native/t3p_rd_test.h"

enum scenario {
    COMPLETE, NO_EXT, EXT_REPLY_ONLY, EXT_RETRYFAIL, EXT_ACK_ONLY, EXT_WRONG_ACK,
    NO_INITIAL_ACK, VBUS_PRESENT, VBUS_DROP, SINK_TX_NG, PD2,
    EXT_UNSUPPORTED, EXT_REJECTED, EXT_WAIT, ALL_UNSUPPORTED, STATUS_RESET,
};

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
static const uint8_t query_ack[] = {0xe0, 0xa1, 0x03, 0, 0, 0, 0};
static const uint8_t extended[35] = {0xe0, 0xa1, 0xf7, 0x19, 0x80};
static const uint8_t status_reply[19] = {0xe0, 0xa2, 0xb9, 0x07, 0x80};
static const uint8_t info_reply[15] = {0xe0, 0xab, 0x2b};
static const uint8_t revision_reply[11] = {0xe0, 0xac, 0x1d};

struct fixture {
    struct t3p_bsp board;
    enum scenario scenario;
    uint8_t regs[sizeof(stock)];
    const uint8_t *fifo;
    unsigned left, polls, sent, detached_at, post_contract_samples;
    uint8_t ack[7];
};

static unsigned phase_seen, vbus, query_states[4];

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
    if (f->scenario == VBUS_DROP && phase_seen == 3 && f->post_contract_samples++)
        *mv = 3500;
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
        out[2] = 0x03; /* SinkTxOK with the PHY's VBUSOK bit clear. */
        if (phase_seen == 3 && (f->scenario == SINK_TX_NG || f->scenario == VBUS_DROP))
            out[2] = 0x02; /* SinkTxNG after PS_RDY */
        out[3] = 0x28; /* RX and TX empty */
        if (f->left && ++f->polls == 1) {
            if (f->sent == 1 || (f->sent >= 2 && f->scenario != EXT_REPLY_ONLY &&
                f->scenario != EXT_RETRYFAIL && f->scenario != EXT_WRONG_ACK))
                out[0] = 0x04; /* TXSENT */
            if (f->sent == 2 && f->scenario == EXT_RETRYFAIL) out[0] = 0x10;
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
        assert(phase_seen == 1 && header == (f->scenario == PD2 ? 0x1042 : 0x1082));
        assert(f->regs[0x09] == (f->scenario == PD2 ? 0x07 : 0x05));
        assert(length == sizeof(rdo));
        assert(!memcmp(payload, rdo, sizeof(rdo)));
        queue(f, contract, sizeof(contract));
    } else {
        static const uint16_t headers[] = {0x0291, 0x0492, 0x0697, 0x0898};
        static const uint8_t *const replies[] = {extended, status_reply, info_reply, revision_reply};
        static const unsigned lengths[] = {sizeof(extended), sizeof(status_reply),
                                           sizeof(info_reply), sizeof(revision_reply)};
        unsigned query = f->sent - 2;
        assert(query < 4 && header == headers[query] && !length);
        assert(phase_seen >= 3 && f->scenario != PD2);
        if (f->scenario == STATUS_RESET && query == 1) {
            memcpy(f->ack, query_ack, sizeof(query_ack));
            f->ack[1] = 0xad; /* Soft Reset */
            queue(f, f->ack, sizeof(f->ack));
        } else if (f->scenario == ALL_UNSUPPORTED || (!query &&
                   (f->scenario == EXT_UNSUPPORTED || f->scenario == EXT_REJECTED ||
                    f->scenario == EXT_WAIT))) {
            memcpy(f->ack, query_ack, sizeof(query_ack));
            f->ack[1] = f->scenario == EXT_REJECTED ? 0xa4 :
                        f->scenario == EXT_WAIT ? 0xac : 0xb0;
            queue(f, f->ack, sizeof(f->ack));
        } else if (!query && (f->scenario == NO_EXT || f->scenario == EXT_WRONG_ACK ||
                             f->scenario == EXT_ACK_ONLY)) {
            queue(f, f->scenario == EXT_ACK_ONLY ? query_ack : contract, sizeof(query_ack));
        } else {
            queue(f, replies[query], lengths[query]);
        }
    }
    return 0;
}

uint32_t meter_charger_frame(const uint8_t *frame, uint32_t length, uint32_t phase)
{
    static const uint8_t expected[] = {0xa1, 0xa3, 0xa6, 0xa1, 0xa2, 0xab, 0xac};
    static const unsigned lengths[] = {11, 7, 7, 35, 19, 15, 11};
    assert(frame[0] == 0xe0 && phase <= 6);
    if (phase >= 3 && frame[1] == 0xad) return 5;
    if (phase >= 3 && frame[1] == 0xa4) return 2;
    if (phase >= 3 && frame[1] == 0xac && length == 7) return 3;
    if (phase >= 3 && frame[1] == 0xb0) return 4;
    if (phase && frame[1] == 0xa1 && !(frame[2] & 0x80)) {
        assert(length == sizeof(query_ack));
        return phase >= 3 && (frame[2] >> 1) == phase - 2 ? 7 : 0;
    }
    assert(length == lengths[phase] && frame[1] == expected[phase]);
    assert(phase >= phase_seen);
    phase_seen = phase + 1;
    return 1;
}

static unsigned pd2;
uint32_t meter_charger_request(void) { assert(phase_seen == 1); return pd2 ? 0x72 : 0xb2; }
void meter_charger_vbus(uint32_t mv) { vbus = mv; }
void meter_charger_query_status(uint32_t phase, uint32_t state)
{
    assert(phase >= 3 && phase <= 6);
    assert(query_states[phase - 3] == ~0u);
    query_states[phase - 3] = state;
}

static void restart(struct fixture *f, enum scenario scenario)
{
    phase_seen = f->sent = f->post_contract_samples = vbus = 0;
    f->scenario = scenario;
    pd2 = scenario == PD2;
    for (unsigned i = 0; i < 4; ++i) query_states[i] = ~0u;
}

int main(void)
{
    struct fixture f = {0};
    struct t3p_meter_bus bus = {.context = &f};
    memcpy(f.regs, stock, sizeof(stock));

    for (unsigned i = 0; i < 2; ++i) {
        restart(&f, COMPLETE);
        assert(t3p_charger_arm(&bus) == CHARGER_ARMED);
        assert(t3p_charger_scan(&f.board, &bus) == CHARGER_COMPLETE);
        assert(phase_seen == 7 && f.sent == 5 && vbus == 5085);
        for (unsigned q = 0; q < 4; ++q) assert(query_states[q] == CHARGER_READY);
        assert(!t3p_pd_rd_baseline(&bus));
    }
    static const struct {
        enum scenario scenario;
        enum t3p_charger_state state;
        unsigned phases, sent, first_query;
    } query_cases[] = {
        {EXT_REPLY_ONLY, CHARGER_COMPLETE, 7, 5, CHARGER_READY},
        {EXT_RETRYFAIL, CHARGER_COMPLETE, 7, 5, CHARGER_READY},
        {EXT_ACK_ONLY, CHARGER_PARTIAL, 7, 5, CHARGER_NO_EXT},
        {EXT_WRONG_ACK, CHARGER_NO_GOODCRC, 3, 2, CHARGER_NO_GOODCRC},
        {NO_EXT, CHARGER_PARTIAL, 7, 5, CHARGER_NO_EXT},
        {EXT_UNSUPPORTED, CHARGER_PARTIAL, 7, 5, CHARGER_UNSUPPORTED},
        {EXT_REJECTED, CHARGER_PARTIAL, 7, 5, CHARGER_REJECTED},
        {EXT_WAIT, CHARGER_PARTIAL, 7, 5, CHARGER_WAIT},
        {ALL_UNSUPPORTED, CHARGER_PARTIAL, 3, 5, CHARGER_UNSUPPORTED},
        {STATUS_RESET, CHARGER_RESET, 4, 3, CHARGER_READY},
    };
    for (unsigned i = 0; i < sizeof(query_cases) / sizeof(query_cases[0]); ++i) {
        restart(&f, query_cases[i].scenario);
        assert(t3p_charger_arm(&bus) == CHARGER_ARMED);
        assert(t3p_charger_scan(&f.board, &bus) == query_cases[i].state);
        assert(phase_seen == query_cases[i].phases && f.sent == query_cases[i].sent);
        assert(query_states[0] == query_cases[i].first_query && !t3p_pd_rd_baseline(&bus));
        if (f.sent == 5) {
            for (unsigned q = 1; q < 4; ++q)
                assert(query_states[q] == (f.scenario == ALL_UNSUPPORTED ?
                                          CHARGER_UNSUPPORTED : CHARGER_READY));
        }
    }

    restart(&f, PD2);
    assert(t3p_charger_arm(&bus) == CHARGER_ARMED);
    assert(t3p_charger_scan(&f.board, &bus) == CHARGER_PD2);
    assert(phase_seen == 3 && f.sent == 1 && query_states[0] == ~0u);
    assert(!t3p_pd_rd_baseline(&bus));

    restart(&f, VBUS_DROP);
    assert(t3p_charger_arm(&bus) == CHARGER_ARMED);
    assert(t3p_charger_scan(&f.board, &bus) == CHARGER_VBUS_RANGE);
    assert(phase_seen == 3 && f.sent == 1 && vbus == 3500);
    assert(!t3p_pd_rd_baseline(&bus));

    restart(&f, SINK_TX_NG);
    assert(t3p_charger_arm(&bus) == CHARGER_ARMED);
    assert(t3p_charger_scan(&f.board, &bus) == CHARGER_EXT_SKIPPED);
    assert(phase_seen == 3 && f.sent == 1 && vbus == 5085);
    assert(!t3p_pd_rd_baseline(&bus));

    restart(&f, NO_INITIAL_ACK);
    assert(t3p_charger_arm(&bus) == CHARGER_ARMED);
    assert(t3p_charger_scan(&f.board, &bus) == CHARGER_NO_GOODCRC);
    assert(phase_seen == 1 && !f.sent && !t3p_pd_rd_baseline(&bus));

    f.scenario = VBUS_PRESENT;
    assert(t3p_charger_arm(&bus) == CHARGER_VBUS_PRESENT);
    assert(!f.sent && !t3p_pd_rd_baseline(&bus));
    return 0;
}
