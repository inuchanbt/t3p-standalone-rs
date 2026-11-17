/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../native/t3p_capture_usb.c"
uint32_t t3p_capture_status,t3p_capture_jedec=0xef4015,t3p_capture_sfdp=0x50444653;
uint32_t t3p_capture_diagnostics[10]={3,1,0,0,0,0,0,0,0,0};
static unsigned reads;
int t3p_capture_profile_page(uint32_t p,uint32_t out[10])
{if(p>=3)return -1;for(unsigned i=0;i<10;i++)out[i]=p*10+i;
 if(!p){out[0]=T3P_CAPTURE_PROFILE_MAGIC;out[1]=1;out[2]=1;out[3]=39;}
 if(p==2){out[7]=0x107;out[8]=1;out[9]=1;}return 0;}
int t3p_capture_store_export_read(uint32_t at,uint8_t *p,uint32_t n)
{reads++;for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(at+i);return 0;}
int main(int argc,char **argv)
{
    uint8_t q[64]={'T','3','C','Q'},a[64];
    t3p_capture_usb_request(q,a);assert(a[6]==40&&!a[5]&&a[7]==2&&!reads);
    FILE *f=argc==2?fopen(argv[1],"wb"):0;if(f)assert(fwrite(a,1,64,f)==64);
    q[4]=1;q[6]=40;q[8]=7;q[12]=9;t3p_capture_usb_request(q,a);
    assert(!a[5]&&reads==1);for(unsigned i=0;i<40;i++)assert(a[24+i]==i+7);
    if(f)assert(fwrite(a,1,64,f)==64);
    q[4]=3;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
    q[4]=1;t3p_capture_status=1;t3p_capture_usb_request(q,a);assert(a[5]==2&&reads==1);t3p_capture_status=0;
    memset(q+8,255,4);t3p_capture_usb_request(q,a);assert(a[5]==3&&reads==1);
    q[8]=q[9]=q[10]=q[11]=0;q[16]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
    q[16]=0;q[4]=2;q[6]=0;t3p_capture_status=2;
    t3p_capture_usb_request(q,a);assert(!a[5]&&a[6]==40&&a[24]==3&&a[28]==1&&reads==1);
    if(f)assert(fwrite(a,1,64,f)==64);
    q[6]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
    q[6]=0;q[8]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
    q[4]=3;
    for(unsigned p=0;p<3;p++){
        q[8]=(uint8_t)p;t3p_capture_usb_request(q,a);assert(!a[5]&&a[6]==40&&reads==1);
        if(f)assert(fwrite(a,1,64,f)==64);
    }
    q[8]=3;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
    q[8]=0;q[6]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
#ifdef T3P_TEMPERATURE_DIAGNOSTICS
    t3p_temperature_diagnostics=(struct t3p_temperature_diag){.magic=T3P_TEMP_DIAG_MAGIC,.version=1,
        .bus_port=1,.scl_mask=4,.sda_mask=2,.init_port=0,.active_port=1,.id=0x58};
    /* ID has deliberately not been read: its validity flag is clear. */
    q[4]=4;q[6]=0;
    for(unsigned p=0;p<=T3P_TEMP_DIAG_WORDS/10;p++){
        q[8]=(uint8_t)p;t3p_capture_usb_request(q,a);assert(!a[5]&&a[6]==40&&reads==1);
        if(!p)t3p_temperature_diagnostics.attempts=123; /* Cache stays frozen. */
        if(f)assert(fwrite(a,1,64,f)==64);
    }
    q[8]=T3P_TEMP_DIAG_WORDS/10+1;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
    q[8]=0;q[6]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
#endif
    if(f)fclose(f);
    return 0;
}
