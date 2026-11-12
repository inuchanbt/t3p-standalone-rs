/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_flash_usb.c"
#ifdef T3P_CAPTURE_DEVICE
uint32_t t3p_capture_status,t3p_capture_jedec,t3p_capture_sfdp;
uint32_t t3p_capture_diagnostics[10];
int t3p_capture_store_export_read(uint32_t at,uint8_t *p,uint32_t n)
{for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(at+i);return 0;}
#endif
static uint32_t regs[0x1600/4];
static unsigned forbidden,rx_held,rx_size,tx_arms;
static uint8_t rx_copy[64];
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
    if(a==USB+8) {
        /* The shared DMA buffer is still owned by RX until serviced.
         * V1/V2 armed CNT before that transition and fail this assertion. */
        assert(!rx_held&&!(regs[(CSR-SIE)/4]&3));tx_arms++;
    }
    if(a==CSR&&(v&0x40)) {
        /* V1/V2 also replaced the shared DMA bytes before releasing RX. */
        if(rx_held)for(unsigned i=0;i<rx_size;i++)assert(dma[i]==rx_copy[i]);
        rx_held=0;regs[(a-SIE)/4]&=~1u;
        if(v&8)regs[(a-SIE)/4]|=8;
        if(v&0x20)regs[(a-SIE)/4]|=0x20;
        assert(!(v&2)); /* service RX and queue TX are distinct operations */
    } else if(a==CSR&&v==0x80)regs[(a-SIE)/4]&=~0x10u;
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
void meter_flash_reader_render(const uint32_t w[17],uint32_t t,uint16_t p[1920]){(void)w;(void)t;(void)p;}
struct t3p_boot_snapshot t3p_boot_snapshot;
static void packet(const uint8_t *p,unsigned n) {
    for(unsigned i=0;i<n;i++)dma[i]=rx_copy[i]=p[i];rx_size=n;rx_held=1;
    regs[(CSR-SIE)/4]=1;regs[0x58/4]=n;poll();
}
int main(void) {
    struct t3p_bsp b={.io={.read32=read_reg,.write32=write_reg}};assert(!start(&b));
    assert(!(regs[(PORT+0x30-SIE)/4]&0x100));
    assert((regs[(PORT+0x14-SIE)/4]&3)==1);
    /* Match stock SIE enable and SDK EP0/bus event-source setup. Polling
     * CPU code does not justify disabling the peripheral event gates. */
    assert(regs[(USB-SIE)/4]&0x800);
    assert(regs[0x1c/4]==1&&regs[0x2c/4]==0x24);
    regs[0x18/4]=4;poll();assert(resets==2&&!configured&&!stage);
    uint8_t q[8]={0x80,6,0,1,0,0,18,0};packet(q,8);
    assert(dma[0]==18&&dma[1]==1&&regs[(USB+8-SIE)/4]==18&&stage==2);
    assert(!rx_held&&!tx_pending&&tx_arms==1);
    assert(setups==1&&setup_word0==0x1000680&&setup_word1==0x120000);
    assert(last_csr==1&&last_count==8&&(bus_events&4));
    uint8_t empty=0;packet(&empty,0);assert(!stage);
    uint8_t addr[8]={0,5,37,0,0,0,0,0};packet(addr,8);
    assert(address_pending&&regs[0]==0);
    regs[(CSR-SIE)/4]=0;regs[8/4]=1;poll();assert(regs[0]==37&&!address_pending);
    uint8_t cfg[8]={0,9,1,0,0,0,0,0};packet(cfg,8);assert(configured==1);
    uint8_t set[8]={0x21,9,0,3,0,0,64,0};packet(set,8);assert(stage==1);
    uint8_t request[64]={'T','3',
#ifdef T3P_CAPTURE_DEVICE
        'C',
#else
        'P',
#endif
        'Q'};packet(request,64);assert(!stage&&answer[6]==40);
    uint8_t get[8]={0xa1,1,0,3,0,0,64,0};packet(get,8);assert(stage==2&&dma[0]=='T');
    /* SETUP_END aborts a previous IN transfer and accepts a new request. */
    for(unsigned i=0;i<8;i++)dma[i]=rx_copy[i]=set[i];rx_size=8;rx_held=1;
    regs[(CSR-SIE)/4]=0x11;regs[0x58/4]=8;
    poll();assert(stage==1);
    packet(request,63);assert(!stage&&stalls==1);
    set[6]=65;packet(set,8);assert(stalls==2);
    q[0]=0x40;q[1]=0xaa;packet(q,8);assert(stalls==3);
    regs[0x18/4]=4;poll();assert(!configured&&!stage&&regs[0]==0);
    packet(get,8);assert(stalls==4); /* feature access before configuration */
    packet(cfg,8);packet(set,8); /* malformed length must still stall */
    set[6]=64;packet(set,8);assert(stage==1);
#ifdef T3P_CAPTURE_DEVICE
    unsigned requests=setups;
#else
    unsigned requests=reader.requests;
#endif
    regs[0x18/4]=4;poll();assert(!stage);
#ifdef T3P_CAPTURE_DEVICE
    assert(setups==requests);
#else
    assert(reader.requests==requests);
#endif
    regs[0x18/4]=0x20;poll();assert(!configured&&!stage);
    /* A busy endpoint must retain a private copy until TX is available. */
    uint8_t data[]={0x12,1,2};regs[(CSR-SIE)/4]=2;unsigned arms=tx_arms;
    send(data,3);assert(tx_pending&&tx_arms==arms);data[0]=0xff;
    regs[(CSR-SIE)/4]=0;poll();assert(!tx_pending&&tx_arms==arms+1&&dma[0]==0x12);
    /* Reset cancels a deferred response rather than transmitting it later. */
    send(data,3);assert(tx_pending);arms=tx_arms;
    regs[0x18/4]=4;poll();assert(!tx_pending&&tx_arms==arms);
    regs[(CSR-SIE)/4]=0;poll();assert(tx_arms==arms);
    regs[(CSR-SIE)/4]=2;send(data,3);assert(tx_pending);arms=tx_arms;
    packet(&empty,0);assert(!tx_pending&&!stage&&tx_arms==arms);
    assert(!forbidden);
    return 0;
}
