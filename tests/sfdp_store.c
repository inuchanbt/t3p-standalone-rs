/* Independent SPI/NOR model reused; none of its write tests run in this probe. */
#define T3P_CAPTURE_SFDP_PROBE 1
#define main production_model_main
#include "capture_store.c"
#undef main
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(directory,1,256,f)==256);assert(fread(reserved_header,1,32,f)==32);fclose(f);
    uint32_t info[10];uint8_t check[4096];
    legacy_setup();
    assert(t3p_capture_store_init(&model_clock)==-1);
    assert(t3p_capture_status==2&&t3p_capture_diagnostics[0]==12);
    assert(sfdp_transactions==19); /* header + full read + 16 verify reads + BFPT */
    t3p_capture_sfdp_info(info);
    assert(info[0]==0x31444653&&info[1]==4096&&info[2]==crc(sfdp_data,4096));
    assert(info[3]==2&&info[4]==1&&info[5]==0x856015);
    for(unsigned i=6;i<10;i++)assert(!info[i]);
    unsigned before=transfers;
    assert(!t3p_capture_sfdp_read(0,check,sizeof check)&&!memcmp(check,sfdp_data,sizeof check));
    assert(t3p_capture_sfdp_read(4095,check,2));
    assert(t3p_capture_sfdp_read(UINT32_MAX,check,1));
    assert(t3p_capture_sfdp_read(0,check,0));
    /* Even a corrupted/forged ready status cannot enable a mutating callback. */
    t3p_capture_status=0;
    assert(t3p_capture_store_prepare()==-1);
    assert(t3p_capture_store_program(0,check,1)==-1);
    assert(t3p_capture_store_flush()==-1);
    assert(transfers==before&&!enables&&!erases);
    legacy_setup();memcpy(sfdp_data+0x30,t3p_856015_bfpt,sizeof t3p_856015_bfpt);
    assert(t3p_capture_store_init(&model_clock)==-1&&t3p_capture_diagnostics[0]==12);
    t3p_capture_sfdp_info(info);assert(info[1]==4096&&info[3]==2&&!enables&&!erases);
    legacy_setup();sfdp_data[0x38]^=1;
    assert(t3p_capture_store_init(&model_clock)==-1&&t3p_capture_diagnostics[0]==9);
    t3p_capture_sfdp_info(info);assert(info[1]==4096&&info[3]==2);
    legacy_setup();change_after=2;
    assert(t3p_capture_store_init(&model_clock)==-1&&t3p_capture_diagnostics[0]==11);
    assert(t3p_capture_diagnostics[1]==4);
    t3p_capture_sfdp_info(info);assert(!info[1]&&!info[2]&&!info[3]);
    assert(t3p_capture_sfdp_read(0,check,1));
    legacy_setup();fail_after=1;
    assert(t3p_capture_store_init(&model_clock)==-1&&t3p_capture_diagnostics[0]==10);
    assert(t3p_capture_diagnostics[1]==3);
    t3p_capture_sfdp_info(info);assert(!info[1]&&!info[2]&&!info[3]);
    legacy_setup();fail_after=2;
    assert(t3p_capture_store_init(&model_clock)==-1&&t3p_capture_diagnostics[0]==11);
    assert(t3p_capture_diagnostics[1]==3);
    t3p_capture_sfdp_info(info);assert(!info[1]);
    assert(!enables&&!erases);
    puts("Read-only SFDP: two independent SPI passes, mismatch/timeouts, bounds and forced-ready refusal passed");
    return 0;
}
