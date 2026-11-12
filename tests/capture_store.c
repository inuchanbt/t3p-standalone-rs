/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A NOR/controller model, independent of the storage driver's implementation.
 * Models command framing, WEL, page boundaries and one-way programming. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define T3P_CAPTURE_STORE_HOST 1
#include "../native/t3p_capture_store.c"
#include "puya_p25q16h_sfdp.h"
static uint8_t flash[0x200000],directory[256],reserved_header[32],sfdp_data[128];
/* Literal independent addresses from BR35 JL_SPI0/JL_PORTF definitions. */
#define MODEL_SPI 0xfe0700u
#define MODEL_GPIO 0xfe1000u
static uint32_t regs_spi[6],gpio_out=1,gpio_dir=0xffffffff,sfc_con=0x5c7e4407;
static uint8_t command,wel,protected_flash,id_bytes[3]={0xef,0x40,0x15};
static uint32_t address_value,count,program_bytes,erases,enables,transfers;
static unsigned page_programs[T3P_CAPTURE_BYTES/256];
static uint8_t data[256];
static struct t3p_bsp model_clock;
static uint32_t ticks;
static unsigned model_port;
static unsigned busy_after_id,identified;
static void finish_command(void)
{
    if(command==6){wel=1;enables++;}
    if(command==0x9f)identified=1;
    if(command==0x20||command==2){
        assert(wel);wel=0;
        unsigned n=command==0x20?4096:program_bytes;
        assert(address_value>=T3P_CAPTURE_START&&address_value+n<=T3P_CAPTURE_START+T3P_CAPTURE_BYTES);
        if(command==0x20){assert(!(address_value&4095));erases++;if(!protected_flash)memset(flash+address_value,255,4096);}
        else {
            assert(program_bytes&&program_bytes<=256&&((address_value&255)+program_bytes<=256));
            page_programs[(address_value-T3P_CAPTURE_START)/256]++;
            if(!protected_flash)for(unsigned i=0;i<program_bytes;i++)flash[address_value+i]&=data[i];
        }
    }
    command=0;count=0;program_bytes=0;address_value=0;
}
uint32_t capture_mmio_read(uint32_t at)
{
    if(at==MODEL_SPI)return (regs_spi[0]|0x8000)&~(busy_after_id==3&&command==0x5a?0x8000u:0u);
    if(at>=MODEL_SPI&&at<=MODEL_SPI+20)return regs_spi[(at-MODEL_SPI)/4];
    if(at==MODEL_GPIO+4)return gpio_out;
    if(at==MODEL_GPIO+8)return gpio_dir;
    if(at==SFC)return sfc_con|(busy_after_id==2&&identified?0x80000000u:0u);
    if(at==SFC+16)return 0xf4005000;
    if(at==SFC+4)return 0x18;
    if(at==SFC+0x24)return 0;
    if(at==0xffe400)return busy_after_id==1&&identified?0x80000000u:0;
    if(at==0xfe1104)return model_port?2:0;
    if(at==0xfd0604){ticks+=1000;return ticks&65535;}
    abort();
}
void capture_mmio_write(uint32_t at,uint32_t value)
{
    if(at==MODEL_GPIO+4){
        unsigned mask=model_port?8:1;
        if(!(gpio_out&mask)&&(value&mask))finish_command();
        gpio_out=value;return;
    }
    if(at==MODEL_GPIO+8){gpio_dir=value;return;}
    if(at==SFC){
        /* The reference resumes bit 1, then bit 0, in separate writes. */
        assert((sfc_con&3)||!(value&1));
        /* Busy is read-only; a read/modify/write cannot latch it forever. */
        sfc_con=(value&~0x80000000u)|0x40000000;return;
    }
    assert(at>=MODEL_SPI&&at<=MODEL_SPI+20);regs_spi[(at-MODEL_SPI)/4]=value;
    if(at!=MODEL_SPI+8)return;
    assert((regs_spi[0]&1)&&!(gpio_out&(model_port?8:1))&&!(sfc_con&3));transfers++;
    unsigned receive=(regs_spi[0]&0x1000)!=0;
    if(!count){assert(!receive);command=(uint8_t)value;}
    else if((command==3||command==0x20||command==2||command==0x5a)&&count<=3){
        assert(!receive);address_value=(address_value<<8)|(value&255);
    }else if(receive){
        if(command==3)regs_spi[2]=flash[address_value++];
        else if(command==0x5a){assert(address_value<sizeof sfdp_data);regs_spi[2]=sfdp_data[address_value++];}
        else if(command==5)regs_spi[2]=wel?2:0;
        else if(command==0x9f){assert(count<=3);regs_spi[2]=id_bytes[count-1];}
        else abort();
    }else if(command==2){assert(program_bytes<256);data[program_bytes++]=(uint8_t)value;}
    else assert(command==0x5a&&count==4);
    count++;
}
uint8_t capture_mapped_read(uint32_t at)
{
    if(at>=0xc000000&&at<0xc000100)return directory[at-0xc000000];
    if(at>=0xc080000&&at<0xc080020)return reserved_header[at-0xc080000];
    assert(at>=0xc000000+T3P_CAPTURE_START-0x5000&&at<0xc000000+T3P_CAPTURE_START-0x5000+T3P_CAPTURE_BYTES);
    /* A fresh update's encrypted FF differs from raw erased FF. */
    return flash[at-0xc000000+0x5000]^0xc3;
}
static void setup(void)
{
    memset(flash,0xa5,sizeof flash);memset(flash+T3P_CAPTURE_START,0x3c,T3P_CAPTURE_BYTES);
    memset(sfdp_data,0,sizeof sfdp_data);memcpy(sfdp_data,"SFDP",4);sfdp_data[5]=1;
    sfdp_data[10]=1;sfdp_data[11]=11;sfdp_data[12]=32;sfdp_data[15]=255;
    sfdp_data[36]=sfdp_data[37]=sfdp_data[38]=255;sfdp_data[60]=12;sfdp_data[61]=0x20;sfdp_data[72]=0x80;
    protected_flash=wel=busy_after_id=identified=0;erases=enables=0;
    id_bytes[0]=0xef;id_bytes[1]=0x40;id_bytes[2]=0x15;
}
static void legacy_setup(void)
{
    setup();id_bytes[0]=0x85;id_bytes[1]=0x60;
    memset(sfdp_data,255,sizeof sfdp_data);
    memcpy(sfdp_data,puya_header,sizeof puya_header);
    memcpy(sfdp_data+0x30,puya_bfpt,sizeof puya_bfpt);
}
int main(int argc,char **argv)
{
    assert(argc==2||argc==3);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(directory,1,256,f)==256);assert(fread(reserved_header,1,32,f)==32);fclose(f);
    setup();assert(!t3p_capture_store_init(&model_clock));assert(!erases&&!enables);
    assert(t3p_capture_jedec==0xef4015&&t3p_capture_sfdp==0x50444653);
    assert(!t3p_capture_diagnostics[0]&&!t3p_capture_diagnostics[1]);
    unsigned before=transfers;uint8_t check[300];
    assert(t3p_capture_store_program(T3P_CAPTURE_BYTES-1,check,2));
    assert(t3p_capture_store_read(UINT32_MAX,check,1));assert(transfers==before);
    assert(!t3p_capture_store_prepare());assert(erases==32);
    for(unsigned i=0;i<T3P_CAPTURE_SLOT;i++)assert(flash[T3P_CAPTURE_START+i]==255);
    assert(flash[T3P_CAPTURE_START+T3P_CAPTURE_SLOT]==0x3c);
    for(unsigned i=0;i<300;i++)check[i]=(uint8_t)(i^0x42);
    assert(!t3p_capture_store_program(190,check,300));
    before=enables;assert(t3p_capture_store_export_read(190,check,1));assert(enables==before);
    assert(!t3p_capture_store_flush());assert(!t3p_capture_store_read(190,check,300));
    for(unsigned i=0;i<300;i++)assert(check[i]==(uint8_t)(i^0x42));
    before=enables;check[0]=255;assert(t3p_capture_store_program(190,check,1));assert(enables==before);
    /* Reboot preserves programmed data and initialization selects another slot. */
    assert(!t3p_capture_store_init(&model_clock));assert(!t3p_capture_store_prepare());assert(erases==64);
    assert(model_clock.milliseconds*375+model_clock.fraction==ticks*64);
    assert(flash[T3P_CAPTURE_START+190]==0x42);
    for(unsigned i=0;i<T3P_CAPTURE_START;i++)assert(flash[i]==0xa5);
    for(unsigned i=T3P_CAPTURE_START+T3P_CAPTURE_BYTES;i<sizeof flash;i++)assert(flash[i]==0xa5);
    /* Unknown SFDP and corrupt partition descriptors never authorize writes. */
    setup();sfdp_data[36]=0;assert(t3p_capture_store_init(&model_clock));assert(t3p_capture_store_prepare());assert(!enables&&!erases);
    setup();directory[8]^=1;assert(t3p_capture_store_init(&model_clock));assert(t3p_capture_store_prepare());assert(!enables&&!erases);directory[8]^=1;
    /* Reproduce the observed V1 fields without treating the absent signature
     * as a recognized chip. Diagnose transfer failure separately from zeros. */
    setup();id_bytes[0]=0x85;id_bytes[1]=0x60;memset(sfdp_data,0,sizeof sfdp_data);
    assert(t3p_capture_store_init(&model_clock)&&t3p_capture_status==2);
    assert(t3p_capture_jedec==0x856015&&!t3p_capture_sfdp&&t3p_capture_diagnostics[0]==4);
    assert(t3p_capture_store_prepare()&&!enables&&!erases);
    for(unsigned fault=1;fault<=3;fault++){
        setup();busy_after_id=fault;assert(t3p_capture_store_init(&model_clock));
        assert(t3p_capture_diagnostics[0]==3&&t3p_capture_diagnostics[1]==fault&&!t3p_capture_sfdp);
        assert(t3p_capture_store_prepare()&&!enables&&!erases);
    }
    /* Accept the exact manufacturer's legacy table; every published byte
     * and the chip ID remain part of the qualification. No boot writes. */
    legacy_setup();assert(!t3p_capture_store_init(&model_clock));
    assert(t3p_capture_diagnostics[2]==9&&t3p_capture_diagnostics[3]==0x30&&!enables&&!erases);
    assert(!t3p_capture_store_prepare()&&erases==32);
    for(unsigned i=0;i<sizeof puya_header+sizeof puya_bfpt;i++){
        legacy_setup();sfdp_data[i<sizeof puya_header?i:i-sizeof puya_header+0x30]^=1;
        assert(t3p_capture_store_init(&model_clock));
        assert(t3p_capture_store_prepare()&&!enables&&!erases);
    }
    legacy_setup();id_bytes[1]^=1;assert(t3p_capture_store_init(&model_clock));
    assert(t3p_capture_store_prepare()&&!enables&&!erases);
    /* Protected flash is not globally unlocked; failed erase is an I/O error. */
    setup();assert(!t3p_capture_store_init(&model_clock));protected_flash=1;
    assert(t3p_capture_store_prepare());assert(t3p_capture_status==3&&erases==1);
    assert(flash[T3P_CAPTURE_START]==0x3c);
    model_port=1;gpio_out=8;setup();
    assert(!t3p_capture_store_init(&model_clock));assert(!t3p_capture_store_prepare());
    assert(erases==32&&gpio_out==8&&gpio_dir==0xffffffff);
    if(argc==3){
        /* Replay every storage call from the linked Pi32v2 Rust writer,
         * including reads that must flush C's pending page before verify. */
        model_port=0;gpio_out=1;setup();
        assert(!t3p_capture_store_init(&model_clock));
        assert(!t3p_capture_store_prepare());
        memset(flash+T3P_CAPTURE_START,255,T3P_CAPTURE_BYTES);
        memset(page_programs,0,sizeof page_programs);
        FILE *flow=fopen(argv[2],"rb");assert(flow);uint8_t h[9],expected[258],actual[258];
        unsigned records=0;
        while(fread(h,1,9,flow)==9){
            uint32_t at=(uint32_t)h[1]|(uint32_t)h[2]<<8|(uint32_t)h[3]<<16|(uint32_t)h[4]<<24;
            uint32_t n=(uint32_t)h[5]|(uint32_t)h[6]<<8|(uint32_t)h[7]<<16|(uint32_t)h[8]<<24;
            assert(n<=sizeof expected);records++;
            if(h[0]==2){assert(!at&&!n);assert(!t3p_capture_store_flush());continue;}
            assert(h[0]<=1&&fread(expected,1,n,flow)==n);
            if(h[0]==1)assert(!t3p_capture_store_program(at,expected,n));
            else {assert(!t3p_capture_store_read(at,actual,n));assert(!memcmp(expected,actual,n));}
        }
        assert(feof(flow)&&records>1000);fclose(flow);
        assert(!memcmp(flash+T3P_CAPTURE_START,"T3PSCR01",8));
        assert(!memcmp(flash+T3P_CAPTURE_START+60,"DONE",4));
        unsigned maximum=0,total=0;
        for(unsigned i=0;i<sizeof page_programs/sizeof page_programs[0];i++){
            if(page_programs[i]>maximum)maximum=page_programs[i];total+=page_programs[i];
        }
        printf("linked writer -> C buffered NOR replay: %u calls, %u page programs, max %u per page\n",records,total,maximum);
    }
    puts("capture NOR framing, partition bounds, page splits, readback, reboot and write refusal passed");
}
