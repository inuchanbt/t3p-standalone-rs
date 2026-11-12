/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../native/t3p_capture_usb.c"
uint32_t t3p_capture_status,t3p_capture_jedec=0xef4015,t3p_capture_sfdp=0x50444653;
uint32_t t3p_capture_diagnostics[10]={3,1,0,0,0,0,0,0,0,0};
static unsigned reads;
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
    if(f){assert(fwrite(a,1,64,f)==64);fclose(f);}
    q[6]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
    q[6]=0;q[8]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
    return 0;
}
