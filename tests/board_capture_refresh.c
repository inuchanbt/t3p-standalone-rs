/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>

#define T3P_MEASURE 1
#define T3P_PD_PROFILES 1
#include "../native/board.c"

static uint32_t clock_ms, capture_at, requested_action;
static unsigned capturing, finished, listening_snapshots, finished_snapshots;
static unsigned measurement_samples, key_page_changes, pd_polls;
static jmp_buf done;
struct t3p_boot_snapshot t3p_boot_snapshot = { .valid = T3P_BOOT_MAGIC };

static uint32_t read_pin(void *context, uint32_t address)
{
    (void)context;
    if (address != PA_BASE) return 0;
    /* Double press starts a capture; a later short press changes page. */
    unsigned initial = (clock_ms >= 100 && clock_ms < 160) ||
                       (clock_ms >= 220 && clock_ms < 280);
    unsigned during = capturing && clock_ms - capture_at >= 500 &&
                      clock_ms - capture_at < 560;
    return initial || during ? 0 : PA11;
}

static void write_pin(void *context, uint32_t address, uint32_t value)
{ (void)context; (void)address; (void)value; }

void t3p_bsp_native_io(struct t3p_bsp_io *io)
{ *io = (struct t3p_bsp_io){ .read32 = read_pin, .write32 = write_pin }; }
int t3p_bsp_init(struct t3p_bsp *b, const struct t3p_bsp_io *io)
{ b->io = *io; return 0; }
int t3p_bsp_lcd_init(struct t3p_bsp *b) { (void)b; return 0; }
uint32_t t3p_bsp_millis(struct t3p_bsp *b) { (void)b; return clock_ms; }
int t3p_bsp_delay(struct t3p_bsp *b, uint32_t ms)
{ (void)b; clock_ms += ms; return 0; }
int t3p_bsp_rectangle(void *context, uint16_t xs, uint16_t xe, uint16_t ys, uint16_t ye,
                      const uint16_t *pixels, uint32_t count)
{
    (void)context; (void)xs; (void)xe; (void)ys; (void)ye; (void)pixels; (void)count;
    ++clock_ms;
    return 0;
}
void t3p_bsp_backlight(struct t3p_bsp *b, int enabled) { (void)b; (void)enabled; }
void t3p_signal_failure(unsigned stage) { (void)stage; abort(); }

void t3p_meter_gpio_init(struct t3p_bsp *b, struct t3p_meter_bus *bus)
{ (void)b; *bus = (struct t3p_meter_bus){0}; }
int t3p_meter_fast_bus(struct t3p_bsp *b, struct t3p_meter_bus *bus)
{ (void)b; (void)bus; return 0; }
int t3p_meter_identify(struct t3p_meter_bus *bus) { (void)bus; return 0; }
int t3p_meter_sample(struct t3p_meter_bus *bus, uint16_t *mv, int16_t *ma)
{ (void)bus; *mv = 5000; *ma = 1000; if (capturing) ++measurement_samples; return 0; }

void t3p_pd_start_capture(struct t3p_pd_capture *pd, struct t3p_meter_bus *bus,
                          uint32_t now, enum t3p_pd_mode mode)
{
    (void)bus;
    assert(!capturing);
    capture_at = now;
    capturing = 1;
    pd->mode = mode;
    pd->state = T3P_PD_LISTENING;
}
void t3p_pd_poll_capture(struct t3p_pd_capture *pd, struct t3p_meter_bus *bus, uint32_t now)
{
    (void)bus;
    if (pd->state != T3P_PD_LISTENING) return;
    ++pd_polls;
    if (now - capture_at >= 3200) {
        pd->state = T3P_PD_TIMEOUT;
        capturing = 0;
        finished = 1;
    }
}

void meter_pd_status(uint32_t state, uint32_t cc, uint32_t mode)
{ (void)state; (void)cc; (void)mode; }
void meter_pd_warning(uint32_t warning) { (void)warning; }
void meter_live_sample(uint32_t tick, uint32_t valid, uint32_t mv, int32_t ma)
{ (void)tick; assert(valid && mv == 5000 && ma == 1000); }
void meter_next_page(void) { ++key_page_changes; }
void meter_long_press(void) { abort(); }
uint32_t meter_double_press(void) { return requested_action; }
void meter_demo_render(uint32_t top, uint16_t *pixels) { (void)top; (void)pixels; }
void meter_snapshot(void)
{
    if (capturing) ++listening_snapshots;
    if (finished) { ++finished_snapshots; longjmp(done, 1); }
}

static void verify_capture(unsigned action)
{
    clock_ms = capture_at = 0;
    capturing = finished = listening_snapshots = finished_snapshots = 0;
    measurement_samples = key_page_changes = pd_polls = 0;
    requested_action = action;
    if (!setjmp(done)) t3p_board_start();
    assert(finished_snapshots == 1);
    assert(key_page_changes == 1);  /* Manual navigation is still rendered. */
    assert(measurement_samples >= 30 && pd_polls >= 1000);
    if (action == 1)
        assert(listening_snapshots >= 4);  /* SPR retains periodic refresh. */
    else
        assert(listening_snapshots == 2);  /* Start + manual; none periodic. */
}

int main(void)
{
    verify_capture(1);
    verify_capture(2);
    verify_capture(3);
    return 0;
}
