/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_pd.h"

struct fake_phy {
    uint8_t registers[0x43];
    unsigned writes, consumed;
};

static struct fake_phy reset_phy(void)
{
    return (struct fake_phy){
        .registers = {[0x01] = 0x91, [0x02] = 0x03, [0x08] = 0x02, [0x09] = 0x06,
                      [0x0b] = 0x01, [0x41] = 0x28},
    };
}

static const uint8_t offer[] = {0xe0, 0xa1, 0x11, 0x2c, 0x91, 0x01, 0, 0, 0, 0, 0};

int t3p_pd_rx_read(struct t3p_meter_bus *bus, uint8_t reg, uint8_t *out, unsigned count)
{
    struct fake_phy *phy = bus->context;
    if (reg == 0x43) {
        assert(phy->consumed + count <= sizeof(offer));
        memcpy(out, offer + phy->consumed, count);
        phy->consumed += count;
        if (phy->consumed == sizeof(offer)) phy->registers[0x41] = 0x28;
    } else {
        assert(reg + count <= sizeof(phy->registers));
        memcpy(out, phy->registers + reg, count);
        if (reg == 0x3e) phy->registers[0x42] = 0; /* completion is read-clear */
    }
    return 0;
}

int t3p_pd_rx_config(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value)
{
    struct fake_phy *phy = bus->context;
    ++phy->writes;
    if (reg == 0x07) {
        assert(value == 4 && phy->registers[0x0b] == 9);
        phy->registers[0x41] = 0x28;
        return 0;
    }
    assert(reg == 0x0b || (reg == 0x02 && (value & 0xf3) == 3));
    phy->registers[reg] = value;
    return 0;
}

static unsigned records;
static int32_t record_result = 1;
int32_t meter_pd_record(const uint8_t *frame, uint32_t count, uint32_t mode, uint32_t now)
{
    (void)mode; (void)now;
    assert(count == sizeof(offer) && !memcmp(frame, offer, count));
    ++records;
    return record_result;
}

int main(void)
{
    struct fake_phy phy = reset_phy();
    struct t3p_meter_bus bus = {.context = &phy};
    struct t3p_pd_capture pd = {0};
    phy.registers[0x03] = 4; /* auto GoodCRC is on */
    t3p_pd_start_capture(&pd, &bus, 0, T3P_PD_SPR);
    assert(pd.state == T3P_PD_CONFIG && phy.writes == 0);

    phy = reset_phy();
    pd = (struct t3p_pd_capture){0};
    t3p_pd_start_capture(&pd, &bus, 0, T3P_PD_SPR);
    assert(pd.state == T3P_PD_LISTENING && pd.cc == 1);
    assert(phy.registers[0x02] == 7 && phy.registers[0x0b] == 7);
    t3p_pd_start_capture(&pd, &bus, 500, T3P_PD_SPR);
    assert(pd.state == T3P_PD_LISTENING && pd.cc == 2);
    assert(phy.registers[0x02] == 0x0b && phy.registers[0x0b] == 7);

    phy.registers[0x42] = 0x10; /* completion arrived just after STATUS1 */
    t3p_pd_poll_capture(&pd, &bus, 505);
    assert(pd.state == T3P_PD_LISTENING && phy.consumed == 0);
    phy.registers[0x41] = 0x08;
    t3p_pd_poll_capture(&pd, &bus, 510);
    assert(pd.state == T3P_PD_CAPTURED && records == 1);
    assert(phy.registers[0x02] == 3 && phy.registers[0x0b] == 1);

    t3p_pd_start_capture(&pd, &bus, 1000, T3P_PD_SPR);
    t3p_pd_poll_capture(&pd, &bus, 11000);
    assert(pd.state == T3P_PD_TIMEOUT);
    assert(phy.registers[0x02] == 3 && phy.registers[0x0b] == 1);

    t3p_pd_start_capture(&pd, &bus, 12000, T3P_PD_EPR);
    assert(pd.state == T3P_PD_LISTENING && pd.mode == T3P_PD_EPR && pd.cc == 1);
    phy.consumed = 0;
    phy.registers[0x41] = 0x08;
    phy.registers[0x42] = 0x10;
    record_result = 2; /* only the second EPR chunk was observed */
    t3p_pd_poll_capture(&pd, &bus, 12005);
    assert(pd.state == T3P_PD_LISTENING && pd.partial);
    t3p_pd_poll_capture(&pd, &bus, 22000);
    assert(pd.state == T3P_PD_LISTENING);
    t3p_pd_poll_capture(&pd, &bus, 42000);
    assert(pd.state == T3P_PD_PARTIAL);
    assert(phy.registers[0x02] == 3 && phy.registers[0x0b] == 1);
    t3p_pd_start_capture(&pd, &bus, 43000, T3P_PD_EPR);
    assert(pd.state == T3P_PD_LISTENING && pd.cc == 2);
    phy.consumed = 0;
    phy.registers[0x41] = 0x08;
    phy.registers[0x42] = 0x10;
    record_result = 3; /* EPR chunk 0 */
    t3p_pd_poll_capture(&pd, &bus, 43005);
    assert(pd.state == T3P_PD_LISTENING && !pd.partial);
    phy.consumed = 0;
    phy.registers[0x41] = 0x08;
    phy.registers[0x42] = 0x10;
    record_result = 1; /* EPR chunk 1 completed the offer */
    t3p_pd_poll_capture(&pd, &bus, 43010);
    assert(pd.state == T3P_PD_CAPTURED);
    assert(phy.registers[0x02] == 3 && phy.registers[0x0b] == 1);

    t3p_pd_start_capture(&pd, &bus, 44000, T3P_PD_SNIFF);
    assert(pd.state == T3P_PD_LISTENING && pd.mode == T3P_PD_SNIFF && pd.cc == 2);
    record_result = 0; /* a logged message must not stop the receiver */
    for (unsigned n = 0; n < 3; ++n) {
        phy.consumed = 0;
        phy.registers[0x41] = 0x08;
        phy.registers[0x42] = 0x10;
        t3p_pd_poll_capture(&pd, &bus, 44005 + n * 5);
        assert(pd.state == T3P_PD_LISTENING);
    }
    t3p_pd_poll_capture(&pd, &bus, 74000);
    assert(pd.state == T3P_PD_TIMEOUT);
    assert(phy.registers[0x02] == 3 && phy.registers[0x0b] == 1);
    return 0;
}
