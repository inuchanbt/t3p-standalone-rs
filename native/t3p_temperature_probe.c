/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_temperature_probe.h"
static struct t3p_temperature_probe_snapshot live,frozen;
static uint32_t attempted;
static unsigned frozen_valid;
void t3p_temperature_probe_init(unsigned timer_ready)
{
    live=(struct t3p_temperature_probe_snapshot){.header={T3P_TEMP_PROBE_MAGIC,2,
        T3P_TEMP_PROBE_WORDS,120,T3P_TEMP_PROBE_COUNT,0,1,0,!!timer_ready,0}};
    attempted=0;frozen_valid=0;
}
void t3p_temperature_probe_poll(struct t3p_bsp *board,struct t3p_meter_bus *bus,
                               uint32_t now,unsigned enabled)
{
    live.header[7]=!!enabled;
    if(!enabled||!live.header[8]||live.header[9]||
       live.header[5]>=T3P_TEMP_PROBE_COUNT||now-attempted<1000)return;
    attempted=now;
    struct t3p_temperature_probe_record record={0};
    record.meta[7]=now;
    (void)t3p_temperature_probe_sample(board,bus,live.header[5],&record);
    record.meta[8]=t3p_bsp_millis(board);
    live.records[live.header[5]]=record;
    if(record.meta[9]==2)live.header[9]=1;
    ++live.header[5];++live.header[6];
}
int t3p_temperature_probe_page(uint32_t page,uint32_t out[10])
{
    if(page>T3P_TEMP_PROBE_WORDS/10)return -1;
    if(!page){frozen=live;frozen_valid=1;}
    if(!frozen_valid)return -1;
    const uint8_t *bytes=(const uint8_t *)&frozen;
    if(page==T3P_TEMP_PROBE_WORDS/10){
        uint32_t crc=~0u;
        for(unsigned i=0;i<sizeof frozen;i++){
            crc^=bytes[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
        }
        for(unsigned i=0;i<10;i++)out[i]=0;
        out[0]=T3P_TEMP_PROBE_MAGIC;out[1]=2;out[2]=T3P_TEMP_PROBE_WORDS;
        out[3]=frozen.header[6];out[4]=~crc;
    }else for(unsigned i=0;i<10;i++){
        unsigned at=(page*10+i)*4;
        out[i]=(uint32_t)bytes[at]|(uint32_t)bytes[at+1]<<8|
            (uint32_t)bytes[at+2]<<16|(uint32_t)bytes[at+3]<<24;
    }
    return 0;
}
