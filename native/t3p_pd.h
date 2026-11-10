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
/* Shared with src/pd/cable/active.rs. */
enum t3p_cable_query {
    CABLE_ID, CABLE_SVIDS, CABLE_MODES, CABLE_MFR, CABLE_STATUS, CABLE_SOP2_RESET, CABLE_SOP2_STATUS
};
enum t3p_cable_result {
    CABLE_IGNORE, CABLE_ACK, CABLE_MORE, CABLE_NAK, CABLE_BUSY, CABLE_UNSUPPORTED,
    CABLE_REJECT, CABLE_INVALID, CABLE_NO_CRC, CABLE_NO_REPLY, CABLE_LIMIT
};
#define CABLE_MODAL 1u
#define CABLE_ACTIVE 2u
#define CABLE_SECOND 4u

int t3p_pd_cable_write(struct t3p_meter_bus *, uint8_t, uint8_t);
int t3p_pd_cable_tx(struct t3p_meter_bus *, unsigned second, uint16_t header,
                   const uint8_t *payload, unsigned length);
#ifdef T3P_PD_CHARGER
int t3p_pd_sop_tx(struct t3p_meter_bus *, uint16_t, const uint8_t *, unsigned);
#endif
int32_t meter_pd_cable_reply(uint32_t query, uint32_t index, const uint8_t *frame, uint32_t length);
void meter_pd_cable_result(uint32_t query, uint32_t index, uint32_t result);
uint32_t meter_pd_cable_parameters(void);
uint32_t meter_pd_cable_svid(uint32_t index);
enum t3p_pd_state t3p_cable_read(struct t3p_bsp *, struct t3p_meter_bus *);
void t3p_pd_read_cable(struct t3p_pd_capture *, struct t3p_meter_bus *, struct t3p_bsp *);
#endif
#endif
