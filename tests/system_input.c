/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#define T3P_MEASURE 1
#define T3P_SYSTEM 1
static void t3p_board_start(void) __attribute__((unused));
#include "../native/board.c"

static unsigned pressed, advances, holds, doubles;
static uint32_t read_key(void *context, uint32_t address)
{ (void)context; assert(address == PA_BASE); return pressed ? 0 : PA11; }
void meter_next_page(void) { ++advances; }
void meter_long_press(void) { ++holds; }
uint32_t meter_double_press(void) { ++doubles; return 0; }

static void down(uint32_t now)
{
    pressed = 1;
    assert(!poll_key(now));
    assert(!poll_key(now + 30));
}
static void up(uint32_t now)
{
    pressed = 0;
    assert(!poll_key(now));
    assert(!poll_key(now + 30));
}

int main(void)
{
    board.io.read32 = read_key;
    t3p_button_init(&button, 0, 0);
    down(100); up(160);
    assert(poll_key(540) == REDRAW);
    assert(system_stats.short_presses == 1 && advances == 1);
    assert(system_stats.key_event == T3P_BUTTON_SHORT && system_stats.key_ms == 380);
    down(700); up(760); down(900);
    pressed = 0;
    assert(!poll_key(960));
    assert(poll_key(990) == REDRAW);
    assert(system_stats.double_presses == 1 && doubles == 1);
    assert(system_stats.key_event == T3P_BUTTON_DOUBLE && system_stats.key_ms == 30);
    down(1200);
    assert(poll_key(2230) == REDRAW);
    assert(system_stats.long_presses == 1 && holds == 1);
    assert(system_stats.key_event == T3P_BUTTON_LONG && system_stats.key_ms == 1030);
    up(2300);
    assert(!poll_key(3000));
    assert(system_stats.short_presses == 1 && system_stats.double_presses == 1);
    assert(system_stats.uptime_low == 3000 && !system_stats.uptime_high);
    return 0;
}
