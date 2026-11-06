/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include "../native/board.c"

static uint32_t level = PA11;
static unsigned resets;
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

void meter_demo_sample(uint32_t tick) { (void)tick; }
void meter_snapshot(void)
{
    if (++snapshots == 2) longjmp(done, 1);
}
void meter_demo_render(uint32_t top, uint16_t *pixels) { (void)top; (void)pixels; }
void t3p_bsp_native_io(struct t3p_bsp_io *io) { io->read32 = read_pin; io->write32 = write_pin; }
int t3p_bsp_init(struct t3p_bsp *b, const struct t3p_bsp_io *io) { b->io = *io; return 0; }
int t3p_bsp_lcd_init(struct t3p_bsp *b) { (void)b; return 0; }
uint32_t t3p_bsp_millis(struct t3p_bsp *b) { (void)b; return clock_ms += 10; }
int t3p_bsp_delay(struct t3p_bsp *b, uint32_t ms) { (void)b; clock_ms += ms; return 0; }
void t3p_bsp_backlight(struct t3p_bsp *b, int enabled) { (void)b; (void)enabled; }
void t3p_signal_failure(unsigned stage) { (void)stage; abort(); }
int t3p_bsp_rectangle(void *context, uint16_t xs, uint16_t xe, uint16_t ys, uint16_t ye,
                      const uint16_t *pixels, uint32_t count)
{
    (void)context; (void)xs; (void)xe; (void)ys; (void)ye; (void)pixels; (void)count;
    ++rectangles;
    if (rectangles == 1) level = 0;
    if (rectangles == 6) level = PA11;
    return 0;
}

int main(void)
{
    board.io.read32 = read_pin;
    raw_key = stable_key = 0;
    key_changed = 0;

    /* A press begins and ends while consecutive strips are being sent. */
    level = 0;
    assert(!poll_key(100));
    assert(!poll_key(120));
    assert(poll_key(130));
    assert(!poll_key(140));
    assert(resets == 1);

    level = PA11;
    assert(!poll_key(145));
    assert(!poll_key(175));
    level = 0;
    assert(!poll_key(180));
    level = PA11;
    assert(!poll_key(190)); /* bounce is shorter than 30 ms */
    assert(!poll_key(220));
    assert(resets == 1);

    /* Debouncing also works across the 32-bit timer wrap. */
    level = 0;
    assert(!poll_key(0xfffffff0u));
    assert(poll_key(14));
    assert(resets == 2);

    /* A press held for five strips resets the totals and requests a new
       snapshot even though it ends before the first frame has finished. */
    level = PA11;
    resets = rectangles = snapshots = clock_ms = 0;
    if (!setjmp(done)) t3p_board_start();
    assert(resets == 1);
    assert(rectangles == 30);
    assert(snapshots == 2);
    return 0;
}
