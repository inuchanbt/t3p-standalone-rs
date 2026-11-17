/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_temperature.h"

#define D(code) do { if(d){code;} } while(0)
enum { RESET_WAIT=1, WAKE_WAIT, IDLE_WAIT, CONVERSION_WAIT };

/* ENS220 DS-29: TEMP_OUT is unsigned, little endian, 1/128 kelvin.
 * Round once to centidegrees; require the supported die-temperature range. */
int t3p_ens220_centidegrees(uint32_t raw,int32_t *centi)
{
    if(!centi||raw>0xffffu||!raw||raw==0xffffu)return -1;
    int32_t value=(int32_t)((raw*100u+64u)/128u)-27315;
    if(value < -4000 || value > 8500)return -1;
    *centi=value;return 0;
}

/* Keep the primary failed transaction intact. Idle cleanup has its own packed
 * result in cal1 (V3 only): state, bus error, stage, STOP error, one byte each.
 * State 2=verified idle, 3=transport failure, 4=mode readback mismatch. */
static int idle(struct t3p_temperature *t,struct t3p_meter_bus *bus,uint32_t now)
{
    struct t3p_temperature_diag *d=bus->temperature_diag,trace={0};
    struct t3p_meter_bus cleanup=*bus;
    if(d){trace=*d;cleanup.temperature_diag=&trace;}
    uint32_t before=trace.transactions;
    uint8_t mode=0xff;
    int error=t3p_ens220_write(&cleanup,0x06,0);
    if(!error)error=t3p_ens220_read(&cleanup,0x06,&mode,1);
    unsigned state=error?3u:mode?4u:2u;
    D(d->transactions+=trace.transactions-before;
      d->cal1=state|(trace.bus_error<<8)|(trace.stage<<16)|(trace.stop_error<<24));
    t->cleanup_attempted=now;
    if(state==2)t->needs_idle=0;
    return state==2?0:-1;
}

void t3p_ens220_poll(struct t3p_temperature *t,struct t3p_meter_bus *bus,
                    struct t3p_system_stats *s,uint32_t now,unsigned enabled)
{
    struct t3p_temperature_diag *d=bus->temperature_diag;
    if(!enabled){
        if(t->pending)D(d->result=TEMP_CANCELLED;++d->sequence);
        t->pending=0;s->temp_valid=0;D(d->temp_valid=0);
        /* Single-shot is self-terminating. Any HP/configuration cleanup waits
         * for SYSTEM and the normal PD/CHARGER/fast-timer gate to reopen. */
        return;
    }
    if(now-t->updated>2500){s->temp_valid=0;D(d->temp_valid=0);}
    if(!t->pending&&t->needs_idle){
        if(now-t->cleanup_attempted>=1000)(void)idle(t,bus,now);
        return;
    }
    uint8_t bytes[2],status,config;
    unsigned result=TEMP_IO;
    if(!t->pending){
        if(now-t->attempted<1000)return;
        t->attempted=now;
        D(t3p_system_count(&d->attempts);++d->sequence;d->phase=TEMP_ID;
          d->result=TEMP_OK;d->valid=0;d->cal0=0;d->cal1=0;d->attempted_ms=now);
        if(t3p_ens220_read(bus,0x00,bytes,2))goto failed;
        unsigned id=(unsigned)bytes[0]|(unsigned)bytes[1]<<8;
        D(d->id=id;d->valid|=TEMP_VALID_ID);
        if(id!=0x0321u){result=TEMP_BAD_ID;goto failed;}
        D(d->phase=TEMP_CONFIGURE);
        if(!t->reset_done){
            /* Confirmed PART_ID only: one successfully transmitted stock/DS
             * reset per boot, never a guessed address or repeated periodic reset. */
            if(t3p_ens220_write(bus,0x06,0x08))goto failed;
            t->reset_done=1;t->started=now;t->pending=RESET_WAIT;return;
        }
        goto wake;
    }
    if(now-t->started>200){result=TEMP_TIMEOUT;goto failed;}
    if(t->pending==RESET_WAIT){
        if(now-t->started<1)return;
wake:
        t->needs_idle=1;D(d->cal1=1;d->phase=TEMP_CONFIGURE);
        if(t3p_ens220_write(bus,0x06,0x80))goto failed;
        t->started=now;t->pending=WAKE_WAIT;return;
    }
    if(t->pending==WAKE_WAIT){
        if(now-t->started<1)return;
        D(d->phase=TEMP_CONFIGURE);
        if(t3p_ens220_read(bus,0x06,&config,1))goto failed;
        if(config!=0x80){result=TEMP_BAD_CONFIGURATION;goto failed;}
        D(d->cal0|=1);
        if(t3p_ens220_write(bus,0x08,1)||t3p_ens220_read(bus,0x08,&config,1))goto failed;
        if(config!=1){result=TEMP_BAD_CONFIGURATION;goto failed;}
        D(d->cal0|=2);
        if(t3p_ens220_write(bus,0x09,0)||t3p_ens220_read(bus,0x09,&config,1))goto failed;
        if(config){result=TEMP_BAD_CONFIGURATION;goto failed;}
        D(d->cal0|=4);
        if(t3p_ens220_write(bus,0x06,0)||t3p_ens220_read(bus,0x06,&config,1))goto failed;
        if(config){result=TEMP_BAD_CONFIGURATION;goto failed;}
        t->needs_idle=0;D(d->cal0|=8;d->cal1=2);
        t->started=now;t->pending=IDLE_WAIT;return;
    }
    if(t->pending==IDLE_WAIT){
        if(now-t->started<1)return;
        D(d->phase=TEMP_PRE_STATUS);
        /* Reading TEMP_OUT_H clears old TR. Verify it is clear before START,
         * so a previous measurement cannot masquerade as a fresh sample. */
        if(t3p_ens220_read(bus,0x1a,bytes,2)||t3p_ens220_read(bus,0x14,&status,1))goto failed;
        D(d->status=status;d->valid|=TEMP_VALID_STATUS);
        if(status&1u){result=TEMP_BUSY;goto failed;}
        D(d->phase=TEMP_TRIGGER);
        if(t3p_ens220_write(bus,0x06,0x12))goto failed;
        D(d->control=0x12;d->valid|=TEMP_VALID_CONTROL;d->phase=TEMP_WAIT);
        t->started=now;t->pending=CONVERSION_WAIT;return;
    }
    if(now-t->started<20)return;
    D(d->phase=TEMP_POST_STATUS;d->valid&=~TEMP_VALID_STATUS);
    if(t3p_ens220_read(bus,0x14,&status,1))goto failed;
    D(d->status=status;d->valid|=TEMP_VALID_STATUS);
    if(!(status&1u)){D(d->phase=TEMP_WAIT);return;}
    D(d->phase=TEMP_RAW);
    if(t3p_ens220_read(bus,0x1a,bytes,2))goto failed;
    unsigned raw=(unsigned)bytes[0]|(unsigned)bytes[1]<<8;
    D(d->adc=raw;d->valid|=TEMP_VALID_RAW;d->phase=TEMP_COMPENSATE);
    if(t3p_ens220_centidegrees(raw,&s->temp_centi)){result=TEMP_RANGE;goto failed;}
    s->temp_valid=1;t->updated=now;t->pending=0;
    D(t3p_system_count(&d->successes);d->phase=TEMP_DONE;d->result=TEMP_OK;
      ++d->sequence;d->temp_centi=s->temp_centi;d->temp_valid=1;d->updated_ms=now);
    return;
failed:
    t->pending=0;s->temp_valid=0;
    D(t3p_system_count(&d->failures);d->result=result;++d->sequence;d->temp_valid=0);
    if(t->needs_idle)(void)idle(t,bus,now);
}
