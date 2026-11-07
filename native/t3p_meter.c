/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_meter.h"

#define PB 0xfd3100u
#define TIMER_COUNT 0xfd0604u
#ifdef T3P_PD_PROFILES
#define PD_TIMER 0xfd0614u
#endif
#define SCL (1u << 2)
#define SDA (1u << 1)
#define LINES (SCL | SDA)

/* Release high through the stock 10k pull-up; drive only low. */
static void line(struct t3p_bsp *b, uint32_t pin, int high)
{
    uint32_t direction = b->io.read32(b->io.context, PB + 8);
    b->io.write32(b->io.context, PB + 8, high ? direction | pin : direction & ~pin);
}
static void scl(void *ctx, int high) { line(ctx, SCL, high); }
static void sda(void *ctx, int high) { line(ctx, SDA, high); }
static int read_scl(void *ctx)
{
    struct t3p_bsp *b = ctx;
    return !!(b->io.read32(b->io.context, PB) & SCL);
}
static int read_sda(void *ctx)
{
    struct t3p_bsp *b = ctx;
    return !!(b->io.read32(b->io.context, PB) & SDA);
}
static int delay(void *ctx)
{
    struct t3p_bsp *b = ctx;
    uint16_t start = (uint16_t)b->io.read32(b->io.context, TIMER_COUNT);
    for (uint32_t i = 0; i < 2000000u; ++i) {
        uint16_t now = (uint16_t)b->io.read32(b->io.context, TIMER_COUNT);
        if ((uint16_t)(now - start) >= 2) return 0;
    }
    return -1;
}
#ifdef T3P_PD_PROFILES
static int pd_delay(void *ctx)
{
    struct t3p_bsp *b = ctx;
    uint16_t start = (uint16_t)b->io.read32(b->io.context, PD_TIMER + 4);
    for (unsigned i = 0; i < 5000; ++i) {
        uint16_t now = (uint16_t)b->io.read32(b->io.context, PD_TIMER + 4);
        if ((uint16_t)(now - start) >= 120) return 0; /* 5 us at 24 MHz */
    }
    return -1;
}

int t3p_meter_fast_bus(struct t3p_bsp *b, struct t3p_meter_bus *bus)
{
    /* BR35 TIMER1, with the same 24 MHz source as TIMER0, no divider. */
    if (b->io.read32(b->io.context, PD_TIMER) & 3u) return -1;
    b->io.write32(b->io.context, PD_TIMER, 0x4000);
    b->io.write32(b->io.context, PD_TIMER + 4, 0);
    b->io.write32(b->io.context, PD_TIMER + 8, 0xffff);
    b->io.write32(b->io.context, PD_TIMER, (6u << 10) | 1u);
    if (pd_delay(b)) return -1;
    bus->delay = pd_delay;
    return 0;
}
#endif
void t3p_meter_gpio_init(struct t3p_bsp *b, struct t3p_meter_bus *bus)
{
    static const uint8_t offsets[] = {8, 4, 12, 16, 20, 24, 28, 32, 36, 40};
    for (unsigned i = 0; i < sizeof(offsets); ++i) {
        uint32_t address = PB + offsets[i];
        uint32_t value = b->io.read32(b->io.context, address);
        if (i == 0 || i == 2 || i == 4) value |= LINES;
        else value &= ~LINES;
        b->io.write32(b->io.context, address, value);
    }
    *bus = (struct t3p_meter_bus){b, scl, sda, read_scl, read_sda, delay};
}

static int clock_high(struct t3p_meter_bus *b)
{
    b->scl(b->context, 1);
    for (unsigned i = 0; i < 30; ++i) {
        if (b->delay(b->context)) return -1;
        if (b->read_scl(b->context)) return 0;
    }
    return -1;
}
static int start(struct t3p_meter_bus *b)
{
    b->sda(b->context, 1);
    if (clock_high(b) || !b->read_sda(b->context)) return -1;
    b->sda(b->context, 0);
    if (b->delay(b->context)) return -1;
    b->scl(b->context, 0);
    return b->delay(b->context);
}
static void stop(struct t3p_meter_bus *b)
{
    b->scl(b->context, 0);
    b->sda(b->context, 0);
    if (!b->delay(b->context)) (void)clock_high(b);
    b->sda(b->context, 1);
    b->scl(b->context, 1);
}
static int write_byte(struct t3p_meter_bus *b, uint8_t value)
{
    for (unsigned bit = 0; bit < 8; ++bit) {
        b->sda(b->context, !!(value & 0x80));
        if (b->delay(b->context) || clock_high(b)) return -1;
        b->scl(b->context, 0);
        value <<= 1;
    }
    b->sda(b->context, 1);
    if (b->delay(b->context) || clock_high(b)) return -1;
    int nack = b->read_sda(b->context);
    b->scl(b->context, 0);
    return nack ? -1 : 0;
}
static int read_byte(struct t3p_meter_bus *b, uint8_t *out, int acknowledge)
{
    uint8_t value = 0;
    b->sda(b->context, 1);
    for (unsigned bit = 0; bit < 8; ++bit) {
        if (b->delay(b->context) || clock_high(b)) return -1;
        value = (uint8_t)((value << 1) | !!b->read_sda(b->context));
        b->scl(b->context, 0);
    }
    b->sda(b->context, !acknowledge);
    if (b->delay(b->context) || clock_high(b)) return -1;
    b->scl(b->context, 0);
    b->sda(b->context, 1);
    *out = value;
    return 0;
}
static int read_register(struct t3p_meter_bus *b, uint8_t address, uint8_t reg,
                         uint8_t *data, unsigned count)
{
    if (!b || !data || !b->scl || !b->sda || !b->read_scl || !b->read_sda || !b->delay)
        return -1;
    int error = start(b) || write_byte(b, address) || write_byte(b, reg) ||
                start(b) || write_byte(b, address | 1);
    for (unsigned i = 0; i < count && !error; ++i)
        error = read_byte(b, &data[i], i + 1 < count);
    stop(b);
    return error ? -1 : 0;
}
int t3p_meter_read_register(struct t3p_meter_bus *b, uint8_t reg, uint16_t *out)
{
    uint8_t data[2];
    if (!out || read_register(b, 0x80, reg, data, sizeof(data))) return -1;
    *out = (uint16_t)((uint16_t)data[0] << 8 | data[1]);
    return 0;
}
int t3p_pd_read_register(struct t3p_meter_bus *b, uint8_t reg, uint8_t *out)
{
    /* Only device ID, switch, control, power and non-clearing status registers. */
    if (reg != 0x01 && reg != 0x02 && reg != 0x08 && reg != 0x0b && reg != 0x40)
        return -1;
    uint8_t value;
    if (!out || read_register(b, 0x44, reg, &value, 1)) return -1;
    *out = value;
    return 0;
}
#ifdef T3P_PD_PROFILES
int t3p_pd_rx_read(struct t3p_meter_bus *b, uint8_t reg, uint8_t *data, unsigned count)
{
    return read_register(b, 0x44, reg, data, count);
}

int t3p_pd_rx_config(struct t3p_meter_bus *b, uint8_t reg, uint8_t value)
{
    /* Preserve both Rd bits. Allow only CC1 RX routing, power and RX flush. */
    if (!((reg == 0x02 && (value == 0x03 || value == 0x07)) ||
          (reg == 0x0b && (value == 0x01 || value == 0x07 || value == 0x09)) ||
          (reg == 0x07 && value == 0x04)))
        return -1;
    int error = start(b) || write_byte(b, 0x44) || write_byte(b, reg) || write_byte(b, value);
    stop(b);
    return error ? -1 : 0;
}
#endif
int t3p_meter_identify(struct t3p_meter_bus *b)
{
    uint16_t id, config;
    /* Stock 3.0.3 checks die ID 0x2260 before starting measurements. */
    if (t3p_meter_read_register(b, 0xff, &id) || id != 0x2260 ||
        t3p_meter_read_register(b, 0x00, &config) || (config & 7u) != 7u)
        return -1;
    return 0;
}
int t3p_meter_sample(struct t3p_meter_bus *b, uint16_t *mv, int16_t *ma)
{
    uint16_t voltage, shunt;
    if (!mv || !ma || t3p_meter_read_register(b, 0x02, &voltage) ||
        t3p_meter_read_register(b, 0x01, &shunt) || voltage > 0x7fffu)
        return -1;
    /* Stock nominal factors, before per-device calibration. */
    *mv = (uint16_t)((uint32_t)voltage * 16123u / 10000u);
    *ma = (int16_t)((int32_t)(int16_t)shunt / 2);
    return 0;
}
