/* SPI/NOR model checks; no timings from this executable are silicon timings. */
#define main legacy_model_main
#include "capture_store.c"
#undef main
static void initialize(void)
{
    legacy_setup();memcpy(sfdp_data+0x30,t3p_856015_bfpt,sizeof t3p_856015_bfpt);
    assert(!t3p_capture_store_init(&model_clock));
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(directory,1,256,f)==256&&fread(reserved_header,1,32,f)==32);fclose(f);
    uint32_t p[10];uint8_t value=0x42;
    (void)value;
    initialize();assert(t3p_capture_store_take_blank()==-1);
    t3p_capture_profile_begin();t3p_capture_profile_phase(CAP_PREPARE);
    assert(!t3p_capture_store_prepare());assert(erases==32);
    t3p_capture_profile_phase(CAP_JOURNAL);
#ifdef T3P_CAPTURE_TIMING_BASELINE
    assert(t3p_capture_store_take_blank()==-1);
#else
    assert(t3p_capture_store_take_blank()==0);
    assert(t3p_capture_store_take_blank()==-1);
#endif
    t3p_capture_profile_end(-1);assert(!t3p_capture_profile_page(1,p));
    assert(p[5]==32&&p[7]==T3P_CAPTURE_SLOT);
    assert(!t3p_capture_profile_page(2,p));
#ifdef T3P_CAPTURE_TIMING_BASELINE
    assert(p[1]==512&&p[3]==0);
#else
    assert(p[1]==32&&p[3]==1);
    initialize();assert(!t3p_capture_store_prepare());
    assert(!t3p_capture_store_program(0,&value,1));
    assert(t3p_capture_store_take_blank()==-2); /* buffered mutation */
    assert(!t3p_capture_store_flush()&&t3p_capture_store_take_blank()==-1);
    initialize();assert(!t3p_capture_store_prepare());
    assert(t3p_capture_store_program(T3P_CAPTURE_BYTES,&value,1));
    assert(t3p_capture_store_take_blank()==-1); /* rejected mutation also revokes */
    initialize();assert(!t3p_capture_store_prepare());t3p_capture_status=3;
    assert(t3p_capture_store_take_blank()==-2);t3p_capture_status=0;
    assert(t3p_capture_store_take_blank()==-1);
#endif
    initialize();protected_flash=1;assert(t3p_capture_store_prepare());
    assert(t3p_capture_store_take_blank()==-2);
    /* Incomplete erasure at transfer/sector/slot edges cannot authorize a save. */
    const unsigned faults[]={0,255,256,4095,4096,65535,131070,131071};
    for(unsigned i=0;i<sizeof faults/sizeof faults[0];i++){
        initialize();erase_stuck_at=T3P_CAPTURE_START+faults[i];
        assert(t3p_capture_store_prepare()&&t3p_capture_status==3);
        assert(erases==faults[i]/4096+1&&t3p_capture_store_take_blank()==-2);
        for(unsigned j=0;j<sizeof page_programs/sizeof page_programs[0];j++)assert(!page_programs[j]);
    }
    initialize();assert(t3p_capture_store_take_blank()==-1); /* reboot revokes */
    puts("Verified-blank proof, invalidation, streaming checks and protected-flash refusal passed");
    return 0;
}
