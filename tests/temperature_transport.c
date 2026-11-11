/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native/t3p_temperature.h"
#define main measurement_checks
#include "measurement.c"
#undef main
static uint32_t pa_read(void *ctx, uint32_t address)
{
    assert(address >= 0xfd3000u && address < 0xfd3040u);
    return ((struct gpio_fake *)ctx)->regs[(address - 0xfd3000u) / 4];
}
static void pa_write(void *ctx, uint32_t address, uint32_t value)
{
    assert(address >= 0xfd3000u && address < 0xfd3040u);
    ((struct gpio_fake *)ctx)->regs[(address - 0xfd3000u) / 4] = value;
}
int main(void)
{
    struct gpio_fake gpio;
    for(unsigned i=0;i<16;++i) gpio.regs[i]=0xa5a5a5a5;
    struct t3p_bsp board = {.io = {.context=&gpio, .read32=pa_read, .write32=pa_write}};
    struct t3p_meter_bus pins;
    t3p_temperature_bus_init(&board,&pins);
    for(unsigned i=0;i<16;++i) assert((gpio.regs[i]&~0x60u)==(0xa5a5a5a5u&~0x60u));
    assert((gpio.regs[1]&0x60u)==0 && (gpio.regs[2]&0x60u)==0x60);
    pins.scl(pins.context,0); assert((gpio.regs[2]&0x60u)==0x20);
    pins.scl(pins.context,1); assert((gpio.regs[2]&0x60u)==0x60);
    struct fake f={0}; struct t3p_meter_bus b=bus(&f);
    struct t3p_system_stats stats={0}; b.system=&stats;
    answer_byte(&f,0x58); uint8_t id;
    assert(!t3p_temperature_read(&b,0xd0,&id,1) && id==0x58 && stats.reads==1);
    for(unsigned bit=0;bit<8;++bit) assert(f.clock_data[1+bit]==((0xeeu>>(7-bit))&1));
    f=(struct fake){0};
    assert(t3p_temperature_write(&b,0xe0,0xb6)); /* Reset forbidden. */
    assert(t3p_temperature_write(&b,0xf4,0xb3)); /* Pressure/normal mode forbidden. */
    assert(!f.clocks && !stats.writes);
    accept_write(&f);
    assert(!t3p_temperature_write(&b,0xf4,0x21) && stats.writes==1 && f.cursor==f.count);
    f=(struct fake){.stuck=1};
    assert(t3p_temperature_write(&b,0xf4,0x21) && stats.write_errors==1);
    return 0;
}
