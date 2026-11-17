/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native/t3p_temperature.h"
#define main existing_measurement_checks
#include "measurement.c"
#undef main
static unsigned wire_byte(struct fake *f,unsigned start)
{unsigned value=0;for(unsigned i=0;i<8;i++)value=value*2+(unsigned)f->clock_data[start+i];return value;}
int main(void)
{
    struct fake f={0};struct t3p_meter_bus b=bus(&f);
    struct t3p_temperature_diag d={0};struct t3p_system_stats s={0};
    b.temperature_diag=&d;b.system=&s;uint8_t data[2];
    answer_bytes(&f,(uint8_t[]){0x21,0x03},2);
    assert(!t3p_ens220_read(&b,0,data,2)&&data[0]==0x21&&data[1]==3&&s.reads==1);
    assert(wire_byte(&f,1)==0x40&&wire_byte(&f,10)==0&&wire_byte(&f,20)==0x41);
    assert(!f.clock_data[37]&&f.clock_data[46]); /* Master ACK, then final NACK. */
    assert(d.stage==TEMP_BUS_DONE&&d.ack_valid==0x58&&d.ack_seen==0x58);
    f=(struct fake){0};accept_write(&f);
    assert(!t3p_ens220_write(&b,6,0x12)&&s.writes==1);
    assert(wire_byte(&f,1)==0x40&&wire_byte(&f,10)==6&&wire_byte(&f,19)==0x12);
    assert(d.stage==TEMP_BUS_DONE&&d.ack_seen==0x98&&d.ack_valid==0x98);
    f=(struct fake){0};
    for(unsigned reg=0;reg<256;reg++)for(unsigned value=0;value<256;value++){
        unsigned allowed=(reg==6&&(value==8||value==0x80||value==0||value==0x12))||
                         (reg==8&&value==1)||(reg==9&&value==0);
        if(!allowed)assert(t3p_ens220_write(&b,(uint8_t)reg,(uint8_t)value));
    }
    assert(!f.clocks&&s.writes==1);
    assert(t3p_ens220_write(0,6,0x12));
    assert(t3p_ens220_read(&b,0xd0,data,1)&&t3p_ens220_read(&b,0,data,1)&&!f.clocks);
    for(unsigned scenario=0;scenario<3;scenario++){
        f=(struct fake){0};answer_bytes(&f,(uint8_t[]){0x21,3},2);
        unsigned cursor[]={1,2,4};unsigned stage[]={TEMP_BUS_ADDRESS_W,TEMP_BUS_REGISTER,TEMP_BUS_ADDRESS_R};
        f.reads[cursor[scenario]]=1;
        assert(t3p_ens220_read(&b,0,data,2)&&d.stage==stage[scenario]&&d.bus_error==TEMP_BUS_NACK);
    }
    return 0;
}
