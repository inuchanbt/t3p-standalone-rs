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
int32_t meter_pd_record(const uint8_t *frame, uint32_t count)
{
    assert(count == sizeof(offer) && !memcmp(frame, offer, count));
    ++records;
    return 1;
}

int main(void)
{
    struct fake_phy phy = reset_phy();
    struct t3p_meter_bus bus = {.context = &phy};
    struct t3p_pd_capture pd = {0};
    phy.registers[0x03] = 4; /* auto GoodCRC is on */
    t3p_pd_start_capture(&pd, &bus, 0);
    assert(pd.state == T3P_PD_CONFIG && phy.writes == 0);

    phy = reset_phy();
    pd = (struct t3p_pd_capture){0};
    t3p_pd_start_capture(&pd, &bus, 0);
    assert(pd.state == T3P_PD_LISTENING && pd.cc == 1);
    assert(phy.registers[0x02] == 7 && phy.registers[0x0b] == 7);
    t3p_pd_start_capture(&pd, &bus, 500);
    assert(pd.state == T3P_PD_LISTENING && pd.cc == 2);
    assert(phy.registers[0x02] == 0x0b && phy.registers[0x0b] == 7);

    phy.registers[0x42] = 0x10; /* completion arrived just after STATUS1 */
    t3p_pd_poll_capture(&pd, &bus, 505);
    assert(pd.state == T3P_PD_LISTENING && phy.consumed == 0);
    phy.registers[0x41] = 0x08;
    t3p_pd_poll_capture(&pd, &bus, 510);
    assert(pd.state == T3P_PD_CAPTURED && records == 1);
    assert(phy.registers[0x02] == 3 && phy.registers[0x0b] == 1);

    t3p_pd_start_capture(&pd, &bus, 1000);
    t3p_pd_poll_capture(&pd, &bus, 11000);
    assert(pd.state == T3P_PD_TIMEOUT);
    assert(phy.registers[0x02] == 3 && phy.registers[0x0b] == 1);
    return 0;
}
