/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_boot.h"
#include "t3p_bsp.h"
#include "t3p_signal.h"
#include "t3p_button.h"
#ifdef T3P_MEASURE
#include "t3p_meter.h"
#endif
#ifdef T3P_PD_PROFILES
#include "t3p_pd.h"
#endif

#define PA_BASE 0xfd3000u
#define PA11 (1u << 11)
#define PANEL_SIDE 240u
#define STRIP_ROWS 8u

void meter_demo_sample(uint32_t tick);
void meter_live_sample(uint32_t tick, uint32_t valid, uint32_t mv, int32_t ma);
void meter_pd_diagnostic(uint32_t valid, uint32_t first_four, uint32_t last);
void meter_pd_status(uint32_t state, uint32_t cc);
void meter_reset_totals(void);
void meter_next_page(void);
uint32_t meter_double_press(void);
void meter_snapshot(void);
void meter_demo_render(uint32_t top, uint16_t pixels[PANEL_SIDE * STRIP_ROWS]);

static struct t3p_bsp board;
static uint16_t strip[PANEL_SIDE * STRIP_ROWS];
static volatile uint32_t startup_cookie = 0x54335044u;
static struct t3p_button button;

static void init_key(void)
{
    /* PA11 active-low input, matching the stock loader and native DOOM. */
    static const uint8_t offsets[] = {0x0c, 0x08, 0x14, 0x18, 0x1c, 0x20};
    for (unsigned i = 0; i < sizeof(offsets); ++i) {
        uint32_t addr = PA_BASE + offsets[i];
        uint32_t value = board.io.read32(board.io.context, addr);
        board.io.write32(board.io.context, addr, i < 3 ? value | PA11 : value & ~PA11);
    }
}

static unsigned key_pressed(void)
{
    return !(board.io.read32(board.io.context, PA_BASE) & PA11);
}

enum redraw_request { REDRAW = 1u, START_CAPTURE = 2u };

static unsigned poll_key(uint32_t now)
{
    enum t3p_button_event event = t3p_button_poll(&button, now, key_pressed());
    if (event == T3P_BUTTON_SHORT) { meter_next_page(); return REDRAW; }
    if (event == T3P_BUTTON_DOUBLE) return meter_double_press() ? START_CAPTURE : REDRAW;
    if (event == T3P_BUTTON_LONG) { meter_reset_totals(); return REDRAW; }
    return 0;
}

#ifdef T3P_PD_PROFILES
static unsigned poll_pd(struct t3p_pd_capture *pd, struct t3p_meter_bus *bus, uint32_t now)
{
    enum t3p_pd_state before = pd->state;
    t3p_pd_poll_capture(pd, bus, now);
    if (pd->state == before) return 0;
    meter_pd_status(pd->state, pd->cc);
    return REDRAW;
}
#endif

void t3p_board_start(void)
{
    struct t3p_bsp_io io;
    if (t3p_boot_snapshot.valid != T3P_BOOT_MAGIC || startup_cookie != 0x54335044u) {
        t3p_signal_failure(1);
    }
    t3p_bsp_native_io(&io);
    if (t3p_bsp_init(&board, &io)) t3p_signal_failure(2);
    if (t3p_bsp_lcd_init(&board)) t3p_signal_failure(3);
    init_key();

    uint32_t start = t3p_bsp_millis(&board);
    uint32_t sampled = start - 100;
    uint32_t rendered = start - 1000;
#ifdef T3P_MEASURE
    struct t3p_meter_bus meter_bus;
    t3p_meter_gpio_init(&board, &meter_bus);
    int identified = 0;
    uint32_t identified_at = start - 1000;
#ifdef T3P_PD_PROFILES
    struct t3p_meter_bus fast_bus = meter_bus;
    struct t3p_pd_capture pd = {0};
    if (t3p_meter_fast_bus(&board, &fast_bus)) pd.state = T3P_PD_TIMER_ERROR;
    meter_pd_status(pd.state, 0);
#endif
#ifdef T3P_PD_DIAGNOSTIC
    uint32_t pd_at = start - 2000;
#endif
#endif
    t3p_button_init(&button, start, key_pressed());
    unsigned redraw = 0;
    for (;;) {
        uint32_t now = t3p_bsp_millis(&board);
        redraw |= poll_key(now);
#ifdef T3P_PD_PROFILES
        if (redraw & START_CAPTURE) {
            t3p_pd_start_capture(&pd, &fast_bus, now);
            meter_pd_status(pd.state, pd.cc);
            redraw = REDRAW;
        }
        redraw |= poll_pd(&pd, &fast_bus, now);
#endif
#ifdef T3P_PD_DIAGNOSTIC
        if (now - pd_at >= 2000) {
            static const uint8_t registers[] = {0x01, 0x02, 0x08, 0x0b, 0x40};
            uint8_t values[sizeof(registers)] = {0};
            int valid = 1;
            for (unsigned i = 0; i < sizeof(registers); ++i) {
                if (t3p_pd_read_register(&meter_bus, registers[i], &values[i])) {
                    valid = 0;
                    break;
                }
            }
            uint32_t first_four = (uint32_t)values[0] | (uint32_t)values[1] << 8 |
                                  (uint32_t)values[2] << 16 | (uint32_t)values[3] << 24;
            meter_pd_diagnostic(valid, first_four, values[4]);
            pd_at = now;
        }
#endif
        if (now - sampled >= 100) {
#ifdef T3P_MEASURE
            uint16_t mv = 0;
            int16_t ma = 0;
            struct t3p_meter_bus *sample_bus = &meter_bus;
#ifdef T3P_PD_PROFILES
            if (pd.state == T3P_PD_LISTENING) sample_bus = &fast_bus;
#endif
            if (!identified && now - identified_at >= 1000) {
                identified = !t3p_meter_identify(sample_bus);
                identified_at = now;
            }
            int valid = identified && !t3p_meter_sample(sample_bus, &mv, &ma);
            if (!valid) identified = 0;
            meter_live_sample(t3p_bsp_millis(&board) - start, valid, mv, ma);
#else
            meter_demo_sample(now - start);
#endif
            sampled = now;
        }
        if (redraw || now - rendered >= 1000) {
            meter_snapshot();
            redraw = 0;
            for (uint32_t top = 0; top < PANEL_SIDE; top += STRIP_ROWS) {
                meter_demo_render(top, strip);
                if (t3p_bsp_rectangle(&board, 0, PANEL_SIDE - 1, top, top + STRIP_ROWS - 1,
                                      strip, PANEL_SIDE * STRIP_ROWS)) {
                    t3p_signal_failure(4);
                }
                uint32_t between_strips = t3p_bsp_millis(&board);
                redraw |= poll_key(between_strips);
#ifdef T3P_PD_PROFILES
                redraw |= poll_pd(&pd, &fast_bus, between_strips);
#endif
            }
            t3p_bsp_backlight(&board, 1);
            rendered = now;
        }
        if (t3p_bsp_delay(&board, 5)) t3p_signal_failure(5);
    }
}
