/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "../native/t3p_capture_profile.c"
static uint32_t model_ms,clock_reads;
uint32_t t3p_bsp_millis(struct t3p_bsp *b){(void)b;clock_reads++;return model_ms;}
int main(void)
{
    struct t3p_bsp b={0};uint32_t p[10];
    t3p_capture_profile_init(&b);assert(!t3p_capture_profile_page(0,p)&&p[0]==T3P_CAPTURE_PROFILE_MAGIC&&!p[1]);
    assert(!clock_reads);model_ms=100;t3p_capture_profile_begin();
    model_ms=103;t3p_capture_profile_phase(CAP_PREPARE);
    model_ms=113;t3p_capture_profile_phase(CAP_JOURNAL);
    model_ms=117;t3p_capture_profile_phase(CAP_RENDER);
    model_ms=123;t3p_capture_profile_phase(CAP_ENCODE);
    model_ms=153;t3p_capture_profile_phase(CAP_FINISH);
    model_ms=163;t3p_capture_profile_phase(CAP_NOTICE);
    t3p_capture_profile_add(CAP_PROGRAM_OPS,74);t3p_capture_profile_add(999,1);
    unsigned before=clock_reads;assert(!t3p_capture_profile_page(0,p)&&!p[1]);assert(clock_reads==before);
    t3p_capture_profile_end(1);assert(!t3p_capture_profile_page(0,p));
    assert(p[1]==1&&p[2]==1&&p[3]==63&&p[4]==3&&p[5]==10&&p[6]==4&&p[7]==6&&p[8]==30&&p[9]==10);
    assert(!t3p_capture_profile_page(1,p)&&p[6]==74);
    assert(!t3p_capture_profile_page(2,p)&&p[6]==1&&p[8]==1&&p[9]==1);
    before=clock_reads;assert(t3p_capture_profile_page(3,p));assert(clock_reads==before);
    model_ms=0xfffffff0u;t3p_capture_profile_begin();
    assert(!t3p_capture_profile_page(0,p)&&p[1]==1); /* previous completed attempt */
    model_ms=5;t3p_capture_profile_phase(CAP_PREPARE);
    model_ms=10;t3p_capture_profile_end(-1);
    assert(!t3p_capture_profile_page(0,p)&&p[1]==2&&p[2]==0xffffffffu&&p[3]==26&&p[4]==21&&p[5]==5);
    t3p_capture_profile_init(&b);assert(!t3p_capture_profile_page(0,p)&&!p[1]);
    before=clock_reads;t3p_capture_profile_init(0);t3p_capture_profile_begin();
    t3p_capture_profile_phase(CAP_FINISH);t3p_capture_profile_end(-1);
    assert(!t3p_capture_profile_page(0,p)&&p[1]==1&&p[3]==0);
    assert(!t3p_capture_profile_page(2,p)&&!(p[7]&0x100)&&p[8]==1);
    assert(clock_reads==before); /* no clock means unavailable, not a measured 0 ms */
    return 0;
}
