/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdlib.h>
#include "../native/t3p_temperature_probe.h"
#define main existing_capture_usb_checks
#include "capture_usb.c"
#undef main
static unsigned samples,fail_restore;
static uint32_t clock_ms;
uint32_t t3p_bsp_millis(struct t3p_bsp *board){(void)board;return clock_ms+17;}
int t3p_temperature_read(struct t3p_meter_bus *b,uint8_t reg,uint8_t *data,unsigned n)
{(void)b;(void)reg;(void)data;(void)n;abort();}
int t3p_temperature_write(struct t3p_meter_bus *b,uint8_t reg,uint8_t value)
{(void)b;(void)reg;(void)value;abort();}
int t3p_temperature_probe_sample(struct t3p_bsp *board,struct t3p_meter_bus *bus,
                                unsigned index,struct t3p_temperature_probe_record *r)
{
    (void)board;(void)bus;
    static const uint32_t targets[8][4]={{1,0x40,0xff,2},{1,0x22,1,1},
        {1,0x76,0xd0,1},{1,0x77,0xd0,1},{0,0x19,0x0f,1},{1,0x20,0,2},
        {0,0x76,0xd0,1},{0,0x77,0xd0,1}};
    assert(index==samples&&index<8);++samples;
    for(unsigned i=0;i<4;i++)r->meta[i]=targets[index][i];
    unsigned success=index<3||index==5,port=targets[index][0];
    uint32_t value=index==0?0x2260u:index==1?0x91u:index==5?0x2103u:0x58u;
    r->meta[4]=success?0:UINT32_MAX;r->meta[5]=success;r->meta[6]=value;
    r->meta[9]=port?0:(fail_restore?2:1);
    r->trace=(struct t3p_temperature_diag){.magic=T3P_TEMP_DIAG_MAGIC,.version=1,
        .attempts=1,.successes=success,.failures=!success,.phase=success?TEMP_DONE:TEMP_ID,
        .result=success?TEMP_OK:TEMP_IO,.transactions=1,.reg=targets[index][2],
        .stage=success?TEMP_BUS_DONE:TEMP_BUS_ADDRESS_W,.bus_error=success?0:TEMP_BUS_NACK,
        .ack_seen=success?0x58u:0,.ack_valid=success?0x58u:8,.valid=success,
        .id=value,.bus_port=port,.scl_mask=port?4u:0x40u,.sda_mask=port?2u:0x20u,
        .init_port=port,.active_port=port,.timer_ready=1};
    return success?0:-1;
}
int main(int argc,char **argv)
{
    (void)existing_capture_usb_checks(1,argv);
    struct t3p_bsp board={0};struct t3p_meter_bus bus={0};uint32_t words[10];
    t3p_temperature_probe_init(0);
    t3p_temperature_probe_poll(&board,&bus,1000,1);assert(!samples);
    assert(t3p_temperature_probe_page(1,words));
    assert(!t3p_temperature_probe_page(0,words)&&!words[5]&&!words[8]);
    t3p_temperature_probe_init(1);
    t3p_temperature_probe_poll(&board,&bus,1000,0);assert(!samples);
    clock_ms=1000;t3p_temperature_probe_poll(&board,&bus,clock_ms,1);assert(samples==1);
    assert(!t3p_temperature_probe_page(0,words)&&words[5]==1&&words[6]==2);
    clock_ms=1500;t3p_temperature_probe_poll(&board,&bus,clock_ms,1);assert(samples==1);
    clock_ms=2000;t3p_temperature_probe_poll(&board,&bus,clock_ms,1);assert(samples==2);
    assert(!t3p_temperature_probe_page(97,words)&&words[3]==2); /* Frozen before sample 2. */
    t3p_temperature_probe_poll(&board,&bus,3000,0);assert(samples==2);
    for(unsigned i=2;i<8;i++){
        clock_ms=(i+2)*1000;t3p_temperature_probe_poll(&board,&bus,clock_ms,1);
    }
    for(unsigned i=0;i<20;i++)t3p_temperature_probe_poll(&board,&bus,20000+i*1000,1);
    assert(samples==8);
    uint8_t q[64]={'T','3','C','Q',5},a[64];q[12]=123;
    FILE *file=argc==2?fopen(argv[1],"wb"):0;
    if(argc==2)assert(file);
    unsigned storage_reads=reads;
    for(unsigned page=0;page<=97;page++){
        q[8]=(uint8_t)page;t3p_capture_usb_request(q,a);
        assert(!a[5]&&a[6]==40&&reads==storage_reads&&samples==8);
        if(!page)assert(a[24]=='T'&&a[25]=='P'&&a[26]=='R'&&a[27]=='2'&&a[44]==8);
        if(file)assert(fwrite(a,1,64,file)==64);
    }
    if(file)fclose(file);
    q[8]=98;t3p_capture_usb_request(q,a);assert(a[5]==1&&samples==8);
    q[8]=0;q[6]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&samples==8);
    /* A restoration mismatch halts the remaining finite matrix. */
    samples=0;fail_restore=1;t3p_temperature_probe_init(1);
    for(unsigned i=0;i<20;i++){
        clock_ms=1000+i*1000;t3p_temperature_probe_poll(&board,&bus,clock_ms,1);
    }
    assert(samples==5);assert(!t3p_temperature_probe_page(0,words)&&words[9]==1&&words[5]==5);
    return 0;
}
