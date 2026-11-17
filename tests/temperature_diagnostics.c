/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>
#include "../native/t3p_temperature.h"
#define main measurement_checks
#include "measurement.c"
#undef main
static unsigned delay_calls, fail_delay;
static int diagnostic_delay(void *ctx)
{ (void)ctx; return ++delay_calls==fail_delay ? -1 : 0; }
static void pins(void *ctx,uint32_t out[14])
{
    struct fake *f=ctx;
    for(unsigned i=0;i<14;i++)out[i]=0;
    out[0]=(f->scl?4u:0u)|(f->sda?2u:0u);
}
static void prepare(struct fake *f,struct t3p_meter_bus *b,struct t3p_temperature_diag *d)
{
    *f=(struct fake){0};*d=(struct t3p_temperature_diag){0};
    *b=bus(f);b->temperature_diag=d;b->temperature_pins=pins;b->delay=diagnostic_delay;
    delay_calls=fail_delay=0;
    answer_byte(f,0x58);
}
struct board_fake {uint32_t pa[13],pb[13],timer;unsigned pa_writes;};
static uint32_t board_read(void *ctx,uint32_t at)
{
    struct board_fake *f=ctx;
    if(at>=0xfd3000u && at<0xfd3034u)return f->pa[(at-0xfd3000u)/4];
    if(at>=0xfd3100u && at<0xfd3134u)return f->pb[(at-0xfd3100u)/4];
    assert(at==0xfd0618u);return f->timer++;
}
static void board_write(void *ctx,uint32_t at,uint32_t value)
{
    struct board_fake *f=ctx;
    if(at>=0xfd3000u&&at<0xfd3034u){++f->pa_writes;f->pa[(at-0xfd3000u)/4]=value;return;}
    assert(at>=0xfd3100u&&at<0xfd3134u);f->pb[(at-0xfd3100u)/4]=value;
}
int main(void)
{
    struct fake f;struct t3p_meter_bus b;struct t3p_temperature_diag d;uint8_t id=0;
    struct t3p_system_stats stats={0};
    prepare(&f,&b,&d);b.system=&stats;
    assert(!t3p_temperature_read(&b,0xd0,&id,1)&&id==0x58);
    assert(d.stage==TEMP_BUS_DONE&&!d.bus_error&&d.ack_seen==0x58&&d.ack_valid==0x58);
    assert(d.transactions==1&&d.reg==0xd0&&!d.direction&&stats.reads==1);
    assert(d.gpio_valid==((1u<<TEMP_PINS_RELEASED)|(1u<<TEMP_PINS_STOPPED)));
    for(unsigned scenario=0;scenario<3;scenario++) {
        prepare(&f,&b,&d);
        unsigned cursor[]={1,2,4}; unsigned stage[]={TEMP_BUS_ADDRESS_W,TEMP_BUS_REGISTER,TEMP_BUS_ADDRESS_R};
        f.reads[cursor[scenario]]=1;
        assert(t3p_temperature_read(&b,0xd0,&id,1));
        assert(d.stage==stage[scenario]&&d.bus_error==TEMP_BUS_NACK&&d.bit_index==8);
        assert(d.pins[TEMP_PINS_FAULT][0]==6); /* Captured before STOP, ACK clock high. */
        assert(f.scl&&f.sda&&d.gpio_valid&(1u<<TEMP_PINS_FAULT));
    }
    prepare(&f,&b,&d);f.reads[0]=0;
    assert(t3p_temperature_read(&b,0xd0,&id,1)&&d.stage==TEMP_BUS_IDLE&&d.bus_error==TEMP_BUS_SDA_LOW);
    prepare(&f,&b,&d);f.reads[3]=0;
    assert(t3p_temperature_read(&b,0xd0,&id,1)&&d.stage==TEMP_BUS_RESTART&&d.bus_error==TEMP_BUS_SDA_LOW);
    prepare(&f,&b,&d);f.stuck=1;
    assert(t3p_temperature_read(&b,0xd0,&id,1)&&d.stage==TEMP_BUS_IDLE&&d.bus_error==TEMP_BUS_SCL_LOW);
    assert(delay_calls<=61); /* Both transfer and best-effort STOP are bounded. */
    unsigned waits[]={1,2,4,22,40,43,61,79};
    unsigned stages[]={TEMP_BUS_IDLE,TEMP_BUS_START,TEMP_BUS_ADDRESS_W,TEMP_BUS_REGISTER,
        TEMP_BUS_RESTART,TEMP_BUS_ADDRESS_R,TEMP_BUS_DATA,TEMP_BUS_STOP};
    for(unsigned i=0;i<8;i++) {
        prepare(&f,&b,&d);fail_delay=waits[i];
        assert(t3p_temperature_read(&b,0xd0,&id,1)&&d.stage==stages[i]);
        assert(i==7 ? d.stop_error==TEMP_BUS_DELAY : d.bus_error==TEMP_BUS_DELAY);
    }
    prepare(&f,&b,&d);f=(struct fake){0};accept_write(&f);
    assert(!t3p_temperature_write(&b,0xf4,0x21)&&d.direction==1&&d.stage==TEMP_BUS_DONE);
    assert(t3p_temperature_write(&b,0xe0,0xb6));
    prepare(&f,&b,&d);
    assert(t3p_temperature_read(&b,0xd0,0,1)&&d.bus_error==TEMP_BUS_ARGUMENT&&!f.clocks);
    struct board_fake hw={0};
    for(unsigned i=0;i<13;i++){hw.pa[i]=0xa5a55a5a;hw.pb[i]=0xa5a55a5a;}
    struct t3p_bsp board={.io={.context=&hw,.read32=board_read,.write32=board_write}};
    t3p_temperature_bus_init(&board,&b);
    assert(!hw.pa_writes && t3p_temperature_diagnostics.bus_port==1);
    assert(!memcmp(t3p_temperature_diagnostics.pins[0],t3p_temperature_diagnostics.pins[1],13*4));
    b.scl(b.context,0);assert((hw.pb[2]&6)==2);
    b.sda(b.context,0);assert((hw.pb[2]&6)==0);
    b.scl(b.context,1);b.sda(b.context,1);assert((hw.pb[2]&6)==6);
    for(unsigned i=0;i<13;i++)assert((hw.pb[i]&~6u)==(0xa5a55a5au&~6u));
    uint32_t out[10];
    assert(t3p_temperature_diag_page(1,out)); /* Require explicit RAM snapshot start. */
    assert(!t3p_temperature_diag_page(0,out)&&out[0]==T3P_TEMP_DIAG_MAGIC);
    uint32_t saved=t3p_temperature_diagnostics.timer_ready;
    t3p_temperature_diagnostics.timer_ready=1234;
    assert(!t3p_temperature_diag_page(2,out)&&out[5]==saved); /* Frozen, no fresh MMIO. */
    assert(!t3p_temperature_diag_page(11,out)&&out[2]==110);
    assert(t3p_temperature_diag_page(12,out));
    return 0;
}
