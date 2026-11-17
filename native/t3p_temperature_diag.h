/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_TEMPERATURE_DIAG_H
#define T3P_TEMPERATURE_DIAG_H
#include <stdint.h>
/* Diagnostic candidate only. Original SYSTEM ABI and Capture V5 stay unchanged.
 * GPIO registers are sequential observations, not simultaneous logic samples. */
#define T3P_TEMP_DIAG_MAGIC 0x31444d54u
#define T3P_TEMP_DIAG_WORDS 110u
enum t3p_temp_phase { TEMP_IDLE, TEMP_ID, TEMP_PRE_STATUS, TEMP_CALIBRATION,
    TEMP_TRIGGER, TEMP_WAIT, TEMP_POST_STATUS, TEMP_RAW, TEMP_COMPENSATE, TEMP_DONE,
    TEMP_CONFIGURE, TEMP_CLEANUP };
enum t3p_temp_result { TEMP_OK, TEMP_IO, TEMP_BAD_ID, TEMP_BUSY, TEMP_BAD_CALIBRATION,
    TEMP_TIMEOUT, TEMP_SKIPPED, TEMP_RANGE, TEMP_CANCELLED, TEMP_BAD_CONFIGURATION };
enum t3p_temp_stage { TEMP_BUS_NONE, TEMP_BUS_IDLE, TEMP_BUS_START, TEMP_BUS_ADDRESS_W,
    TEMP_BUS_REGISTER, TEMP_BUS_RESTART, TEMP_BUS_ADDRESS_R, TEMP_BUS_DATA,
    TEMP_BUS_STOP, TEMP_BUS_DONE };
enum t3p_temp_bus_error { TEMP_BUS_OK, TEMP_BUS_DELAY, TEMP_BUS_SCL_LOW,
    TEMP_BUS_SDA_LOW, TEMP_BUS_NACK, TEMP_BUS_ARGUMENT };
enum t3p_temp_pins { TEMP_PINS_BEFORE, TEMP_PINS_INIT, TEMP_PINS_RELEASED,
    TEMP_PINS_FAULT, TEMP_PINS_STOPPED };
enum t3p_temp_valid { TEMP_VALID_ID=1, TEMP_VALID_STATUS=2, TEMP_VALID_CAL=4,
    TEMP_VALID_CONTROL=8, TEMP_VALID_RAW=16 };
struct t3p_temperature_diag {
    uint32_t magic, version, sequence, attempts, successes, failures;
    uint32_t gate, phase, result, transactions, reg, direction;
    uint32_t stage, bus_error, byte_index, bit_index, ack_seen, ack_valid;
    uint32_t valid, id, status, adc, cal0, cal1, control;
    uint32_t timer_ready, timer_con, timer_count, gpio_valid, stop_error;
    uint32_t bus_port, scl_mask, sda_mask, init_port, active_port;
    int32_t temp_centi;
    /* V3: cal0=config readback mask; cal1=idle cleanup result/error/stage/STOP;
     * adc=TEMP_OUT counts (1/128 K), control=MODE_CFG trigger; sensor_kind=2. */
    uint32_t temp_valid, updated_ms, attempted_ms, sensor_kind;
    /* IN OUT DIR DIE DIEH PU0 PU1 PD0 PD1 HD0 HD1 SPL CON TIMER1_COUNT */
    uint32_t pins[5][14]; /* BEFORE/INIT: PA; RELEASED/FAULT/STOPPED: active PB */
};
_Static_assert(sizeof(struct t3p_temperature_diag)==T3P_TEMP_DIAG_WORDS*4,"Temperature diagnostic ABI");
extern struct t3p_temperature_diag t3p_temperature_diagnostics;
int t3p_temperature_diag_page(uint32_t, uint32_t [10]);
#endif
