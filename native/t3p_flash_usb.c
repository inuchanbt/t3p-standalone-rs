/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Polling EP0 HID feature transport. Register sequences independently
 * transcribed from stock loader 104320..104372 and pinned BR35 SDK usb.c.o.
 * No SDK object code or ROM entry points are linked. */
#include "t3p_flash_reader.h"
#define SIE 0xfd2000u
#define USB 0xfd2500u
#define CSR (SIE+0x44)
#define PORT 0xfd3500u
static struct t3p_bsp *bus;
static struct t3p_flash_reader reader;
static volatile uint8_t dma[64] __attribute__((aligned(4)));
static uint8_t answer[64],configured,stage,address_pending,address_value;
static uint8_t tx_data[64],tx_length,tx_pending;
static uint32_t resets,stalls;
static uint32_t setups,setup_word0,setup_word1,last_csr,last_count,bus_events;
void meter_flash_reader_render(const uint32_t words[17],uint32_t top,uint16_t pixels[1920]);
static uint32_t rd(uint32_t a){return bus->io.read32(bus->io.context,a);}
static void wr(uint32_t a,uint32_t v){bus->io.write32(bus->io.context,a,v);}
static void change(uint32_t a,uint32_t clear,uint32_t set){wr(a,(rd(a)&~clear)|set);}
static unsigned word(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static void stall(void){wr(CSR,0x60);stage=address_pending=tx_pending=0;stalls++;}
static void kick_tx(void)
{
    if(!tx_pending||(rd(CSR)&2))return;
    for(unsigned i=0;i<tx_length;i++)dma[i]=tx_data[i];
    wr(USB+8,tx_length);wr(CSR,0x0a);tx_pending=0;
}
static void send(const uint8_t *p,unsigned n)
{
    /* Match SDK usb_control_transfer -> ep0_stage_tx: release the received
     * packet BEFORE replacing the shared RX/TX DMA buffer or arming CNT.
     * Combining service-RX and start-TX after CNT (V1/V2) is not equivalent. */
    for(unsigned i=0;i<n;i++)tx_data[i]=p[i];
    wr(CSR,0x40);stage=2;tx_length=(uint8_t)n;tx_pending=1;kick_tx();
}
static const uint8_t device[]={18,1,0,2,0,0,0,64,0x83,0x04,0x50,0x57,0x03,0x09,1,2,0,1};
static const uint8_t report[]={0x06,0x31,0xff,0x09,1,0xa1,1,0x15,0,0x26,0xff,0,
                             0x75,8,0x95,64,0x09,1,0xb1,2,0xc0};
static const uint8_t configuration[]={9,2,34,0,1,1,0,0x80,50,
    9,4,0,0,1,3,0,0,0,9,0x21,0x11,1,0,1,0x22,sizeof report,0,
    7,5,0x81,3,64,0,10};
static void reset(void)
{
    configured=stage=address_pending=address_value=tx_pending=0;resets++;
    wr(SIE,0);wr(USB+0x1c,(uint32_t)(uintptr_t)dma);
    /* EP1 is advertised for HID compatibility; it always NAKs. Only EP0
     * feature reports are used, and its DMA pointer remains in static RAM. */
    wr(USB+0x20,(uint32_t)(uintptr_t)dma);
    wr(0xfd2140,64);wr(0xfd2144,0x48);wr(0xfd2148,0);
    change(USB,1u<<20,0);
}
static void setup(const uint8_t q[8])
{
    setups++;
    setup_word0=(uint32_t)q[0]|(uint32_t)q[1]<<8|(uint32_t)q[2]<<16|(uint32_t)q[3]<<24;
    setup_word1=(uint32_t)q[4]|(uint32_t)q[5]<<8|(uint32_t)q[6]<<16|(uint32_t)q[7]<<24;
    unsigned type=q[0],req=q[1],value=word(q+2),index=word(q+4),length=word(q+6);
    stage=address_pending=tx_pending=0;
    if(type==0x80&&req==6&&index==0) {
        const uint8_t *p=0;unsigned n=0;uint8_t string[64];
        if(value==0x100){p=device;n=sizeof device;}
        if(value==0x200){p=configuration;n=sizeof configuration;}
        if(value==0x300){string[0]=4;string[1]=3;string[2]=9;string[3]=4;p=string;n=4;}
        if(p){send(p,length<n?length:n);return;}
    }
    if(type==0x80&&req==6&&(index==0||index==0x409)&&((value==0x301)||(value==0x302))) {
        const char *s=value==0x301?"T3P Experimental":"T3P Flash Reader V3";
        uint8_t p[64]={0,3};unsigned n=2;
        while(*s&&n<62){p[n++]=(uint8_t)*s++;p[n++]=0;}p[0]=(uint8_t)n;
        send(p,length<n?length:n);return;
    }
    if(type==0x81&&req==6&&index==0&&(value==0x2200||value==0x2100)) {
        const uint8_t *p=value==0x2200?report:configuration+18;
        unsigned n=value==0x2200?sizeof report:9;send(p,length<n?length:n);return;
    }
    if(type==0&&req==5&&index==0&&length==0&&value<=127&&!configured) {
        address_value=(uint8_t)value;address_pending=1;wr(CSR,0x48);return;
    }
    if(type==0&&req==9&&index==0&&length==0&&value<=1) {
        configured=(uint8_t)value;wr(CSR,0x48);return;
    }
    if(type==0x80&&req==8&&value==0&&index==0&&length==1){send(&configured,1);return;}
    if((type==0x80||type==0x81||type==0x82)&&req==0&&value==0&&length==2&&
       ((type==0x80&&index==0)||(type==0x81&&index==0&&configured)||
        (type==0x82&&(index==0||index==0x80||(index==0x81&&configured))))) {
        const uint8_t zero[]={0,0};send(zero,2);return;
    }
    if(configured&&type==0x81&&req==10&&index==0&&value==0&&length==1) {
        const uint8_t alt=0;send(&alt,1);return;
    }
    if(configured&&type==1&&req==11&&index==0&&value==0&&length==0){wr(CSR,0x48);return;}
    if(configured&&type==0xa1&&req==1&&value==0x300&&index==0&&length==64){send(answer,64);return;}
    if(configured&&type==0x21&&req==9&&value==0x300&&index==0&&length==64) {
        wr(CSR,0x40);stage=1;return;
    }
    stall();
}
static void poll(void)
{
    uint32_t intr=rd(SIE+0x18),tx=rd(SIE+8);
    bus_events|=intr;
    (void)rd(SIE+0xc);(void)rd(SIE+0x10);(void)rd(SIE+0x14);
    if(intr&4){reset();return;}
    if(intr&0x20){configured=stage=address_pending=tx_pending=0;return;}
    uint32_t csr=rd(CSR);
    if(csr)last_csr=csr;
    if(csr&4){wr(CSR,0);stage=address_pending=tx_pending=0;return;}
    if(csr&0x10){wr(CSR,0x80);stage=address_pending=tx_pending=0;csr=rd(CSR);}
    if(address_pending&&(tx&1)&&!(csr&2)) {wr(SIE,address_value);address_pending=0;}
    if(!(csr&1)){kick_tx();return;}
    unsigned n=rd(SIE+0x58)&0x7f;
    last_count=n;
    if(stage==1) {
        if(n!=64){stall();return;}
        uint8_t q[64];for(unsigned i=0;i<64;i++)q[i]=dma[i];
        t3p_flash_reader_request(&reader,q,answer);wr(CSR,0x48);stage=0;
    } else if(stage==2&&n==0){wr(CSR,0x40);stage=tx_pending=0;}
    else if(n==8) {uint8_t q[8];for(unsigned i=0;i<8;i++)q[i]=dma[i];setup(q);}
    else stall();
}
static int mapped_read(void *context,uint32_t address,uint8_t *out,uint32_t n)
{
    (void)context;
    volatile const uint8_t *p=(volatile const uint8_t *)(uintptr_t)address;
    for(uint32_t i=0;i<n;i++)out[i]=p[i];return 0;
}
static int start(struct t3p_bsp *b)
{
    bus=b;
    /* SDK usb_g_hold: isolate pins in IO mode, release both pullups and
     * pulldowns for 150ms before presenting the new descriptors. */
    change(PORT+0x30,0,0x100);change(PORT+8,0,3);change(PORT+0xc,3,0);
    change(PORT+0x14,3,0);change(PORT+0x1c,3,0);
    if(t3p_bsp_delay(b,150))return -1;
    /* Same full-speed device pin setup as stock loader 104320..104356.
     * Preserve inherited PLL/SFC configuration and keep CPU IRQs disabled. */
    change(PORT+0xc,0,3);change(PORT+0x30,0x104,0);
    change(USB,0x4076,0);wr(USB+4,0);change(PORT+0x30,0,1);
    change(PORT+0x1c,3,0);change(PORT+0x14,3,1);change(USB,0,0x3c);
    /* The peripheral event gates are required even while CPU IRQs stay
     * disabled. Stock 104372 enables CON0.SIEIE; the SDK slave init also
     * enables USB bus and EP0 event sources. V1 incorrectly cleared all. */
    wr(SIE+0x1c,1);wr(SIE+0x20,0);wr(SIE+0x24,0);wr(SIE+0x28,0);
    wr(SIE+0x2c,0x24);wr(SIE+4,0x40);reset();
    change(USB,0,0x800);
    return 0;
}
void t3p_flash_usb_run(struct t3p_bsp *b)
{
    (void)t3p_flash_reader_init(&reader,&t3p_boot_snapshot,b,0,mapped_read);
    uint8_t info[64]={'T','3','P','Q'};t3p_flash_reader_request(&reader,info,answer);
    if(start(b))return;
    static uint16_t pixels[1920];uint32_t last=~0u;
    for(;;) {
        poll();uint32_t now=t3p_bsp_millis(b);
        if((uint32_t)(now-last)<1000)continue;last=now;
        uint32_t words[]={reader.ready,configured,reader.requests,reader.errors,resets,stalls,reader.physical_base,reader.length,
                          setups,setup_word0,setup_word1,last_csr,last_count,rd(USB),bus_events,rd(SIE+4),(now/4000)%2};
        for(unsigned top=0;top<240;top+=8){
            poll();meter_flash_reader_render(words,top,pixels);
            if(t3p_bsp_rectangle(b,0,239,top,top+7,pixels,1920))return;
        }
        t3p_bsp_backlight(b,1);
    }
}
