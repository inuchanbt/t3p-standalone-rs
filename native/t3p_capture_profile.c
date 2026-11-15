/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_capture_profile.h"
static struct t3p_bsp *profile_clock;
static uint32_t profile_work[30],profile_published[30]={
    [0]=T3P_CAPTURE_PROFILE_MAGIC,[29]=1};
static uint32_t profile_generation,profile_start,profile_phase_at,profile_phase;
static unsigned profile_active;
static uint32_t profile_options(void)
{
    return (profile_clock?0x100u:0u)
#if !defined(T3P_CAPTURE_TIMING_BASELINE) && !defined(T3P_CAPTURE_SFDP_PROBE)
        |7u /* verified-blank handoff, streamed blank check, full-page buffering */
#endif
        ;
}
void t3p_capture_profile_init(struct t3p_bsp *clock)
{
    profile_clock=clock;profile_active=profile_generation=0;
    for(unsigned i=0;i<30;i++)profile_work[i]=profile_published[i]=0;
    profile_published[0]=T3P_CAPTURE_PROFILE_MAGIC;
    profile_published[27]=profile_options();profile_published[29]=1;
}
uint32_t t3p_capture_profile_stamp(void)
{return profile_active&&profile_clock?t3p_bsp_millis(profile_clock):0;}
void t3p_capture_profile_begin(void)
{
    for(unsigned i=0;i<30;i++)profile_work[i]=0;
    if(!++profile_generation)++profile_generation;
    profile_work[0]=T3P_CAPTURE_PROFILE_MAGIC;profile_work[1]=profile_generation;
    profile_work[27]=profile_options();profile_work[29]=1;profile_active=1;
    profile_start=profile_phase_at=t3p_capture_profile_stamp();profile_phase=CAP_NOTICE;
}
void t3p_capture_profile_phase(uint32_t phase)
{
    if(!profile_active||phase<CAP_NOTICE||phase>CAP_FINISH)return;
    uint32_t now=t3p_capture_profile_stamp();
    profile_work[profile_phase]+=now-profile_phase_at;
    profile_phase=phase;profile_phase_at=now;
    if(phase==CAP_RENDER)profile_work[CAP_RENDER_CALLS]++;
}
void t3p_capture_profile_add(uint32_t metric,uint32_t value)
{if(profile_active&&metric>=CAP_ERASE_MS&&metric<=CAP_RENDER_CALLS)profile_work[metric]+=value;}
void t3p_capture_profile_end(int32_t result)
{
    if(!profile_active)return;
    uint32_t now=t3p_capture_profile_stamp();
    profile_work[profile_phase]+=now-profile_phase_at;
    profile_work[2]=(uint32_t)result;profile_work[3]=now-profile_start;
    profile_work[28]=1;
    for(unsigned i=0;i<30;i++)profile_published[i]=profile_work[i];
    profile_active=0;
}
int t3p_capture_profile_page(uint32_t page,uint32_t out[10])
{
    if(page>=3)return -1;
    for(unsigned i=0;i<10;i++)out[i]=profile_published[page*10+i];
    return 0;
}
