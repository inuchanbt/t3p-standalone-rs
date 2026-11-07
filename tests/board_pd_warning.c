/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#define T3P_MEASURE 1
#define T3P_PD_PROFILES 1
static void t3p_board_start(void);
#include "../native/board.c"

static enum t3p_pd_state next_state;
static unsigned next_warning, status_calls, warning_calls;
static uint32_t last_state, last_warning;

void t3p_pd_poll_capture(struct t3p_pd_capture *pd, struct t3p_meter_bus *bus, uint32_t now)
{
    (void)bus; (void)now;
    pd->state = next_state;
    pd->fifo_warning = next_warning;
}

void meter_pd_status(uint32_t state, uint32_t cc, uint32_t mode)
{
    (void)cc; (void)mode;
    ++status_calls;
    last_state = state;
}

void meter_pd_warning(uint32_t warning)
{
    ++warning_calls;
    last_warning = warning;
}

int main(void)
{
    struct t3p_pd_capture pd = { .state = T3P_PD_LISTENING, .cc = 1, .mode = T3P_PD_EPR };
    struct t3p_meter_bus bus = {0};

    /* A warning during reception must not reset the decoder or request a
       screen redraw; both would interfere with a multi-part PD exchange. */
    next_state = T3P_PD_LISTENING;
    next_warning = 1;
    assert(poll_pd(&pd, &bus, 1) == 0);
    assert(warning_calls == 1 && last_warning == 1 && status_calls == 0);
    assert(poll_pd(&pd, &bus, 2) == 0);
    assert(warning_calls == 1 && status_calls == 0);

    /* Completion still triggers the normal state update and redraw. */
    next_state = T3P_PD_CAPTURED;
    assert(poll_pd(&pd, &bus, 3) == REDRAW);
    assert(status_calls == 1 && last_state == T3P_PD_CAPTURED && warning_calls == 1);
    return 0;
}
