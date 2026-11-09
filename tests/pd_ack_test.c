/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_rd_test.h"

struct fixture {
    uint16_t mv;
    uint8_t switches, status[5];
    unsigned writes, fifo_pos;
    int ack_enabled, arm_error, disarm_error;
};

static const uint8_t offer[] = {0xe0, 0xa1, 0x11, 0x2c, 0x91, 0x01, 0, 0, 0, 0, 0};

int t3p_pd_rd_baseline(struct t3p_meter_bus *bus)
{
    struct fixture *f = bus->context;
    return f->switches != T3P_RD_DEFAULT || f->ack_enabled;
}

int t3p_pd_set_rd(struct t3p_meter_bus *bus, uint8_t value)
{
    struct fixture *f = bus->context;
    if (value == T3P_RD_CC1_ON) assert(f->ack_enabled);
    f->switches = value;
    ++f->writes;
    return 0;
}

int t3p_meter_identify(struct t3p_meter_bus *bus) { (void)bus; return 0; }

int t3p_meter_sample(struct t3p_meter_bus *bus, uint16_t *mv, int16_t *ma)
{
    *mv = ((struct fixture *)bus->context)->mv;
    *ma = 0;
    return 0;
}

int t3p_pd_arm_ack(struct t3p_meter_bus *bus, uint8_t *saved, unsigned *restore)
{
    struct fixture *f = bus->context;
    assert(f->switches == T3P_RD_CC1_OFF && f->mv < 4000);
    *saved = 0x20;
    *restore = 1;
    f->ack_enabled = 1;
    f->fifo_pos = 0;
    memset(f->status, 0, sizeof(f->status));
    f->status[3] = 0x28;
    return f->arm_error;
}

int t3p_pd_disarm_ack(struct t3p_meter_bus *bus, uint8_t saved)
{
    struct fixture *f = bus->context;
    assert(f->switches != T3P_RD_DEFAULT && f->ack_enabled && saved == 0x20);
    if (f->disarm_error) return -1;
    f->ack_enabled = 0;
    return 0;
}

int t3p_pd_rx_read(struct t3p_meter_bus *bus, uint8_t reg, uint8_t *out, unsigned count)
{
    struct fixture *f = bus->context;
    assert(f->switches == T3P_RD_CC1_ON && f->ack_enabled);
    if (reg == 0x3e) {
        assert(count == sizeof(f->status));
        memcpy(out, f->status, count);
        f->status[1] = f->status[4] = 0;
        return 0;
    }
    assert(reg == 0x43 && f->fifo_pos + count <= sizeof(offer));
    memcpy(out, offer + f->fifo_pos, count);
    f->fifo_pos += count;
    if (f->fifo_pos == sizeof(offer)) f->status[3] |= 0x20;
    return 0;
}

int32_t meter_pd_ack_pdo_count(const uint8_t *record, uint32_t length)
{
    assert(length == sizeof(offer) && !memcmp(record, offer, length));
    return 1;
}

int main(void)
{
    struct fixture f = { .mv = 5000, .switches = T3P_RD_DEFAULT };
    struct t3p_meter_bus bus = { .context = &f };
    struct t3p_rd_test test = {0};

    t3p_rd_test_step(&test, &bus, 0);
    assert(test.state == T3P_RD_VBUS_PRESENT && f.writes == 0 && !f.ack_enabled);
    f.mv = 3010;
    t3p_rd_test_step(&test, &bus, 100);
    f.mv = 5000;
    t3p_rd_test_step(&test, &bus, 200);
    assert(test.state == T3P_RD_VBUS_PRESENT && f.switches == T3P_RD_DEFAULT && !f.ack_enabled);

    f.mv = 2660;
    t3p_rd_test_step(&test, &bus, 300);
    assert(test.state == T3P_RD_OFF && !f.ack_enabled);
    t3p_rd_test_step(&test, &bus, 2300);
    assert(test.state == T3P_RD_ON && f.ack_enabled);
    f.status[1] = 1; /* read-clear GoodCRC flag before the RX record is drained */
    t3p_rd_test_poll(&test, &bus, 2305);
    assert(test.goodcrc && test.pdos == 0 && f.fifo_pos == 0);
    f.status[3] = 0x08;
    t3p_rd_test_poll(&test, &bus, 2310);
    assert(test.pdos == 1 && test.state == T3P_RD_ON);
    f.mv = 5085;
    t3p_rd_test_poll(&test, &bus, 2400);
    assert(test.state == T3P_RD_5V_SEEN && test.on_mv == 5085 && test.off_mv == 2660);
    assert(f.switches == T3P_RD_DEFAULT && !f.ack_enabled);

    f.mv = 3010;
    t3p_rd_test_step(&test, &bus, 3000);
    t3p_rd_test_step(&test, &bus, 3100);
    assert(!test.pdos && !test.goodcrc);
    f.mv = 5100;
    f.status[3] = 0x08;
    f.status[4] = 0x10;
    t3p_rd_test_poll(&test, &bus, 3200);
    t3p_rd_test_poll(&test, &bus, 6100);
    assert(test.state == T3P_RD_NO_GOODCRC && f.switches == T3P_RD_DEFAULT && !f.ack_enabled);

    f.mv = 3010;
    f.arm_error = -1;
    t3p_rd_test_step(&test, &bus, 6200);
    t3p_rd_test_step(&test, &bus, 6300);
    assert(test.state == T3P_RD_BUS_ERROR && f.switches == T3P_RD_DEFAULT && !f.ack_enabled);

    f.arm_error = 0;
    t3p_rd_test_step(&test, &bus, 6400);
    t3p_rd_test_step(&test, &bus, 6500);
    f.status[3] = 0x18; /* full FIFO */
    f.disarm_error = -1;
    t3p_rd_test_poll(&test, &bus, 6510);
    assert(test.state == T3P_RD_RESTORE_FAILED && f.switches == T3P_RD_DEFAULT);
    unsigned writes = f.writes;
    t3p_rd_test_step(&test, &bus, 6600);
    assert(f.writes == writes);
    return 0;
}
