/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_PD_H
#define T3P_PD_H
#include "t3p_meter.h"

/* Status numbers are consumed by src/pd.rs. */
enum t3p_pd_state {
    T3P_PD_READY, T3P_PD_LISTENING, T3P_PD_CAPTURED, T3P_PD_TIMEOUT, T3P_PD_CONFIG,
    T3P_PD_BUS_ERROR, T3P_PD_FIFO_FULL, T3P_PD_BAD_FRAME, T3P_PD_LINK_RESET, T3P_PD_TIMER_ERROR,
    T3P_PD_PARTIAL, T3P_PD_CABLE_VBUS, T3P_PD_CABLE_CC_BUSY, T3P_PD_CABLE_NO_RA,
    T3P_PD_CABLE_VCONN_FAULT, T3P_PD_CABLE_NO_ID, T3P_PD_CABLE_RESTORE
};

enum t3p_pd_mode { T3P_PD_SPR, T3P_PD_EPR, T3P_PD_SNIFF, T3P_PD_CABLE };

struct t3p_pd_capture {
    uint32_t started, polled;
    enum t3p_pd_state state;
    unsigned cc, draining, partial, fifo_warning, catch_up;
    enum t3p_pd_mode mode;
};

int t3p_pd_rx_config(struct t3p_meter_bus *, uint8_t, uint8_t);
void t3p_pd_start_capture(struct t3p_pd_capture *, struct t3p_meter_bus *, uint32_t,
                          enum t3p_pd_mode);
void t3p_pd_poll_capture(struct t3p_pd_capture *, struct t3p_meter_bus *, uint32_t);
#ifdef T3P_PD_CABLE_READ
int t3p_pd_cable_write(struct t3p_meter_bus *, uint8_t, uint8_t);
int t3p_pd_send_identity(struct t3p_meter_bus *);
enum t3p_pd_state t3p_cable_read(struct t3p_bsp *, struct t3p_meter_bus *);
void t3p_pd_read_cable(struct t3p_pd_capture *, struct t3p_meter_bus *, struct t3p_bsp *);
#endif
#endif
