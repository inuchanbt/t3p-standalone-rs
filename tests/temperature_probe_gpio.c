/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_temperature_probe.h"
struct io_fake{uint32_t pa[13],pb[13],ticks;unsigned writes;int ignore_restore;};
static uint32_t read_io(void *ctx,uint32_t address)
{
    struct io_fake *f=ctx;
    if(address>=0xfd3000u&&address<0xfd3034u)return f->pa[(address-0xfd3000u)/4];
    if(address>=0xfd3100u&&address<0xfd3134u)return f->pb[(address-0xfd3100u)/4];
    if(address==0xfd0614u)return 0x1801;
    assert(address==0xfd0618u);return f->ticks++;
}
static void write_io(void *ctx,uint32_t address,uint32_t value)
{
    struct io_fake *f=ctx;uint32_t *registers,mask;unsigned offset;
    if(address>=0xfd3000u&&address<0xfd3034u){registers=f->pa;mask=0x60;offset=(address-0xfd3000u)/4;}
    else {assert(address>=0xfd3100u&&address<0xfd3134u);registers=f->pb;mask=6;offset=(address-0xfd3100u)/4;}
    assert(offset>0);assert((registers[offset]&~mask)==(value&~mask));
    assert(offset!=11&&offset!=12); /* No SPL/CON or mux/power writes. */
    ++f->writes;
    if(f->ignore_restore&&registers==f->pa&&offset==4&&(value&mask))return;
    registers[offset]=value;
}
static int delay(void *ctx){(void)ctx;return 0;}
static void prepare(struct io_fake *f)
{
    *f=(struct io_fake){0};
    for(unsigned i=1;i<13;i++){f->pa[i]=0xa5a50000u+i;f->pb[i]=0x5a5a0000u+i;}
    f->pa[1]&=~0x60u;f->pa[2]|=0x60u;f->pa[4]|=0x60u;
    f->pb[1]&=~6u;f->pb[2]|=6u;
}
int main(void)
{
    struct io_fake f;struct t3p_bsp board={.io={.context=&f,.read32=read_io,.write32=write_io}};
    struct t3p_system_stats stats={0};
    struct t3p_meter_bus bus={.context=&board,.delay=delay,.system=&stats};
    for(unsigned index=0;index<8;index++){
        prepare(&f);uint32_t before_pa[13],before_pb[13];
        memcpy(before_pa,f.pa,sizeof before_pa);memcpy(before_pb,f.pb,sizeof before_pb);
        struct t3p_temperature_probe_record r={0};uint32_t target[4];
        assert(!t3p_temperature_probe_target(index,target));
        assert(t3p_temperature_probe_sample(&board,&bus,index,&r));
        assert(r.trace.bus_error==TEMP_BUS_SCL_LOW&&r.trace.stage==TEMP_BUS_IDLE&&!r.meta[5]);
        assert(!memcmp(r.meta,target,4*4)&&stats.reads==index+1&&!stats.writes);
        assert(!memcmp(before_pa,f.pa,sizeof before_pa)&&!memcmp(before_pb,f.pb,sizeof before_pb));
        if(!target[0]){
            assert(r.meta[9]==1&&(r.trace.pins[TEMP_PINS_INIT][4]&0x60)==0);
            assert((r.trace.pins[TEMP_PINS_INIT][5]&0x60)==0x60);
            assert((r.trace.pins[TEMP_PINS_INIT][1]&0x60)==0);
        }else assert(!r.meta[9]);
    }
    prepare(&f);f.pa[0]=0x40;struct t3p_temperature_probe_record r={0};
    assert(t3p_temperature_probe_sample(&board,&bus,7,&r)&&r.trace.bus_error==TEMP_BUS_SDA_LOW&&r.meta[9]==1);
    prepare(&f);f.ignore_restore=1;r=(struct t3p_temperature_probe_record){0};
    assert(t3p_temperature_probe_sample(&board,&bus,7,&r)&&r.meta[9]==2);
    unsigned writes=f.writes;
    assert(t3p_temperature_probe_sample(&board,&bus,8,&r)&&f.writes==writes);
    assert(t3p_temperature_probe_target(8,(uint32_t[4]){0}));
    return 0;
}
