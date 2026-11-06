/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "../native/t3p_meter.h"

struct fake {
    int reads[128];
    unsigned count, cursor;
    int clock_data[128];
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
        assert(f->clocks < 128);
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
static void answer(struct fake *f, uint16_t data)
{
    f->reads[f->count++] = 1; /* idle bus */
    f->reads[f->count++] = 0; /* address ACK */
    f->reads[f->count++] = 0; /* register ACK */
    f->reads[f->count++] = 1; /* repeated START */
    f->reads[f->count++] = 0; /* read address ACK */
    for (int bit = 15; bit >= 0; --bit) f->reads[f->count++] = (data >> bit) & 1;
}
static void answer_byte(struct fake *f, uint8_t data)
{
    f->reads[f->count++] = 1;
    f->reads[f->count++] = 0;
    f->reads[f->count++] = 0;
    f->reads[f->count++] = 1;
    f->reads[f->count++] = 0;
    for (int bit = 7; bit >= 0; --bit) f->reads[f->count++] = (data >> bit) & 1;
}
static struct t3p_meter_bus bus(struct fake *f)
{
    return (struct t3p_meter_bus){f, set_scl, set_sda, get_scl, get_sda, tick};
}
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
    return 0;
}
