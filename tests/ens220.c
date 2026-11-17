/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_temperature.h"

static struct {
    uint8_t regs[32];uint16_t id,raw;
    uint32_t now,triggered;unsigned ops,writes,resets,starts,id_reads;
    unsigned fail_at,fail_idle,mismatch,ready,sticky,running,tr;
} chip;

static int transaction(struct t3p_meter_bus *b,unsigned reg,unsigned write)
{
    ++chip.ops;
    struct t3p_temperature_diag *d=b->temperature_diag;
    if(d){++d->transactions;d->reg=reg;d->direction=write;d->stage=TEMP_BUS_DONE;
        d->bus_error=0;d->stop_error=0;d->ack_seen=write?0x98:0x58;d->ack_valid=d->ack_seen;}
    if(chip.ops==chip.fail_at){
        if(d){d->stage=TEMP_BUS_ADDRESS_W;d->bus_error=TEMP_BUS_NACK;d->ack_seen=0;d->ack_valid=8;}
        return -1;
    }
    return 0;
}
int t3p_ens220_read(struct t3p_meter_bus *b,uint8_t reg,uint8_t *data,unsigned n)
{
    if(transaction(b,reg,0))return -1;
    if(reg==0){assert(n==2);++chip.id_reads;data[0]=(uint8_t)chip.id;data[1]=(uint8_t)(chip.id>>8);}
    else if(reg==0x14){
        assert(n==1);
        if(chip.running&&chip.ready&&chip.now-chip.triggered>=5){chip.tr=1;chip.running=0;chip.regs[6]&=~0x10u;}
        *data=(uint8_t)(chip.tr||chip.sticky);
    }else if(reg==0x1a){assert(n==2);data[0]=(uint8_t)chip.raw;data[1]=(uint8_t)(chip.raw>>8);chip.tr=0;}
    else {assert(n==1&&(reg==6||reg==8||reg==9));*data=chip.regs[reg];if(chip.mismatch==reg)*data^=1;}
    return 0;
}
int t3p_ens220_write(struct t3p_meter_bus *b,uint8_t reg,uint8_t value)
{
    assert(chip.id_reads&&chip.id==0x0321); /* No writes before acquired, verified ID. */
    ++chip.writes;
    if(transaction(b,reg,1))return -1;
    assert((reg==6&&(value==8||value==0x80||value==0||value==0x12))||
        (reg==8&&value==1)||(reg==9&&value==0));
    if(reg==6&&value==0&&chip.fail_idle){--chip.fail_idle;return -1;}
    if(reg==6&&value==8){memset(chip.regs,0,sizeof chip.regs);chip.regs[6]=3;chip.tr=0;chip.running=0;++chip.resets;}
    else {
        if(reg==6&&value==0x12){
            assert(chip.regs[8]==1&&chip.regs[9]==0&&chip.regs[6]==0&&!chip.tr);
            chip.triggered=chip.now;chip.running=1;++chip.starts;
        }
        chip.regs[reg]=value;
    }
    return 0;
}
static void poll(struct t3p_temperature *t,struct t3p_meter_bus *b,struct t3p_system_stats *s,uint32_t now,unsigned enabled)
{chip.now=now;t3p_temperature_poll(t,b,s,now,enabled);}
static void initial(struct t3p_temperature *t,struct t3p_system_stats *s,struct t3p_temperature_diag *d)
{
    *t=(struct t3p_temperature){0};*s=(struct t3p_system_stats){0};
    *d=(struct t3p_temperature_diag){.magic=T3P_TEMP_DIAG_MAGIC,.version=3,.sensor_kind=2,
        .bus_port=1,.scl_mask=4,.sda_mask=2,.active_port=1};
    memset(&chip,0,sizeof chip);chip.id=0x0321;chip.raw=0x9513;chip.ready=1;
}
static void complete(struct t3p_temperature *t,struct t3p_meter_bus *b,struct t3p_system_stats *s,uint32_t first)
{
    for(unsigned i=0;i<4;i++)poll(t,b,s,first+i,1);
    poll(t,b,s,first+23,1);
}
int main(void)
{
    struct t3p_temperature t;struct t3p_system_stats s;struct t3p_temperature_diag d;
    struct t3p_meter_bus b={.temperature_diag=&d};int32_t value;
    unsigned raw[]={0x9513,34963,0x8000,29843,45843};int expected[]={2500,0,-1715,-4000,8500};
    for(unsigned i=0;i<5;i++){assert(!t3p_ens220_centidegrees(raw[i],&value)&&value==expected[i]);}
    for(unsigned i=0;i<4;i++){unsigned invalid[]={0,0xffff,29842,45844};value=123;
        assert(t3p_ens220_centidegrees(invalid[i],&value)&&value==123);}
    assert(t3p_ens220_centidegrees(0x10000,&value)&&t3p_ens220_centidegrees(0x9513,0));
    initial(&t,&s,&d);poll(&t,&b,&s,999,1);assert(!chip.ops);
    poll(&t,&b,&s,1000,1);assert(t.pending==1&&chip.resets==1);
    unsigned before=chip.ops;poll(&t,&b,&s,1000,1);assert(chip.ops==before);
    poll(&t,&b,&s,1001,1);poll(&t,&b,&s,1002,1);poll(&t,&b,&s,1003,1);
    assert(chip.starts==1&&t.pending==4&&!s.temp_valid&&d.cal0==15);
    before=chip.ops;poll(&t,&b,&s,1022,1);assert(chip.ops==before);
    poll(&t,&b,&s,1023,1);assert(s.temp_valid&&s.temp_centi==2500&&d.id==0x0321);
    assert(d.valid==27&&d.successes==1&&d.phase==TEMP_DONE&&d.adc==0x9513&&chip.ops==15);
    /* Later attempts retain the same known configuration, without periodic resets. */
    poll(&t,&b,&s,2000,1);poll(&t,&b,&s,2001,1);poll(&t,&b,&s,2002,1);poll(&t,&b,&s,2022,1);
    assert(d.successes==2&&chip.resets==1&&chip.starts==2);
    for(unsigned failure=1;failure<=15;failure++){
        initial(&t,&s,&d);chip.fail_at=failure;complete(&t,&b,&s,1000);
        assert(!s.temp_valid&&d.failures==1&&d.result==TEMP_IO&&d.bus_error==TEMP_BUS_NACK);
        assert(d.stage==TEMP_BUS_ADDRESS_W); /* Successful cleanup cannot hide the primary failure. */
    }
    initial(&t,&s,&d);chip.id=0x1234;complete(&t,&b,&s,1000);
    assert(d.result==TEMP_BAD_ID&&d.id==0x1234&&!chip.writes&&!s.temp_valid);
    initial(&t,&s,&d);chip.mismatch=8;complete(&t,&b,&s,1000);
    assert(d.result==TEMP_BAD_CONFIGURATION&&!chip.starts&&!s.temp_valid&&!t.needs_idle);
    initial(&t,&s,&d);chip.sticky=1;complete(&t,&b,&s,1000);
    assert(d.result==TEMP_BUSY&&!chip.starts&&!s.temp_valid);
    initial(&t,&s,&d);chip.ready=0;complete(&t,&b,&s,1000);
    assert(t.pending==4&&!s.temp_valid);poll(&t,&b,&s,1203,1);assert(t.pending==4);
    poll(&t,&b,&s,1204,1);assert(d.result==TEMP_TIMEOUT&&!s.temp_valid);
    initial(&t,&s,&d);for(unsigned i=0;i<4;i++)poll(&t,&b,&s,1000+i,1);
    before=chip.ops;poll(&t,&b,&s,5000,1);assert(d.result==TEMP_TIMEOUT&&!s.temp_valid&&chip.ops==before);
    initial(&t,&s,&d);chip.raw=0xffff;complete(&t,&b,&s,1000);
    assert(d.result==TEMP_RANGE&&!s.temp_valid);
    initial(&t,&s,&d);chip.raw=34963;complete(&t,&b,&s,1000);assert(s.temp_valid&&!s.temp_centi);
    poll(&t,&b,&s,4000,1);assert(!s.temp_valid); /* Old samples expire during a new attempt. */
    for(unsigned stage=1;stage<=4;stage++){
        initial(&t,&s,&d);for(unsigned i=0;i<stage;i++)poll(&t,&b,&s,1000+i,1);
        before=chip.ops;poll(&t,&b,&s,1500,0);
        assert(!t.pending&&!s.temp_valid&&chip.ops==before&&d.result==TEMP_CANCELLED);
        if(stage==2){
            assert(t.needs_idle);chip.fail_idle=1;poll(&t,&b,&s,3000,1);
            assert(t.needs_idle&&(d.cal1&255)==3);before=chip.ops;
            poll(&t,&b,&s,3500,1);assert(chip.ops==before);
            poll(&t,&b,&s,4000,1);assert(!t.needs_idle&&(d.cal1&255)==2);
        }
    }
    initial(&t,&s,&d);complete(&t,&b,&s,0xfffffff0u);assert(s.temp_valid&&s.temp_centi==2500);
    return 0;
}
