/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_meter.h"
#ifdef T3P_PD_CABLE_READ
#include "t3p_pd.h"
#endif
#ifdef T3P_PD_RD_TEST
#include "t3p_rd_test.h"
#endif

#define PB 0xfd3100u
#define TIMER_COUNT 0xfd0604u
#if defined(T3P_PD_PROFILES) || defined(T3P_PD_ACK_TEST)
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
#if defined(T3P_PD_PROFILES) || defined(T3P_PD_ACK_TEST)
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
#if defined(T3P_PD_PROFILES) || defined(T3P_PD_RD_TEST)
static int pd_write_register(struct t3p_meter_bus *b, uint8_t reg, uint8_t value)
{
    int error = start(b) || write_byte(b, 0x44) || write_byte(b, reg) || write_byte(b, value);
    stop(b);
    return error ? -1 : 0;
}
#endif
#if defined(T3P_PD_PROFILES) || defined(T3P_PD_ACK_TEST)
int t3p_pd_rx_read(struct t3p_meter_bus *b, uint8_t reg, uint8_t *data, unsigned count)
{
    return read_register(b, 0x44, reg, data, count);
}
#endif

#ifdef T3P_PD_CABLE_READ
int t3p_pd_cable_write(struct t3p_meter_bus *b, uint8_t reg, uint8_t value)
{
    return pd_write_register(b, reg, value);
}

int t3p_pd_cable_tx(struct t3p_meter_bus *b, unsigned second, uint16_t header,
                   const uint8_t *payload, unsigned length)
{
    if (length > 28 || length != 4u * ((header >> 12) & 7u)) return -1;
    const uint8_t prefix[] = {
        0x12, second ? 0x1b : 0x12, second ? 0x12 : 0x1b, 0x1b, /* SOP' or SOP'' */
        0x80 | (2 + length), (uint8_t)header, (uint8_t)(header >> 8), /* PACKSYM and header */
    };
    static const uint8_t suffix[] = {0xff, 0x14, 0xfe, 0xa1}; /* JAMCRC, EOP, TXOFF, TXON */
    int error = start(b) || write_byte(b, 0x44) || write_byte(b, 0x43);
    for (unsigned i = 0; i < sizeof(prefix) && !error; ++i) error = write_byte(b, prefix[i]);
    for (unsigned i = 0; i < length && !error; ++i) error = write_byte(b, payload[i]);
    for (unsigned i = 0; i < sizeof(suffix) && !error; ++i) error = write_byte(b, suffix[i]);
    stop(b);
    return error ? -1 : 0;
}
#endif

#ifdef T3P_PD_PROFILES
int t3p_pd_rx_config(struct t3p_meter_bus *b, uint8_t reg, uint8_t value)
{
    /* Preserve both Rd bits. Allow only CC1 RX routing, power, RX flush and SOP' RX. */
    if (!((reg == 0x02 && (value == 0x03 || value == 0x07)) ||
          (reg == 0x0b && (value == 0x01 || value == 0x07 || value == 0x09)) ||
          (reg == 0x07 && (value == 0x00 || value == 0x01 || value == 0x04))))
        return -1;
    return pd_write_register(b, reg, value);
}
#endif
#ifdef T3P_PD_RD_TEST
int t3p_pd_rd_baseline(struct t3p_meter_bus *b)
{
    uint8_t registers[9], power, status1;
    if (read_register(b, 0x44, 0x01, registers, sizeof(registers)) ||
        read_register(b, 0x44, 0x0b, &power, 1) ||
        read_register(b, 0x44, 0x41, &status1, 1))
        return -1;

    /* Require the observed startup configuration with TX and auto GoodCRC off. */
    return registers[0] != 0x91 || registers[1] != T3P_RD_DEFAULT || (registers[2] & 7) ||
           (registers[5] & 3) || registers[6] || registers[7] != 0x02 || registers[8] != 0x06 ||
           power != 0x01 || (status1 & 0x0f) != 0x08;
}

int t3p_pd_set_rd(struct t3p_meter_bus *b, uint8_t switches)
{
    uint8_t actual;
    if (switches != T3P_RD_DEFAULT && switches != T3P_RD_CC1_OFF && switches != T3P_RD_CC1_ON)
        return -1;
    return pd_write_register(b, 0x02, switches) ||
           t3p_pd_read_register(b, 0x02, &actual) || actual != switches ? -1 : 0;
}
#endif
#ifdef T3P_PD_ACK_TEST
#define ACK_SWITCHES0 0x02
#define ACK_SWITCHES1 0x03
#define ACK_CONTROL1 0x07
#define ACK_POWER 0x0b
#define ACK_RESET 0x0c
#define ACK_INTERRUPTA 0x3e
#define ACK_RX_FLUSH 0x04
#define ACK_PD_RESET 0x02
#define ACK_SINK_CC1 0x25 /* AUTO_CRC, TXCC1, Rev 2.0, Sink/UFP */

static int write_checked(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value,
                         uint8_t expected)
{
    uint8_t actual;
    return pd_write_register(bus, reg, value) || t3p_pd_rx_read(bus, reg, &actual, 1) ||
           actual != expected ? -1 : 0;
}

int t3p_pd_arm_ack(struct t3p_meter_bus *bus, uint8_t *saved_switches1, unsigned *restore)
{
    uint8_t registers[3], status[5];
    if (t3p_pd_rx_read(bus, ACK_SWITCHES0, registers, sizeof(registers))) return -1;
    /* Verify Rd is still off, TX is disabled and the measurement mux selects CC. */
    if (registers[0] != T3P_RD_CC1_OFF || (registers[1] & 0x0f) || (registers[2] & 0x40))
        return 1;

    *saved_switches1 = registers[1];
    *restore = 1;
    /* Reset the local PD logic on every attach. Keep the oscillator on and
       the receiver off while resetting and flushing RX. */
    if (write_checked(bus, ACK_POWER, 0x09, 0x09) ||
        pd_write_register(bus, ACK_RESET, ACK_PD_RESET) ||
        write_checked(bus, ACK_CONTROL1, ACK_RX_FLUSH, 0) ||
        t3p_pd_rx_read(bus, ACK_INTERRUPTA, status, sizeof(status)))
        return -1;
    if ((status[3] & 0x3f) != 0x28) return 1; /* RX and TX FIFOs empty */
    return write_checked(bus, ACK_POWER, 0x0f, 0x0f) ||
           write_checked(bus, ACK_SWITCHES1, ACK_SINK_CC1, ACK_SINK_CC1) ? -1 : 0;
}

int t3p_pd_disarm_ack(struct t3p_meter_bus *bus, uint8_t saved_switches1)
{
    int tx_failed = write_checked(bus, ACK_SWITCHES1, saved_switches1, saved_switches1) &&
                    write_checked(bus, ACK_SWITCHES1, saved_switches1, saved_switches1);
    int power_failed = write_checked(bus, ACK_POWER, 0x01, 0x01) &&
                       write_checked(bus, ACK_POWER, 0x01, 0x01);
    return tx_failed || power_failed ? -1 : 0;
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
