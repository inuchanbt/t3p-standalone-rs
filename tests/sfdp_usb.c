#define T3P_CAPTURE_SFDP_PROBE 1
#include <assert.h>
#include <string.h>
#include "../native/t3p_capture_usb.c"
uint32_t t3p_capture_status=2,t3p_capture_jedec=0x856015,t3p_capture_sfdp=0x50444653;
uint32_t t3p_capture_diagnostics[10]={9};
static unsigned reads,partition_reads;
int t3p_capture_store_export_read(uint32_t at,uint8_t *p,uint32_t n)
{(void)at;(void)p;(void)n;partition_reads++;return -1;}
void t3p_capture_sfdp_info(uint32_t info[10])
{memset(info,0,40);info[0]=0x31444653;info[1]=4096;info[3]=2;info[4]=1;info[5]=0x856015;}
int t3p_capture_sfdp_read(uint32_t at,uint8_t *p,uint32_t n)
{reads++;for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(at+i);return 0;}
int main(void)
{
    uint8_t q[64]={'T','3','C','Q',4},a[64];
    t3p_capture_usb_request(q,a);assert(!a[5]&&a[6]==40&&get(a+24)==0x31444653&&!reads);
    q[6]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&!reads);
    q[6]=0;q[8]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&!reads);
    q[4]=3;q[6]=40;t3p_capture_usb_request(q,a);assert(!a[5]&&a[6]==40&&reads==1);
    for(unsigned i=0;i<40;i++)assert(a[24+i]==i+1);
    q[6]=0;t3p_capture_usb_request(q,a);assert(a[5]==3&&reads==1);
    q[6]=41;t3p_capture_usb_request(q,a);assert(a[5]==3&&reads==1);
    q[6]=2;q[8]=255;q[9]=15;t3p_capture_usb_request(q,a);assert(a[5]==3&&reads==1);
    memset(q+8,255,4);t3p_capture_usb_request(q,a);assert(a[5]==3&&reads==1);
    memset(q+8,0,4);q[6]=1;q[16]=1;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
    q[16]=0;q[4]=5;t3p_capture_usb_request(q,a);assert(a[5]==1&&reads==1);
    q[4]=1;t3p_capture_usb_request(q,a);assert(a[5]==2&&!partition_reads);
    return 0;
}
