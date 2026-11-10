/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_CHARGER_H
#define T3P_CHARGER_H
#include "t3p_meter.h"

#define CHARGER_ARM_MS 30000u

/* Shared with src/pd/charger.rs and src/screen/charger.rs. */
enum t3p_charger_state {
    CHARGER_READY, CHARGER_ARMED, CHARGER_COMPLETE, CHARGER_PD2,
    CHARGER_EXT_SKIPPED, CHARGER_NO_EXT, CHARGER_REJECTED, CHARGER_WAIT,
    CHARGER_UNSUPPORTED, CHARGER_NO_CAPS, CHARGER_NO_ACCEPT, CHARGER_NO_PS_RDY,
    CHARGER_NO_GOODCRC, CHARGER_VBUS_PRESENT, CHARGER_VBUS_RANGE, CHARGER_BUSY,
    CHARGER_CONFIG, CHARGER_IO, CHARGER_RX_ERROR, CHARGER_RESET,
    CHARGER_CANCELLED, CHARGER_EXPIRED, CHARGER_RESTORE, CHARGER_TIMER
};

enum t3p_charger_state t3p_charger_arm(struct t3p_meter_bus *);
enum t3p_charger_state t3p_charger_scan(struct t3p_bsp *, struct t3p_meter_bus *);
enum t3p_charger_state t3p_charger_cancel(struct t3p_meter_bus *, enum t3p_charger_state);
void meter_charger_clear(void);
void meter_charger_status(uint32_t state);
#endif
