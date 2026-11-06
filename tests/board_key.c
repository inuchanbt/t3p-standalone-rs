/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include "../native/board.c"

static uint32_t level = PA11;
static unsigned resets, advances, doubles;
static unsigned rectangles, snapshots;
static uint32_t clock_ms;
static jmp_buf done;
struct t3p_boot_snapshot t3p_boot_snapshot = { .valid = T3P_BOOT_MAGIC };

static uint32_t read_pin(void *context, uint32_t address)
{
    (void)context;
    return address == PA_BASE ? level : 0;
}

static void write_pin(void *context, uint32_t address, uint32_t value)
{
    (void)context; (void)address; (void)value;
}

void meter_reset_totals(void)
{
    ++resets;
}

void meter_next_page(void) { ++advances; }
uint32_t meter_double_press(void) { ++doubles; return 1; }
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

int main(void)
{
    board.io.read32 = read_pin;
    t3p_button_init(&button, 0, key_pressed());
    level = 0;
    assert(!poll_key(100));
    assert(!poll_key(130));
    level = PA11;
    assert(!poll_key(150));
    assert(!poll_key(180));
    level = 0;
    assert(!poll_key(210));
    assert(!poll_key(240));
    level = PA11;
    assert(!poll_key(260));
    assert(poll_key(290) == START_CAPTURE);
    assert(doubles == 1 && advances == 0 && resets == 0);

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
