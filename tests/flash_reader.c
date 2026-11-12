/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../native/t3p_flash_reader.h"
static unsigned reads;
static uint32_t reg(void *ctx,uint32_t a) {
    (void)ctx;
    switch(a){case 0xfe0200:return 0xdc7e4407;case 0xfe0210:return 0xf4005000;
        case 0xfe0218:return 0xf92e465;case 0xfe021c:return 0xffec8ee;default:assert(0);return 0;}
}
static int fail;
static int flash(void *ctx,uint32_t a,uint8_t *p,uint32_t n) {
    (void)ctx;assert(a>=0xc000000&&a+n<=0xc1fa000&&n<=40);reads++;
    for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(a+i);
    return fail;
}
int main(int argc,char **argv) {
    struct t3p_boot_snapshot boot={.valid=T3P_BOOT_MAGIC,.header_valid=1,
        .context={0x100020,0x5000,0xc000000}};
    boot.header[8]=0;boot.header[9]=0xf0;boot.header[10]=0x1f;
    struct t3p_bsp b={.io={.read32=reg}};struct t3p_flash_reader r;
    assert(!t3p_flash_reader_init(&r,&boot,&b,0,flash));
    uint8_t q[64]={'T','3','P','Q'},a[64],fixture[128];
    t3p_flash_reader_request(&r,q,a);assert(!a[5]&&a[6]==40&&!reads);memcpy(fixture,a,64);
    q[4]=1;q[6]=40;q[8]=7;q[12]=9;
    t3p_flash_reader_request(&r,q,a);assert(!a[5]&&a[6]==40&&a[24]==7&&a[63]==46&&reads==1);
    memcpy(fixture+64,a,64);
    q[8]=0xff;q[9]=0x9f;q[10]=0x1f;q[6]=1;
    t3p_flash_reader_request(&r,q,a);assert(!a[5]&&a[24]==0xff&&reads==2);
    q[6]=2;t3p_flash_reader_request(&r,q,a);assert(a[5]==3&&reads==2);
    memset(q+8,0xff,4);t3p_flash_reader_request(&r,q,a);assert(a[5]==3&&reads==2);
    memset(q+8,0,4);q[6]=41;t3p_flash_reader_request(&r,q,a);assert(a[5]==3);
    q[6]=0;t3p_flash_reader_request(&r,q,a);assert(a[5]==3);
    q[4]=2;t3p_flash_reader_request(&r,q,a);assert(a[5]==1);
    q[4]=1;q[6]=1;q[63]=1;t3p_flash_reader_request(&r,q,a);assert(a[5]==1);
    q[63]=0;fail=1;t3p_flash_reader_request(&r,q,a);assert(a[5]==4&&a[6]==0);
    for(unsigned i=24;i<64;i++)assert(!a[i]);
    boot.context[1]=0x15000;assert(t3p_flash_reader_init(&r,&boot,&b,0,flash)==-1);
    t3p_flash_reader_request(&r,q,a);assert(a[5]==2);
    boot.context[1]=0x5000;boot.header_valid=0;assert(t3p_flash_reader_init(&r,&boot,&b,0,flash)==-1);
    if(argc==2){FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(fixture,1,128,f)==128);assert(!fclose(f));}
    puts("flash reader protocol/guards: PASS");return 0;
}
