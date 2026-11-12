/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Own polling SPI0 implementation. No SDK objects or ROM calls. All code
 * reachable with SFC suspended is copied into SRAM by runtime_memory_init.
 * Capture I/O uses RAW SPI, bypassing the loader's transparent SFC decode.
 * No status-register writes, chip erase, VM access or protection changes.
 */
#include "t3p_capture_store.h"
#define RAM __attribute__((section(".data.flash_ram"),noinline))
#ifdef T3P_CAPTURE_STORE_HOST
uint32_t capture_mmio_read(uint32_t);
void capture_mmio_write(uint32_t,uint32_t);
uint8_t capture_mapped_read(uint32_t);
#define R(a) capture_mmio_read(a)
#define W(a,v) capture_mmio_write(a,v)
#define M(a) capture_mapped_read(a)
#define SYNC() ((void)0)
#else
#define R(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define W(a,v) (*(volatile uint32_t *)(uintptr_t)(a)=(v))
#define M(a) (*(volatile const uint8_t *)(uintptr_t)(a))
#define SYNC() __asm__ volatile("csync" ::: "memory")
#endif
#define SFC 0xfe0200u
#define SPI 0xfe0700u
#define GPIO 0xfe1000u
uint32_t t3p_capture_status=1,t3p_capture_jedec,t3p_capture_sfdp;
static uint32_t cs_mask, saved_sfc, saved_spi[3], saved_gpio[2];
static uint32_t virgin, page_at=~0u;
static uint8_t page[256];
static unsigned dirty;
static struct t3p_bsp *timer_owner;

static RAM void clock_tick(void)
{
    /* Same TIMER0 accounting as the BSP, with no XIP callback. Poll during
     * long erase/read loops so its 16-bit counter cannot silently wrap. */
    struct t3p_bsp *b=timer_owner;
    if(!b)return;
    uint16_t now=(uint16_t)R(0xfd0604);
    b->fraction+=(uint32_t)(uint16_t)(now-b->previous_tick)*64;
    b->previous_tick=now;
    b->milliseconds+=b->fraction/375;
    b->fraction%=375;
}

static RAM void cs(unsigned high)
{
    W(GPIO+4,(R(GPIO+4)&~cs_mask)|(high?cs_mask:0));
    W(GPIO+8,R(GPIO+8)&~cs_mask);SYNC();
}
static RAM int byte(unsigned value,unsigned receive)
{
    if(receive)W(SPI,R(SPI)|8);
    W(SPI,R(SPI)|0x4000);
    W(SPI,receive?(R(SPI)|0x1000):(R(SPI)&~0x1000u));
    W(SPI,R(SPI)&~0xc00u);W(SPI+8,value&255);SYNC();
    unsigned timeout=196608;
    while(!(R(SPI)&0x8000u))if(!--timeout)return -1;
    unsigned result=R(SPI+8)&255;
    W(SPI,R(SPI)|0x1000);W(SPI,R(SPI)&~8u);W(SPI,R(SPI)|0x4000);
    return (int)result;
}
static RAM int enter(void)
{
    saved_sfc=R(SFC);
    saved_spi[0]=R(SPI);saved_spi[1]=R(SPI+4);saved_spi[2]=R(SPI+20);
    saved_gpio[0]=R(GPIO+4);saved_gpio[1]=R(GPIO+8);
    /* Drain cache traffic before disabling XIP; IRQs remain disabled by
     * the standalone entry. This firmware never starts the second core. */
    SYNC();unsigned timeout=196608;
    while(R(0xffe400)&0x80000000u)if(!--timeout)return -1;
    timeout=196608;
    while(R(SFC)&0x80000000u)if(!--timeout)return -1;
    W(SFC,R(SFC)&~1u);W(SFC,R(SFC)&~2u);
    /* tzspi_init config plus sfc_suspend(1)'s SPI enable bit. */
    W(SPI,0x125021u);W(SPI+20,R(SPI+20)&~4u);W(SPI+4,31);
    cs(1);return 0;
}
static RAM void leave(void)
{
    cs(1);W(SPI,saved_spi[0]);W(SPI+4,saved_spi[1]);W(SPI+20,saved_spi[2]);
    W(GPIO+4,saved_gpio[0]);W(GPIO+8,saved_gpio[1]);
    W(SFC,(R(SFC)&~3u)|(saved_sfc&3));SYNC();
    /* Never return into unavailable XIP after a controller fault. */
    while(!(R(SFC)&0x40000000u)){}
}
static RAM int address(unsigned command,uint32_t at)
{
    cs(0);
    if(byte(command,0)<0||byte(at>>16,0)<0||byte(at>>8,0)<0||byte(at,0)<0)return -1;
    return 0;
}
static RAM int status(void)
{
    clock_tick();
    cs(0);int s=byte(5,0);if(s>=0)s=byte(255,1);cs(1);return s;
}
static RAM int read_raw(uint32_t at,uint8_t *out,uint32_t n,unsigned sfdp)
{
    if(enter())return -1;
    int result=address(sfdp?0x5a:3,at);
    if(!result&&sfdp&&byte(0,0)<0)result=-1;
    while(n--&&!result){int b=byte(255,1);if(b<0)result=-1;else *out++=(uint8_t)b;}
    cs(1);clock_tick();leave();return result;
}
static RAM int operate(uint32_t at,const uint8_t *data,unsigned n)
{
    if(enter())return -1;
    int result=-1,s=status();
    if(s<0||(s&1))goto done;
    cs(0);int ok=byte(6,0);cs(1);s=status();
    if(ok<0||s<0||(s&3)!=2)goto done;
    if(address(n?2:0x20,at))goto uncertain;
    for(unsigned i=0;i<n;i++)if(byte(data[i],0)<0)goto uncertain;
    cs(1);
    /* Keep execution in SRAM until WIP clears. On an uncertain transfer,
     * never resume XIP while the flash might still be busy. */
    for(unsigned i=0;i<2000000;i++){
        s=status();if(s>=0&&!(s&1)){result=0;goto done;}
    }
uncertain:
    cs(1);
    for(;;){s=status();if(s>=0&&!(s&1))break;}
done:
    cs(1);leave();return result;
}
static uint32_t get(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t crc(const uint8_t *p,unsigned n)
{
    uint32_t c=~0u;while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}return ~c;
}
static unsigned crc16(const uint8_t *p,unsigned n)
{
    unsigned c=0;while(n--){c^=(unsigned)*p++<<8;for(unsigned j=0;j<8;j++)c=((c<<1)^((c&0x8000)?0x1021:0))&65535;}return c;
}
static int mapped_header(uint32_t at,uint8_t h[32])
{
    for(unsigned i=0;i<32;i++)h[i]=M(at+i);
    return crc16(h+2,30)==(unsigned)(h[0]|h[1]<<8);
}
static RAM int identify(uint8_t id[3])
{
    if(enter())return -1;cs(0);int result=byte(0x9f,0);
    for(unsigned i=0;i<3;i++){int b=byte(255,1);if(b<0)result=-1;id[i]=(uint8_t)b;}
    cs(1);leave();return result<0?-1:0;
}
/* Volatile entry pointers force full-address calls across the XIP/SRAM gap;
 * the Pi32v2 backend otherwise shortens local calls to a 2 MiB range. */
static int (*volatile raw_io)(uint32_t,uint8_t *,uint32_t,unsigned)=read_raw;
static int (*volatile program_io)(uint32_t,const uint8_t *,unsigned)=operate;
static int (*volatile identify_io)(uint8_t *)=identify;
int t3p_capture_store_init(struct t3p_bsp *clock)
{
    timer_owner=clock;
    t3p_capture_status=1;page_at=~0u;dirty=0;virgin=0;
    if(R(SFC+16)!=0xf4005000u||(R(SFC+4)&1)||R(SFC+0x24))return -1;
    uint8_t h[32];
    if(!mapped_header(0xc000000,h)||get(h+8)!=0x80000||get(h+4)!=0xc000100)return -1;
    if(!mapped_header(0xc000020,h)||get(h+4)!=0x100||get(h+8)!=0x7ff00)return -1;
    if(!mapped_header(0xc080000,h)||get(h+4)!=0x85000||get(h+8)!=0xe5e40||h[12]!=0x12||h[13]!=0x81)return -1;
    for(unsigned i=0;i<32;i++)h[i]=M(0xc0000c0+i);
    const uint8_t magic[]={'T','3','P','C','A','P','0','1'};
    for(unsigned i=0;i<8;i++)if(h[i]!=magic[i])return -1;
    if(get(h+8)!=T3P_CAPTURE_START||get(h+12)!=T3P_CAPTURE_BYTES||get(h+16)!=T3P_CAPTURE_SLOT||
       get(h+20)!=1||get(h+24)!=0x80000||get(h+28)!=crc(h,28))return -1;
    /* Record only an entirely FF DECODED slot as a fresh firmware image.
     * Previously captured raw data will not satisfy this predicate. */
    for(unsigned slot=0;slot<6;slot++){
        unsigned empty=1;for(unsigned i=0;i<T3P_CAPTURE_SLOT;i++)
            if(M(0xc000000+T3P_CAPTURE_START-0x5000+slot*T3P_CAPTURE_SLOT+i)!=255){empty=0;break;}
        if(empty)virgin|=1u<<slot;
    }
    cs_mask=(R(0xfe1104)&2)?8:1;
    t3p_capture_status=2;
    uint8_t id[3];if(identify_io(id))return -1;
    t3p_capture_jedec=(uint32_t)id[0]<<16|(uint32_t)id[1]<<8|id[2];
    if(id[0]==0||id[0]==255||id[2]!=0x15)return -1;
    uint8_t s[64];if(raw_io(0,s,16,1))return -1;
    t3p_capture_sfdp=get(s);
    if(get(s)!=0x50444653u||s[5]!=1||s[8]!=0||s[10]!=1||s[11]<11||s[15]!=255)return -1;
    uint32_t pt=(uint32_t)s[12]|(uint32_t)s[13]<<8|(uint32_t)s[14]<<16;
    if(pt>0xffc0||raw_io(pt,s,44,1))return -1;
    if(get(s+4)!=0xffffffu||((get(s)>>17)&3)>1||((get(s+40)>>4)&15)!=8)return -1;
    unsigned erase=0;for(unsigned i=28;i<36;i+=2)if(s[i]==12&&s[i+1]==0x20)erase=1;
    if(!erase)return -1;
    t3p_capture_status=0;return 0;
}
static int bounds(uint32_t at,uint32_t n)
{return !t3p_capture_status&&at<=T3P_CAPTURE_BYTES&&n<=T3P_CAPTURE_BYTES-at;}
int t3p_capture_store_flush(void)
{
    if(!dirty)return t3p_capture_status?-1:0;
    if(!bounds(page_at,256)||program_io(T3P_CAPTURE_START+page_at,page,256)){t3p_capture_status=3;return -1;}
    uint8_t check[256];if(raw_io(T3P_CAPTURE_START+page_at,check,256,0)){t3p_capture_status=3;return -1;}
    for(unsigned i=0;i<256;i++)if(check[i]!=page[i]){t3p_capture_status=3;return -1;}
    dirty=0;return 0;
}
int t3p_capture_store_read(uint32_t at,uint8_t *out,uint32_t n)
{
    if(!bounds(at,n)||t3p_capture_store_flush())return -1;
    return raw_io(T3P_CAPTURE_START+at,out,n,0);
}
int t3p_capture_store_export_read(uint32_t at,uint8_t *out,uint32_t n)
{
    /* USB requests must never flush a pending program buffer. */
    if(!bounds(at,n)||dirty)return -1;
    return raw_io(T3P_CAPTURE_START+at,out,n,0);
}
int t3p_capture_store_program(uint32_t at,const uint8_t *p,uint32_t n)
{
    if(!bounds(at,n))return -1;
    while(n--){uint32_t base=at&~255u;
        if(base!=page_at){if(t3p_capture_store_flush()||raw_io(T3P_CAPTURE_START+base,page,256,0))return -1;page_at=base;}
        unsigned pos=at++&255;
        if((page[pos]&*p)!=*p)return -1;
        page[pos]=*p++;dirty=1;
    }return 0;
}
int t3p_capture_store_prepare(void)
{
    if(t3p_capture_status||t3p_capture_store_flush())return -1;
    /* This is invoked by a SAVE gesture, never at boot/USB enumeration.
     * Only slots proved to be fresh packaged FF are initialized. Unknown,
     * dirty and valid old slots are never erased or recycled automatically. */
    uint8_t check[256];
    for(unsigned slot=0;slot<6;slot++)if(virgin&(1u<<slot)){
        virgin&=~(1u<<slot); /* A failure cannot silently authorize another try. */
        for(unsigned at=slot*T3P_CAPTURE_SLOT;at<(slot+1)*T3P_CAPTURE_SLOT;at+=4096){
            if(program_io(T3P_CAPTURE_START+at,0,0)){t3p_capture_status=3;return -1;}
            for(unsigned off=0;off<4096;off+=256){
                if(raw_io(T3P_CAPTURE_START+at+off,check,256,0)){t3p_capture_status=3;return -1;}
                for(unsigned i=0;i<256;i++)if(check[i]!=255){t3p_capture_status=3;return -1;}
            }
        }
        break; /* Initialize at most one new slot for this SAVE. */
    }
    page_at=~0u;return 0;
}
