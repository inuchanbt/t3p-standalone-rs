/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_flash_reader.h"
static uint32_t get(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put(uint8_t *p,uint32_t v)
{ for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t crc(const uint8_t *p,unsigned n)
{
    uint32_t c=~0u;
    while(n--) { c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u& (0u-(c&1))); }
    return ~c;
}
int t3p_flash_reader_init(struct t3p_flash_reader *r,const struct t3p_boot_snapshot *boot,
                        struct t3p_bsp *b,void *context,int (*read)(void *,uint32_t,uint8_t *,uint32_t))
{
    *r=(struct t3p_flash_reader){.context=context,.read=read};
    r->con0=b->io.read32(b->io.context,0xfe0200);
    r->offset=b->io.read32(b->io.context,0xfe0210);
    r->ndec_start=b->io.read32(b->io.context,0xfe0218);
    r->ndec_end=b->io.read32(b->io.context,0xfe021c);
    /* Limit this trial to the photographed unit's established mapping/header.
     * FlashSize is a boundary, not a JEDEC capacity result. */
    if(!read || boot->valid!=T3P_BOOT_MAGIC || boot->header_valid!=1 ||
       boot->context[1]!=0x5000 || boot->context[2]!=0x0c000000 ||
       r->offset!=0xf4005000 || get(boot->header+8)!=0x1ff000)return -1;
    r->cpu_base=boot->context[2];r->physical_base=boot->context[1];
    r->length=0x1ff000-r->physical_base;r->ready=1;return 0;
}
void t3p_flash_reader_request(struct t3p_flash_reader *r,const uint8_t q[64],uint8_t a[64])
{
    for(unsigned i=0;i<64;i++)a[i]=0;
    a[0]='T';a[1]='3';a[2]='P';a[3]='R';a[4]=1;a[7]=1; /* mapped view */
    uint32_t at=get(q+8);put(a+8,at);put(a+16,get(q+12));r->requests++;
    unsigned bad=q[0]!='T'||q[1]!='3'||q[2]!='P'||q[3]!='Q'||q[5]||q[7];
    for(unsigned i=16;i<64;i++)bad|=q[i];
    if(bad || q[4]>1) a[5]=1;
    else if(q[4]==0) {
        if(q[6]||at)a[5]=1;
        else {
            uint32_t info[]={r->cpu_base,r->physical_base,r->length,r->con0,r->offset,
                             r->ndec_start,r->ndec_end,r->ready,0,0};
            for(unsigned i=0;i<10;i++)put(a+24+i*4,info[i]);a[6]=40;
        }
    } else if(!r->ready)a[5]=2;
    else if(!q[6]||q[6]>40||at>r->length||q[6]>r->length-at)a[5]=3;
    else if(r->read(r->context,r->cpu_base+at,a+24,q[6])) {
        a[5]=4;for(unsigned i=24;i<64;i++)a[i]=0;
    } else a[6]=q[6];
    if(a[5])r->errors++;
    /* Covers status, view, length, address, sequence and zero padding too. */
    put(a+12,crc(a,64));
}
