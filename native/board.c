/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_boot.h"
#include "t3p_bsp.h"
#include "t3p_signal.h"
#include "t3p_button.h"
#ifdef T3P_FLASH_READER
#include "t3p_flash_reader.h"
#endif
#ifdef T3P_CAPTURE_PROBE
#include "t3p_capture_probe.h"
#endif
#ifdef T3P_SYSTEM
#include "t3p_temperature.h"
#endif
#ifdef T3P_MEASURE
#include "t3p_meter.h"
#endif
#ifdef T3P_PD_PROFILES
#include "t3p_pd.h"
#endif
#ifdef T3P_PD_CHARGER
#include "t3p_charger.h"
static enum t3p_charger_state charger_state;
#endif
#ifdef T3P_PD_RD_TEST
#include "t3p_rd_test.h"
uint32_t meter_on_rd_test_page(void);
void meter_rd_test(uint32_t state, uint32_t off_mv, uint32_t on_mv);
#ifdef T3P_PD_ACK_TEST
void meter_rd_ack(uint32_t pdos, uint32_t goodcrc);
#endif
static struct t3p_rd_test rd_test = { .off_mv = UINT16_MAX, .on_mv = UINT16_MAX };
#endif

#define PA_BASE 0xfd3000u
#define PA11 (1u << 11)
#define PANEL_SIDE 240u
#define STRIP_ROWS 8u

void meter_demo_sample(uint32_t tick);
void meter_live_sample(uint32_t tick, uint32_t valid, uint32_t mv, int32_t ma);
void meter_pd_diagnostic(uint32_t valid, uint32_t first_four, uint32_t last);
void meter_pd_status(uint32_t state, uint32_t cc, uint32_t mode);
void meter_pd_warning(uint32_t warning);
void meter_long_press(void);
void meter_next_page(void);
uint32_t meter_double_press(void);
void meter_snapshot(void);
void meter_demo_render(uint32_t top, uint16_t pixels[PANEL_SIDE * STRIP_ROWS]);

static struct t3p_bsp board;
static uint16_t strip[PANEL_SIDE * STRIP_ROWS];
static volatile uint32_t startup_cookie = 0x54335044u;
static struct t3p_button button;
#ifdef T3P_SYSTEM
static struct t3p_system_stats system_stats;
static uint32_t system_tick;
#endif

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

enum redraw_request {
    REDRAW = 1u, START_CAPTURE = 2u, START_EPR = 4u, START_SNIFF = 8u, START_CABLE = 16u,
    RD_STEP = 32u, RD_CANCEL = 64u, CHARGER_ARM = 128u, CHARGER_SCAN = 256u,
    CHARGER_CANCEL = 512u
};

static unsigned poll_key(uint32_t now)
{
    enum t3p_button_event event = t3p_button_poll(&button, now, key_pressed());
#ifdef T3P_SYSTEM
    t3p_system_tick(&system_stats, &system_tick, now);
    if (event != T3P_BUTTON_NONE) {
        if (event == T3P_BUTTON_SHORT) t3p_system_count(&system_stats.short_presses);
        if (event == T3P_BUTTON_DOUBLE) t3p_system_count(&system_stats.double_presses);
        if (event == T3P_BUTTON_LONG) t3p_system_count(&system_stats.long_presses);
        system_stats.key_event = event;
        system_stats.key_ms = now - button.changed;
    }
#endif
#ifdef T3P_PD_CHARGER
    if (charger_state == CHARGER_ARMED) {
        if (event == T3P_BUTTON_SHORT) return CHARGER_SCAN;
        if (event == T3P_BUTTON_LONG) return CHARGER_CANCEL;
        return 0;
    }
#endif
#ifdef T3P_PD_RD_TEST
    if (event != T3P_BUTTON_NONE && meter_on_rd_test_page()) {
        if (event == T3P_BUTTON_DOUBLE) return RD_STEP;
        if (event == T3P_BUTTON_LONG) {
            if (rd_test.state == T3P_RD_OFF || rd_test.state == T3P_RD_ON) return RD_CANCEL;
            meter_next_page();
            return REDRAW;
        }
        if (rd_test.state == T3P_RD_OFF) return RD_STEP;
        /* A late second tap must leave the result on screen. */
        if (rd_test.state != T3P_RD_READY) return 0;
    }
#endif
    if (event == T3P_BUTTON_SHORT) { meter_next_page(); return REDRAW; }
    if (event == T3P_BUTTON_LONG) { meter_long_press(); return REDRAW; }
    if (event == T3P_BUTTON_DOUBLE) {
        uint32_t action = meter_double_press();
        if (action == 1) return START_CAPTURE;
        if (action == 2) return START_EPR;
        if (action == 3) return START_SNIFF;
        if (action == 4) return START_CABLE;
        if (action == 5) return CHARGER_ARM;
        return REDRAW;
    }
    return 0;
}

#ifdef T3P_PD_PROFILES
static unsigned poll_pd(struct t3p_pd_capture *pd, struct t3p_meter_bus *bus, uint32_t now)
{
    enum t3p_pd_state before = pd->state;
    unsigned before_warning = pd->fifo_warning;
    t3p_pd_poll_capture(pd, bus, now);
    /* Avoid starting a full redraw in the middle of the PD exchange. */
    if (pd->fifo_warning != before_warning) meter_pd_warning(pd->fifo_warning);
    if (pd->state == before) return 0;
    meter_pd_status(pd->state, pd->cc, pd->mode);
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
#ifdef T3P_SYSTEM
    system_stats = (struct t3p_system_stats){0};
    system_tick = 0; /* TIMER0 milliseconds start at BSP initialization. */
#endif
    if (t3p_bsp_lcd_init(&board)) t3p_signal_failure(3);
    init_key();

#ifdef T3P_FLASH_READER
    t3p_flash_usb_run(&board);
    t3p_signal_failure(4);
    return;
#endif

#ifdef T3P_CAPTURE_PROBE
    /* Read-only preflight, before meter/PD initialization. A long press exits.
     * No capture storage backend is enabled in this diagnostic firmware. */
    uint32_t probe_words[36];
    t3p_capture_probe_read(&board, &t3p_boot_snapshot, t3p_boot_parameters, probe_words);
    struct t3p_button probe_key;
    t3p_button_init(&probe_key, t3p_bsp_millis(&board), key_pressed());
    unsigned probe_section=0, probe_redraw=1;
    for (;;) {
        if (probe_redraw) {
            for (uint32_t top=0;top<PANEL_SIDE;top+=STRIP_ROWS) {
                meter_capture_probe_render(probe_words,probe_section,top,strip);
                if (t3p_bsp_rectangle(&board,0,PANEL_SIDE-1,top,top+STRIP_ROWS-1,strip,PANEL_SIDE*STRIP_ROWS))
                    t3p_signal_failure(4);
            }
            t3p_bsp_backlight(&board,1);
            probe_redraw=0;
        }
        enum t3p_button_event event=t3p_button_poll(&probe_key,t3p_bsp_millis(&board),key_pressed());
        if (event==T3P_BUTTON_LONG) break;
        if (event==T3P_BUTTON_SHORT || event==T3P_BUTTON_DOUBLE) {
            probe_section=(probe_section+1)%4; probe_redraw=1;
        }
    }
#endif

    uint32_t start = t3p_bsp_millis(&board);
    uint32_t sampled = start - 100;
    uint32_t rendered = start - 1000;
#ifdef T3P_MEASURE
    struct t3p_meter_bus meter_bus;
    t3p_meter_gpio_init(&board, &meter_bus);
#ifdef T3P_SYSTEM
    meter_bus.system = &system_stats; /* Shared by slow and fast bus copies. */
    struct t3p_meter_bus temperature_bus;
    t3p_temperature_bus_init(&board, &temperature_bus);
    temperature_bus.system = &system_stats;
    struct t3p_temperature temperature = {0};
    unsigned temperature_ready = 0;
#endif
    int identified = 0;
    uint32_t identified_at = start - 1000;
#ifdef T3P_PD_PROFILES
    struct t3p_meter_bus fast_bus = meter_bus;
    struct t3p_pd_capture pd = {0};
    int fast_ready = !t3p_meter_fast_bus(&board, &fast_bus);
    if (!fast_ready) pd.state = T3P_PD_TIMER_ERROR;
#ifdef T3P_SYSTEM
    temperature_ready = fast_ready;
    if (fast_ready) temperature_bus.delay = fast_bus.delay;
#endif
    meter_pd_status(pd.state, 0, pd.mode);
#endif
#ifdef T3P_PD_CHARGER
    uint32_t charger_armed_at = 0;
#endif
#ifdef T3P_PD_RD_TEST
    struct t3p_meter_bus *rd_bus = &meter_bus;
#ifdef T3P_PD_ACK_TEST
    struct t3p_meter_bus ack_bus = meter_bus;
    if (t3p_meter_fast_bus(&board, &ack_bus)) rd_test.state = T3P_RD_TIMER_ERROR;
    rd_bus = &ack_bus;
#endif
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
#ifdef T3P_PD_CHARGER
        if (redraw & CHARGER_ARM) {
            meter_charger_clear();
            if (charger_state == CHARGER_RESTORE || pd.state == T3P_PD_CABLE_RESTORE)
                charger_state = CHARGER_RESTORE;
            else if (pd.state == T3P_PD_TIMER_ERROR) charger_state = CHARGER_TIMER;
            else if (pd.state == T3P_PD_LISTENING) charger_state = CHARGER_BUSY;
            else charger_state = t3p_charger_arm(&fast_bus);
            now = charger_armed_at = t3p_bsp_millis(&board);
            redraw = REDRAW;
        } else if (charger_state == CHARGER_ARMED) {
            if (redraw & CHARGER_CANCEL)
                charger_state = t3p_charger_cancel(&fast_bus, CHARGER_CANCELLED);
            else if (now - charger_armed_at >= CHARGER_ARM_MS)
                charger_state = t3p_charger_cancel(&fast_bus, CHARGER_EXPIRED);
            else if (redraw & CHARGER_SCAN) {
                charger_state = t3p_charger_scan(&board, &fast_bus);
                now = t3p_bsp_millis(&board);
            }
            if (charger_state != CHARGER_ARMED) redraw = REDRAW;
        }
#endif
#ifdef T3P_PD_RD_TEST
        enum t3p_rd_state before = rd_test.state;
        if (redraw & RD_CANCEL) t3p_rd_test_cancel(&rd_test, rd_bus);
        else if (redraw & RD_STEP) t3p_rd_test_step(&rd_test, rd_bus, now);
        t3p_rd_test_poll(&rd_test, rd_bus, now);
        if (rd_test.state != before) redraw |= REDRAW;
#endif
#ifdef T3P_PD_PROFILES
        if (redraw & (START_CAPTURE | START_EPR | START_SNIFF | START_CABLE)) {
            enum t3p_pd_mode mode = (redraw & START_CABLE) ? T3P_PD_CABLE :
                                    (redraw & START_SNIFF) ? T3P_PD_SNIFF :
                                    (redraw & START_EPR) ? T3P_PD_EPR : T3P_PD_SPR;
#ifdef T3P_PD_CABLE_READ
            if (mode == T3P_PD_CABLE) t3p_pd_read_cable(&pd, &fast_bus, &board);
            else
#endif
                t3p_pd_start_capture(&pd, &fast_bus, now, mode);
            meter_pd_status(pd.state, pd.cc, pd.mode);
            meter_pd_warning(pd.fifo_warning);
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
#ifdef T3P_PD_ACK_TEST
            if (rd_test.state == T3P_RD_ON) sample_bus = rd_bus;
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
        unsigned refresh_due = redraw || now - rendered >= 1000;
#ifdef T3P_SYSTEM
        unsigned temperature_enabled = meter_on_system_page() && temperature_ready;
#ifdef T3P_PD_PROFILES
        temperature_enabled &= pd.state != T3P_PD_LISTENING;
#endif
#ifdef T3P_PD_CHARGER
        temperature_enabled &= charger_state != CHARGER_ARMED;
#endif
        t3p_temperature_poll(&temperature, &temperature_bus, &system_stats,
            t3p_bsp_millis(&board), temperature_enabled);
#endif
#ifdef T3P_PD_PROFILES
        /* Give fast captures priority over automatic repaints. Key presses
           and completion still redraw, and measurement sampling continues. */
        if (!redraw && pd.state == T3P_PD_LISTENING && pd.mode != T3P_PD_SPR)
            refresh_due = 0;
#endif
#ifdef T3P_PD_ACK_TEST
        if (!redraw && rd_test.state == T3P_RD_ON) refresh_due = 0;
#endif
        if (refresh_due) {
#ifdef T3P_SYSTEM
            uint32_t frame_started = t3p_bsp_millis(&board);
            t3p_system_tick(&system_stats, &system_tick, frame_started);
            system_stats.dbi_ops = board.operations;
            if (meter_on_system_page()) meter_system_status(&system_stats);
#endif
#ifdef T3P_PD_CHARGER
            meter_charger_status(charger_state);
#endif
#ifdef T3P_PD_RD_TEST
            meter_rd_test(rd_test.state, rd_test.off_mv, rd_test.on_mv);
#endif
#ifdef T3P_PD_ACK_TEST
            meter_rd_ack(rd_test.pdos, rd_test.goodcrc);
#endif
            meter_snapshot();
            redraw = 0;
            for (uint32_t top = 0; top < PANEL_SIDE; top += STRIP_ROWS) {
                meter_demo_render(top, strip);
                if (t3p_bsp_rectangle(&board, 0, PANEL_SIDE - 1, top, top + STRIP_ROWS - 1,
                                      strip, PANEL_SIDE * STRIP_ROWS)) {
#ifdef T3P_PD_CHARGER
                    if (charger_state == CHARGER_ARMED)
                        t3p_charger_cancel(&fast_bus, CHARGER_CANCELLED);
#endif
#ifdef T3P_PD_RD_TEST
                    t3p_rd_test_cancel(&rd_test, rd_bus);
#endif
                    t3p_signal_failure(4);
                }
                uint32_t between_strips = t3p_bsp_millis(&board);
                redraw |= poll_key(between_strips);
#ifdef T3P_PD_PROFILES
                redraw |= poll_pd(&pd, &fast_bus, between_strips);
#endif
#ifdef T3P_PD_ACK_TEST
                if (rd_test.state == T3P_RD_ON) {
                    t3p_rd_test_poll(&rd_test, rd_bus, between_strips);
                    if (rd_test.state != T3P_RD_ON) redraw |= REDRAW;
                }
#endif
            }
            t3p_bsp_backlight(&board, 1);
#ifdef T3P_SYSTEM
            uint32_t frame_finished = t3p_bsp_millis(&board);
            t3p_system_tick(&system_stats, &system_tick, frame_finished);
            system_stats.frame_ms = frame_finished - frame_started;
            if (system_stats.frame_ms > system_stats.frame_max_ms)
                system_stats.frame_max_ms = system_stats.frame_ms;
            t3p_system_count(&system_stats.frames);
#endif
            rendered = now;
        }
        uint32_t pause_ms = 5;
#ifdef T3P_PD_PROFILES
        if (pd.state == T3P_PD_LISTENING) pause_ms = pd.catch_up ? 0 : 1;
#endif
#ifdef T3P_PD_ACK_TEST
        if (rd_test.state == T3P_RD_ON) pause_ms = 1;
#endif
        if (pause_ms && t3p_bsp_delay(&board, pause_ms)) {
#ifdef T3P_PD_CHARGER
            if (charger_state == CHARGER_ARMED)
                t3p_charger_cancel(&fast_bus, CHARGER_TIMER);
#endif
#ifdef T3P_PD_RD_TEST
            t3p_rd_test_cancel(&rd_test, rd_bus);
#endif
            t3p_signal_failure(5);
        }
    }
}
