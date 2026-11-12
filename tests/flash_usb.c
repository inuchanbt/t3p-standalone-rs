/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_flash_usb.c"
static uint32_t regs[0x1600/4];
static unsigned forbidden;
static uint32_t read_reg(void *ctx,uint32_t a) {
    (void)ctx;assert(a>=SIE&&a<SIE+sizeof regs);
    uint32_t v=regs[(a-SIE)/4];
    if(a>=SIE+8&&a<=SIE+0x18)regs[(a-SIE)/4]=0; /* read-clear IRQ flags */
    return v;
}
static void write_reg(void *ctx,uint32_t a,uint32_t v) {
    (void)ctx;
    if(!((a>=SIE&&a<=SIE+0x3c)||(a==CSR)||(a>=0xfd2140&&a<=0xfd2148)||
         (a>=USB&&a<=USB+0x40)||(a>=PORT&&a<=PORT+0x30)))forbidden++;
    assert(a>=SIE&&a<SIE+sizeof regs);
    if(a==CSR&&v==0x80)regs[(a-SIE)/4]&=~0x10u;
    else regs[(a-SIE)/4]=v;
}
uint32_t t3p_bsp_millis(struct t3p_bsp *b){return b->milliseconds++;}
int t3p_bsp_delay(struct t3p_bsp *b,uint32_t n){
    (void)b;assert(n==150);
    assert(regs[(PORT+0x30-SIE)/4]&0x100);
    assert(!(regs[(PORT+0x14-SIE)/4]&3));
    assert(!(regs[(PORT+0x1c-SIE)/4]&3));
    assert(!(regs[(PORT+0xc-SIE)/4]&3));return 0;
}
int t3p_bsp_rectangle(void *b,uint16_t a,uint16_t z,uint16_t c,uint16_t d,const uint16_t *p,uint32_t n)
{(void)b;(void)a;(void)z;(void)c;(void)d;(void)p;(void)n;return 0;}
void t3p_bsp_backlight(struct t3p_bsp *b,int e){(void)b;(void)e;}
void meter_flash_reader_render(const uint32_t w[8],uint32_t t,uint16_t p[1920]){(void)w;(void)t;(void)p;}
struct t3p_boot_snapshot t3p_boot_snapshot;
static void packet(const uint8_t *p,unsigned n) {
    for(unsigned i=0;i<n;i++)dma[i]=p[i];regs[(CSR-SIE)/4]=1;regs[0x58/4]=n;poll();
}
int main(void) {
    struct t3p_bsp b={.io={.read32=read_reg,.write32=write_reg}};assert(!start(&b));
    assert(!(regs[(PORT+0x30-SIE)/4]&0x100));
    assert((regs[(PORT+0x14-SIE)/4]&3)==1);
    uint8_t q[8]={0x80,6,0,1,0,0,18,0};packet(q,8);
    assert(dma[0]==18&&dma[1]==1&&regs[(USB+8-SIE)/4]==18&&stage==2);
    uint8_t empty=0;packet(&empty,0);assert(!stage);
    uint8_t addr[8]={0,5,37,0,0,0,0,0};packet(addr,8);
    assert(address_pending&&regs[0]==0);
    regs[(CSR-SIE)/4]=0;regs[8/4]=1;poll();assert(regs[0]==37&&!address_pending);
    uint8_t cfg[8]={0,9,1,0,0,0,0,0};packet(cfg,8);assert(configured==1);
    uint8_t set[8]={0x21,9,0,3,0,0,64,0};packet(set,8);assert(stage==1);
    uint8_t request[64]={'T','3','P','Q'};packet(request,64);assert(!stage&&answer[6]==40);
    uint8_t get[8]={0xa1,1,0,3,0,0,64,0};packet(get,8);assert(stage==2&&dma[0]=='T');
    /* SETUP_END aborts a previous IN transfer and accepts a new request. */
    for(unsigned i=0;i<8;i++)dma[i]=set[i];regs[(CSR-SIE)/4]=0x11;regs[0x58/4]=8;
    poll();assert(stage==1);
    packet(request,63);assert(!stage&&stalls==1);
    set[6]=65;packet(set,8);assert(stalls==2);
    q[0]=0x40;q[1]=0xaa;packet(q,8);assert(stalls==3);
    regs[0x18/4]=4;poll();assert(!configured&&!stage&&regs[0]==0);
    packet(get,8);assert(stalls==4); /* feature access before configuration */
    packet(cfg,8);packet(set,8); /* malformed length must still stall */
    set[6]=64;packet(set,8);assert(stage==1);
    unsigned requests=reader.requests;regs[0x18/4]=4;poll();assert(!stage);
    assert(reader.requests==requests);
    regs[0x18/4]=0x20;poll();assert(!configured&&!stage);
    assert(!forbidden);
    return 0;
}
