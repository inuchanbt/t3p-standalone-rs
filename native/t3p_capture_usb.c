/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_capture_usb.h"
#include "t3p_capture_store.h"
int32_t t3p_capture_result;
static uint32_t get(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
void t3p_capture_usb_request(const uint8_t q[64],uint8_t a[64])
{
    for(unsigned i=0;i<64;i++)a[i]=0;
    a[0]='T';a[1]='3';a[2]='C';a[3]='R';a[4]=1;a[7]=2;
    uint32_t at=get(q+8);put(a+8,at);put(a+16,get(q+12));
    unsigned bad=q[0]!='T'||q[1]!='3'||q[2]!='C'||q[3]!='Q'||q[5]||q[7];
    for(unsigned i=16;i<64;i++)bad|=q[i];
    if(bad||q[4]>1)a[5]=1;
    else if(!q[4]){
        if(q[6]||at)a[5]=1;
        else {
            uint32_t info[]={T3P_CAPTURE_START,T3P_CAPTURE_BYTES,T3P_CAPTURE_SLOT,6,
                t3p_capture_status,t3p_capture_jedec,t3p_capture_sfdp,(uint32_t)t3p_capture_result,0,0};
            for(unsigned i=0;i<10;i++)put(a+24+4*i,info[i]);a[6]=40;
        }
    }else if(t3p_capture_status)a[5]=2;
    else if(!q[6]||q[6]>40||at>T3P_CAPTURE_BYTES||q[6]>T3P_CAPTURE_BYTES-at)a[5]=3;
    else if(t3p_capture_store_export_read(at,a+24,q[6])){a[5]=4;for(unsigned i=24;i<64;i++)a[i]=0;}
    else a[6]=q[6];
    uint32_t c=~0u;for(unsigned i=0;i<64;i++){c^=a[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}put(a+12,~c);
}
