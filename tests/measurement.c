/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "../native/t3p_meter.h"
#ifdef T3P_PD_PROFILES
#include "../native/t3p_pd.h"
#endif
#ifdef T3P_PD_RD_TEST
#include "../native/t3p_rd_test.h"
#endif

struct fake {
    int reads[256];
    unsigned count, cursor;
    int clock_data[512];
    unsigned clocks;
    int scl, sda, stuck, delay_error;
};
struct gpio_fake { uint32_t regs[16]; };
static uint32_t gpio_read(void *ctx, uint32_t address)
{
    struct gpio_fake *g = ctx;
    assert(address >= 0xfd3100u && address < 0xfd3140u);
    return g->regs[(address - 0xfd3100u) / 4];
}
static void gpio_write(void *ctx, uint32_t address, uint32_t value)
{
    struct gpio_fake *g = ctx;
    assert(address >= 0xfd3100u && address < 0xfd3140u);
    g->regs[(address - 0xfd3100u) / 4] = value;
}
static void set_scl(void *ctx, int high)
{
    struct fake *f = ctx;
    f->scl = high;
    if (high) {
        assert(f->clocks < sizeof(f->clock_data) / sizeof(f->clock_data[0]));
        f->clock_data[f->clocks++] = f->sda;
    }
}
static void set_sda(void *ctx, int high) { ((struct fake *)ctx)->sda = high; }
static int get_scl(void *ctx) { struct fake *f = ctx; return !f->stuck && f->scl; }
static int get_sda(void *ctx)
{
    struct fake *f = ctx;
    assert(f->cursor < f->count);
    return f->reads[f->cursor++];
}
static int tick(void *ctx) { return ((struct fake *)ctx)->delay_error; }
static void answer_bytes(struct fake *f, const uint8_t *data, unsigned count)
{
    f->reads[f->count++] = 1; /* idle bus */
    f->reads[f->count++] = 0; /* address ACK */
    f->reads[f->count++] = 0; /* register ACK */
    f->reads[f->count++] = 1; /* repeated START */
    f->reads[f->count++] = 0; /* read address ACK */
    for (unsigned i = 0; i < count; ++i)
        for (int bit = 7; bit >= 0; --bit) f->reads[f->count++] = (data[i] >> bit) & 1;
}
static void answer(struct fake *f, uint16_t data)
{
    const uint8_t bytes[] = { data >> 8, data & 0xff };
    answer_bytes(f, bytes, sizeof(bytes));
}
static void answer_byte(struct fake *f, uint8_t data)
{
    answer_bytes(f, &data, 1);
}
#if defined(T3P_PD_PROFILES) || defined(T3P_PD_RD_TEST)
static void accept_write(struct fake *f)
{
    f->reads[f->count++] = 1;
    f->reads[f->count++] = 0;
    f->reads[f->count++] = 0;
    f->reads[f->count++] = 0;
}
#endif
static struct t3p_meter_bus bus(struct fake *f)
{
    return (struct t3p_meter_bus){f, set_scl, set_sda, get_scl, get_sda, tick};
}
#ifdef T3P_PD_ACK_TEST
static void test_pd_ack_reset(void)
{
    struct fake f = {0};
    struct t3p_meter_bus b = bus(&f);
    const uint8_t idle_switches[] = { T3P_RD_CC1_OFF, 0x20, 0 };
    const uint8_t empty_fifos[] = { 0, 0, 0, 0x28, 0 };
    answer_bytes(&f, idle_switches, sizeof(idle_switches));
    accept_write(&f);      /* oscillator on, receiver off */
    answer_byte(&f, 0x09);
    accept_write(&f);      /* PD_RESET */
    accept_write(&f);      /* RX_FLUSH */
    answer_byte(&f, 0);
    answer_bytes(&f, empty_fifos, sizeof(empty_fifos));
    accept_write(&f);      /* power up the receiver */
    answer_byte(&f, 0x0f);
    accept_write(&f);      /* enable auto GoodCRC on CC1 */
    answer_byte(&f, 0x25);

    uint8_t saved = 0;
    unsigned restore = 0;
    assert(!t3p_pd_arm_ack(&b, &saved, &restore));
    assert(saved == 0x20 && restore && f.cursor == f.count && f.sda && f.scl);

    /* Count releases of SCL for the three-byte read and the POWER write/read. */
    unsigned reset_start = (31 + 3 * 9) + 30 + (31 + 9);
    const uint8_t reset_write[] = { 0x44, 0x0c, 0x02 };
    for (unsigned i = 0; i < sizeof(reset_write); ++i)
        for (unsigned bit = 0; bit < 8; ++bit)
            assert(f.clock_data[reset_start + 1 + i * 9 + bit] ==
                   ((reset_write[i] >> (7 - bit)) & 1));
}
#endif
int main(void)
{
    struct gpio_fake gpio;
    for (unsigned i = 0; i < 16; ++i) gpio.regs[i] = 0xa5a5a5a5u;
    struct t3p_bsp board = { .io = { .context = &gpio, .read32 = gpio_read, .write32 = gpio_write } };
    struct t3p_meter_bus pins;
    t3p_meter_gpio_init(&board, &pins);
    for (unsigned i = 0; i < 16; ++i)
        assert((gpio.regs[i] & ~6u) == (0xa5a5a5a5u & ~6u));
    assert((gpio.regs[1] & 6u) == 0); /* output latch remains low */
    assert((gpio.regs[2] & 6u) == 6u); /* both released */
    assert((gpio.regs[3] & 6u) == 6u); /* digital inputs enabled */
    assert((gpio.regs[5] & 6u) == 6u); /* pull-ups enabled */
    pins.sda(pins.context, 0);
    assert((gpio.regs[2] & 6u) == 4u && (gpio.regs[1] & 6u) == 0);
    pins.sda(pins.context, 1);
    assert((gpio.regs[2] & 6u) == 6u);

    struct fake f = {0};
    struct t3p_meter_bus b = bus(&f);
    answer(&f, 0x2260);
    answer(&f, 0x4127);
    assert(!t3p_meter_identify(&b));
    assert(f.cursor == f.count && f.sda && f.scl);

    f = (struct fake){0};
    answer(&f, 3101);
    answer(&f, (uint16_t)-2000);
    uint16_t mv = 0;
    int16_t ma = 0;
    assert(!t3p_meter_sample(&b, &mv, &ma));
    assert(mv == 4999 && ma == -1000);
    /* START, 0x80, register 0x02, repeated START, 0x81. */
    static const int address_write[] = {1,0,0,0,0,0,0,0};
    static const int register_two[] = {0,0,0,0,0,0,1,0};
    static const int address_read[] = {1,0,0,0,0,0,0,1};
    for (unsigned i = 0; i < 8; ++i) {
        assert(f.clock_data[1 + i] == address_write[i]);
        assert(f.clock_data[10 + i] == register_two[i]);
        assert(f.clock_data[20 + i] == address_read[i]);
    }

    f = (struct fake){0};
    answer(&f, 0x1234);
    assert(t3p_meter_identify(&b));
    assert(f.sda && f.scl);

    f = (struct fake){0};
    answer(&f, 0x2260);
    answer(&f, 0x4120);
    assert(t3p_meter_identify(&b));

    f = (struct fake){0};
    f.reads[f.count++] = 1;
    f.reads[f.count++] = 1; /* address NACK */
    mv = 1234;
    assert(t3p_meter_read_register(&b, 2, &mv));
    assert(mv == 1234 && f.sda && f.scl);

    f = (struct fake){.stuck = 1};
    assert(t3p_meter_read_register(&b, 2, &mv));
    assert(f.sda && f.scl);

    f = (struct fake){0};
    answer_byte(&f, 0x91);
    uint8_t id = 0;
    assert(!t3p_pd_read_register(&b, 1, &id));
    assert(id == 0x91 && f.cursor == f.count && f.sda && f.scl);
    static const int pd_write[] = {0,1,0,0,0,1,0,0};
    static const int pd_read[] = {0,1,0,0,0,1,0,1};
    for (unsigned i = 0; i < 8; ++i) {
        assert(f.clock_data[1 + i] == pd_write[i]);
        assert(f.clock_data[20 + i] == pd_read[i]);
    }
    f = (struct fake){0};
    assert(t3p_pd_read_register(&b, 0x42, &id)); /* interrupt is read-clear */
    assert(f.clocks == 0);
#ifdef T3P_PD_PROFILES
    assert(t3p_pd_rx_config(&b, 0x02, 0x04)); /* would drop both Rd */
    assert(t3p_pd_rx_config(&b, 0x02, 0x0b)); /* unsupported CC2 receive */
    assert(t3p_pd_rx_config(&b, 0x03, 0x04)); /* automatic GoodCRC */
    assert(t3p_pd_rx_config(&b, 0x07, 0x02)); /* SOP'' receive */
    assert(t3p_pd_rx_config(&b, 0x07, 0x10)); /* BIST Mode 2 transmission */
    assert(t3p_pd_rx_config(&b, 0x43, 0xa1)); /* transmit token */
    assert(f.clocks == 0);
    accept_write(&f);
    assert(!t3p_pd_rx_config(&b, 0x02, 0x07)); /* both Rd and MEAS_CC1 */
    assert(f.cursor == f.count && f.sda && f.scl);
    static const int select_cc1[] = {0,0,0,0,0,1,1,1};
    for (unsigned i = 0; i < 8; ++i) {
        assert(f.clock_data[1 + i] == pd_write[i]);
        assert(f.clock_data[10 + i] == register_two[i]);
        assert(f.clock_data[19 + i] == select_cc1[i]);
    }
#endif
#ifdef T3P_PD_RD_TEST
    f = (struct fake){0};
    assert(t3p_pd_set_rd(&b, 0x25)); /* VCONN */
    assert(f.clocks == 0);
    accept_write(&f);
    answer_byte(&f, T3P_RD_CC1_ON);
    assert(!t3p_pd_set_rd(&b, T3P_RD_CC1_ON));
    assert(f.cursor == f.count && f.sda && f.scl);
    static const int cc1_rd_on[] = {0,0,0,0,0,1,0,1};
    for (unsigned i = 0; i < 8; ++i) {
        assert(f.clock_data[1 + i] == pd_write[i]);
        assert(f.clock_data[10 + i] == register_two[i]);
        assert(f.clock_data[19 + i] == cc1_rd_on[i]);
    }
#endif
#ifdef T3P_PD_ACK_TEST
    test_pd_ack_reset();
#endif
    return 0;
}
