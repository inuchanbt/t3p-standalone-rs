/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../native/t3p_capture_probe.h"
static unsigned reads;
static uint32_t rd(void *ctx, uint32_t address) {
    (void)ctx;
    static const uint32_t allowed[]={0xfe0200,0xfe0204,0xfe0210,0xfe0218,0xfe021c,0xfe0220,0xfe0224};
    assert(reads<7 && address==allowed[reads++]); return address;
}
static int boot_fail;
static uint32_t primary=0x102000;
static int ram(void *ctx,uint32_t address,void *out,uint32_t count) {
    (void)ctx;
    if (address==0x100000) {
        const uint32_t root[]={primary,2,0x101000,4,5,6}; assert(count==24); memcpy(out,root,24); return 0;
    }
    assert((address==0x101000 || address==0x102000) && count==32);
    if(address==0x102000) { memset(out,0x55,count); return boot_fail&2 ? -1 : 0; }
    memset(out,0x77,count); return boot_fail&1 ? -1 : 0;
}
int main(void) {
    struct t3p_boot_snapshot boot;
    assert(!t3p_boot_capture(&boot,0x100000,ram,0));
    assert(boot.auxiliary_valid==1 && boot.header_valid==1 && sizeof boot==100);
    struct t3p_bsp b={.io={.read32=rd}}; /* no write callbacks: any write would fail */
    uint32_t p[]={11,22,33,44}, words[36];
    t3p_capture_probe_read(&b,&boot,p,words);
    assert(reads==7 && words[4]==7 && words[7]==0x101000 && words[18]==0x77777777 && !words[26]);
    for(unsigned i=27;i<35;i++)assert(words[i]==0x55555555);
    boot_fail=1;
    assert(!t3p_boot_capture(&boot,0x100000,ram,0) && !boot.auxiliary_valid);
    reads=0;t3p_capture_probe_read(&b,&boot,p,words);
    assert(words[4]==5);for(unsigned i=11;i<=18;i++)assert(!words[i]);
    boot_fail=2;
    assert(!t3p_boot_capture(&boot,0x100000,ram,0) && !boot.header_valid);
    reads=0;t3p_capture_probe_read(&b,&boot,p,words);
    assert(words[4]==3);for(unsigned i=27;i<35;i++)assert(!words[i]);
    primary=0xfe0200;boot_fail=0;
    assert(!t3p_boot_capture(&boot,0x100000,ram,0) && !boot.header_valid);
    assert(t3p_boot_capture(&boot,0xfe0200,ram,0)==-1 && !boot.valid);
    reads=0;t3p_capture_probe_read(&b,&boot,p,words);
    assert(!words[4]);for(unsigned i=5;i<=18;i++)assert(!words[i]);
    for(unsigned i=27;i<35;i++)assert(!words[i]);
    return 0;
}
