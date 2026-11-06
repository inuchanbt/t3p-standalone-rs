/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native/t3p_button.h"
#include <assert.h>
#include <stdio.h>

static void poll_none(struct t3p_button *b, uint32_t t, unsigned down) {
    assert(t3p_button_poll(b, t, down) == T3P_BUTTON_NONE);
}
static void press(struct t3p_button *b, uint32_t t) {
    poll_none(b, t, 1); poll_none(b, t + 30, 1);
}
static void release(struct t3p_button *b, uint32_t t) {
    poll_none(b, t, 0); poll_none(b, t + 30, 0);
}
int main(void) {
    struct t3p_button b;
    t3p_button_init(&b, 0, 0);
    press(&b, 10); release(&b, 140);
    poll_none(&b, 519, 0);
    assert(t3p_button_poll(&b, 520, 0) == T3P_BUTTON_SHORT);
    poll_none(&b, 600, 0);

    t3p_button_init(&b, 0, 0);
    press(&b, 10); release(&b, 100); press(&b, 200);
    poll_none(&b, 280, 0);
    assert(t3p_button_poll(&b, 310, 0) == T3P_BUTTON_DOUBLE);
    poll_none(&b, 800, 0);

    /* Separated taps remain two short presses. */
    t3p_button_init(&b, 0, 0);
    press(&b, 10); release(&b, 100);
    assert(t3p_button_poll(&b, 480, 0) == T3P_BUTTON_SHORT);
    press(&b, 500); release(&b, 590);
    assert(t3p_button_poll(&b, 970, 0) == T3P_BUTTON_SHORT);

    t3p_button_init(&b, 0, 0);
    press(&b, 10);
    assert(t3p_button_poll(&b, 1040, 1) == T3P_BUTTON_LONG);
    poll_none(&b, 1500, 1); release(&b, 1600); poll_none(&b, 2200, 0);

    /* A second press that becomes a hold resets once, without changing page. */
    t3p_button_init(&b, 0, 0);
    press(&b, 10); release(&b, 100); press(&b, 200);
    assert(t3p_button_poll(&b, 1230, 1) == T3P_BUTTON_LONG);
    release(&b, 1300); poll_none(&b, 1800, 0);

    /* A release before the hold threshold remains short despite debounce. */
    t3p_button_init(&b, 0, 0);
    press(&b, 10); release(&b, 1039);
    assert(t3p_button_poll(&b, 1419, 0) == T3P_BUTTON_SHORT);

    t3p_button_init(&b, UINT32_MAX - 100u, 0);
    press(&b, UINT32_MAX - 90u);
    assert(t3p_button_poll(&b, 939, 1) == T3P_BUTTON_LONG);
    t3p_button_init(&b, UINT32_MAX - 300u, 0);
    press(&b, UINT32_MAX - 290u); release(&b, UINT32_MAX - 200u);
    press(&b, UINT32_MAX - 100u);
    poll_none(&b, UINT32_MAX - 20u, 0);
    assert(t3p_button_poll(&b, 9, 0) == T3P_BUTTON_DOUBLE);
    t3p_button_init(&b, 0, 1);
    release(&b, 50); poll_none(&b, 500, 0);
    puts("button tests passed");
}
