/* Model loss of volatile page buffers immediately before the final commit. */
#define main legacy_model_main
#include "capture_store.c"
#undef main
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(directory,1,256,f)==256&&fread(reserved_header,1,32,f)==32);fclose(f);
    legacy_setup();memcpy(sfdp_data+0x30,t3p_856015_bfpt,sizeof t3p_856015_bfpt);
    /* A distinct occupied slot must survive every simulated restart. */
    memset(flash+T3P_CAPTURE_START+5*T3P_CAPTURE_SLOT,0x91,T3P_CAPTURE_SLOT);
    assert(!t3p_capture_store_init(&model_clock)&&!t3p_capture_store_prepare());
    uint8_t h[64],out[64];memset(h,255,sizeof h);memset(h,0,32);
    memcpy(h,"T3PSCR01",8);h[8]=1;memcpy(h+10,"SCRN",4);
    const uint8_t payload[]={0xff,0xff,0x34,0x12,0x01,0xe1,0x34,0x12};
    assert(!t3p_capture_store_program(0,h,32));
    assert(!t3p_capture_store_init(&model_clock)); /* queued header is lost */
    assert(!t3p_capture_store_export_read(0,out,64));
    for(unsigned i=0;i<64;i++)assert(out[i]==255);
    assert(!t3p_capture_store_program(0,h,32));
    assert(!t3p_capture_store_program(64,payload,sizeof payload));
    assert(!t3p_capture_store_read(64,out,sizeof payload)&&!memcmp(out,payload,sizeof payload));
    h[32]=sizeof payload;h[33]=h[34]=h[35]=0;
    uint32_t sum=crc(payload,sizeof payload);
    for(unsigned i=0;i<4;i++)h[36+i]=(uint8_t)(sum>>(8*i));
    sum=crc(h,40);for(unsigned i=0;i<4;i++)h[40+i]=(uint8_t)(sum>>(8*i));
    assert(!t3p_capture_store_program(32,h+32,12));
    assert(!t3p_capture_store_read(0,out,44)&&!memcmp(out,h,44));
    assert(!t3p_capture_store_program(60,(const uint8_t *)"DONE",4));
    assert(!t3p_capture_store_init(&model_clock)); /* commit not durable yet */
    assert(!t3p_capture_store_export_read(0,out,64)&&memcmp(out+60,"DONE",4));
    assert(!t3p_capture_store_program(60,(const uint8_t *)"DONE",4));
    assert(!t3p_capture_store_read(60,out,4)&&!memcmp(out,"DONE",4));
    assert(!t3p_capture_store_init(&model_clock));
    assert(!t3p_capture_store_export_read(0,out,64)&&!memcmp(out,h,44)&&!memcmp(out+60,"DONE",4));
    assert(!t3p_capture_store_export_read(64,out,sizeof payload)&&!memcmp(out,payload,sizeof payload));
    for(unsigned i=0;i<T3P_CAPTURE_SLOT;i++)assert(flash[T3P_CAPTURE_START+5*T3P_CAPTURE_SLOT+i]==0x91);
    puts("Buffered header/commit power loss and durable read barriers preserve occupied slots");
    return 0;
}
