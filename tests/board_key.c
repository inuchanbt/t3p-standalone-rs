/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include "../native/board.c"

static uint32_t level = PA11;
static unsigned resets, advances, doubles;
static unsigned rectangles, snapshots;
static uint32_t double_action = 1;
static uint32_t clock_ms;
static jmp_buf done;
struct t3p_boot_snapshot t3p_boot_snapshot = { .valid = T3P_BOOT_MAGIC };

#ifdef T3P_PD_RD_TEST
static uint32_t rd_page;
static uint8_t switches = T3P_RD_DEFAULT;

uint32_t meter_on_rd_test_page(void) { return rd_page; }
void meter_rd_test(uint32_t state, uint32_t off_mv, uint32_t on_mv)
{
    (void)state; (void)off_mv; (void)on_mv;
}
void meter_live_sample(uint32_t tick, uint32_t valid, uint32_t mv, int32_t ma)
{
    (void)tick; (void)valid; (void)mv; (void)ma;
}
void t3p_meter_gpio_init(struct t3p_bsp *b, struct t3p_meter_bus *bus)
{
    (void)b;
    *bus = (struct t3p_meter_bus){0};
}
int t3p_meter_identify(struct t3p_meter_bus *bus) { (void)bus; return 0; }
int t3p_meter_sample(struct t3p_meter_bus *bus, uint16_t *mv, int16_t *ma)
{
    (void)bus;
    *mv = switches == T3P_RD_CC1_ON ? 5094 : 3010;
    *ma = 0;
    return 0;
}
int t3p_pd_rd_baseline(struct t3p_meter_bus *bus)
{
    (void)bus;
    return switches != T3P_RD_DEFAULT;
}
int t3p_pd_set_rd(struct t3p_meter_bus *bus, uint8_t value)
{
    (void)bus;
    switches = value;
    return 0;
}
#endif

static uint32_t read_pin(void *context, uint32_t address)
{
    (void)context;
    return address == PA_BASE ? level : 0;
}

static void write_pin(void *context, uint32_t address, uint32_t value)
{
    (void)context; (void)address; (void)value;
}

void meter_long_press(void)
{
    ++resets;
}

void meter_next_page(void)
{
    ++advances;
#ifdef T3P_PD_RD_TEST
    rd_page = 0;
#endif
}
uint32_t meter_double_press(void) { ++doubles; return double_action; }
void meter_demo_sample(uint32_t tick) { (void)tick; }
void meter_snapshot(void)
{
    if (++snapshots == 2) longjmp(done, 1);
}
void meter_demo_render(uint32_t top, uint16_t *pixels) { (void)top; (void)pixels; }
void t3p_bsp_native_io(struct t3p_bsp_io *io) { io->read32 = read_pin; io->write32 = write_pin; }
int t3p_bsp_init(struct t3p_bsp *b, const struct t3p_bsp_io *io) { b->io = *io; return 0; }
int t3p_bsp_lcd_init(struct t3p_bsp *b) { (void)b; return 0; }
uint32_t t3p_bsp_millis(struct t3p_bsp *b) { (void)b; return clock_ms += 50; }
int t3p_bsp_delay(struct t3p_bsp *b, uint32_t ms) { (void)b; clock_ms += ms; return 0; }
void t3p_bsp_backlight(struct t3p_bsp *b, int enabled) { (void)b; (void)enabled; }
void t3p_signal_failure(unsigned stage) { (void)stage; abort(); }
int t3p_bsp_rectangle(void *context, uint16_t xs, uint16_t xe, uint16_t ys, uint16_t ye,
                      const uint16_t *pixels, uint32_t count)
{
    (void)context; (void)xs; (void)xe; (void)ys; (void)ye; (void)pixels; (void)count;
    ++rectangles;
    if (rectangles == 1) level = 0;
    if (rectangles == 26) level = PA11;
    return 0;
}

static unsigned tap(uint32_t start)
{
    level = 0;
    assert(!poll_key(start));
    assert(!poll_key(start + 30));
    level = PA11;
    assert(!poll_key(start + 50));
    return poll_key(start + 80);
}

#ifdef T3P_PD_RD_TEST
static void test_rd_buttons(void)
{
    struct t3p_meter_bus bus = {0};
    level = PA11;
    t3p_button_init(&button, 0, key_pressed());
    rd_page = 1;
    assert(!tap(100));
    assert(tap(210) == RD_STEP);
    t3p_rd_test_step(&rd_test, &bus, 290);
    assert(rd_test.state == T3P_RD_OFF && switches == T3P_RD_CC1_OFF);

    assert(!tap(2300));
    assert(poll_key(2730) == RD_STEP);
    t3p_rd_test_step(&rd_test, &bus, 2730);
    assert(rd_test.state == T3P_RD_ON && switches == T3P_RD_CC1_ON);
    t3p_rd_test_poll(&rd_test, &bus, 2830);
    assert(!tap(2840)); /* A second tap outside the double-press window. */
    t3p_rd_test_poll(&rd_test, &bus, 2930);
    t3p_rd_test_poll(&rd_test, &bus, 3030);
    assert(!poll_key(3270));
    assert(rd_test.state == T3P_RD_5V_SEEN && switches == T3P_RD_DEFAULT);
    assert(rd_test.off_mv == 3010 && rd_test.on_mv == 5094);
    assert(rd_page && advances == 0);

    level = 0;
    assert(!poll_key(3300));
    assert(!poll_key(3330));
    assert(poll_key(4330) == REDRAW);
    assert(!rd_page && advances == 1);
}
#endif

int main(void)
{
    board.io.read32 = read_pin;
    t3p_button_init(&button, 0, key_pressed());
    assert(!tap(100));
    assert(tap(210) == START_CAPTURE);
    assert(doubles == 1 && advances == 0 && resets == 0);

    for (unsigned action = 2; action <= 3; ++action) {
        level = PA11;
        double_action = action;
        t3p_button_init(&button, 0, key_pressed());
        assert(!tap(100));
        assert(tap(210) == (action == 2 ? START_EPR : START_SNIFF));
    }

#ifdef T3P_PD_RD_TEST
    test_rd_buttons();
#endif

    /* A long press during strip transfers resets the totals and requests a new
       snapshot even though it ends before the first frame has finished. */
    level = PA11;
    resets = rectangles = snapshots = clock_ms = 0;
    if (!setjmp(done)) t3p_board_start();
    assert(resets == 1);
    assert(rectangles == 30);
    assert(snapshots == 2);
    return 0;
}
