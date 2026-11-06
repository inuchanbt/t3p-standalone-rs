/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native/t3p_button.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    struct t3p_button b;
    t3p_button_init(&b, 0, 0);
    assert(t3p_button_poll(&b, 10, 1) == T3P_BUTTON_NONE);
    assert(t3p_button_poll(&b, 40, 1) == T3P_BUTTON_NONE);
    assert(t3p_button_poll(&b, 140, 0) == T3P_BUTTON_NONE);
    assert(t3p_button_poll(&b, 170, 0) == T3P_BUTTON_SHORT);
    assert(t3p_button_poll(&b, 200, 1) == T3P_BUTTON_NONE);
    assert(t3p_button_poll(&b, 230, 1) == T3P_BUTTON_NONE);
    assert(t3p_button_poll(&b, 1230, 1) == T3P_BUTTON_LONG);
    assert(t3p_button_poll(&b, 1500, 1) == T3P_BUTTON_NONE);
    assert(t3p_button_poll(&b, 1600, 0) == T3P_BUTTON_NONE);
    assert(t3p_button_poll(&b, 1630, 0) == T3P_BUTTON_NONE);
    t3p_button_init(&b, UINT32_MAX - 100u, 0);
    assert(t3p_button_poll(&b, UINT32_MAX - 90u, 1) == T3P_BUTTON_NONE);
    assert(t3p_button_poll(&b, UINT32_MAX - 60u, 1) == T3P_BUTTON_NONE);
    assert(t3p_button_poll(&b, 939, 1) == T3P_BUTTON_LONG);
    t3p_button_init(&b, 0, 1);
    assert(t3p_button_poll(&b, 50, 0) == T3P_BUTTON_NONE);
    assert(t3p_button_poll(&b, 80, 0) == T3P_BUTTON_NONE);
    puts("button tests passed");
}
