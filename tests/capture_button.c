/* SPDX-License-Identifier: GPL-2.0-or-later */
#define T3P_CAPTURE_DEVICE 1
#include "../native/t3p_button.h"
#include <assert.h>
static void none(struct t3p_button *b,uint32_t t,unsigned p){assert(t3p_button_poll(b,t,p)==0);}
int main(void)
{
    struct t3p_button b;
    t3p_button_init(&b,0,0);none(&b,10,1);none(&b,40,1);none(&b,1040,1);
    none(&b,1200,0);assert(t3p_button_poll(&b,1230,0)==T3P_BUTTON_LONG);none(&b,4000,0);
    t3p_button_init(&b,0,0);none(&b,10,1);none(&b,40,1);none(&b,3039,1);
    assert(t3p_button_poll(&b,3040,1)==T3P_BUTTON_SAVE);none(&b,4000,1);none(&b,4100,0);none(&b,4130,0);none(&b,5000,0);
    t3p_button_init(&b,0,1);none(&b,5000,1);none(&b,5010,0);none(&b,5040,0);none(&b,6000,0);
    /* Millisecond wrap does not turn a hold into a short/double press. */
    t3p_button_init(&b,UINT32_MAX-100,0);none(&b,UINT32_MAX-90,1);none(&b,UINT32_MAX-60,1);
    assert(t3p_button_poll(&b,2939,1)==T3P_BUTTON_SAVE);
    return 0;
}
