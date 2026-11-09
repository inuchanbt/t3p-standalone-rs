/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_pd.h"

#define DEVICE_ID 0x01
#define SWITCHES0 0x02
#define SWITCHES1 0x03
#define MEASURE 0x04
#define CONTROL0 0x06
#define CONTROL1 0x07
#define CONTROL3 0x09
#define POWER 0x0b
#define RESET 0x0c
#define OCPREG 0x0d
#define INTERRUPTA 0x3e
#define STATUS0 0x40
#define STATUS1 0x41
#define FIFO 0x43

#define RD_BOTH 0x03
#define MEAS_CC1 0x04
#define MEAS_CC2 0x08
#define RP_CC1 0x40
#define RP_CC2 0x80
#define VCONN_CC2 0x20
#define SOURCE_CC1_AUTO_CRC 0xb5 /* Source/DFP for SOP, Rev 2.0 GoodCRC, TX on CC1 */
#define HOST_CUR_330UA_MASKED 0x2c
#define ENSOP1 0x01
#define ENSOP2 0x02
#define TX_FLUSH 0x40
#define RX_FLUSH 0x04
#define PD_RESET 0x02
#define RETRY_TWICE 0x05
#define PD_POWER 0x0f
#define OSC_POWER 0x09
#define BASE_POWER 0x01
#define CABLE_OCP_300MA 0x0a

#define I_OCP_TEMP 0x80
#define I_TXSENT 0x04
#define I_HARDRST 0x01
#define I_GCRCSENT 0x01
#define VBUSOK 0x80
#define ACTIVITY 0x40
#define COMP 0x20
#define CRC_CHK 0x10
#define RX_EMPTY 0x20
#define RX_FULL 0x10
#define TX_IDLE 0x08
#define OCP_OR_OVERTEMP 0x03
#define I_CRC_CHK 0x10
#define I_COLLISION 0x02

#define EXTERNAL_RP_MDAC 0x03 /* 168 mV with Rd on */
#define OPEN_CC_MDAC 0x3d    /* 2.604 V with 330 uA Rp */
#define RA_MIN_CODE 4u       /* Above 168 mV, excluding a short */
#define RA_MAX_CODE 13u      /* Below 588 mV, excluding Rd */
#define CC_PROBE_SETTLE_MS 2u
#define VBUS_PRESENT_MV 4000u /* Allow the observed HID-only reading of about 3 V. */
#define VCONN_SETTLE_MS 55u
#define VCONN_OFF_MS 230u
#define RESPONSE_MS 35u
#define VDM_BUSY_MS 50u
#define SVID_PAGE_LIMIT 6u
#define PD2 0x40u
#define PD3 0x80u
#define SOP1_TOKEN 6u
#define SOP2_TOKEN 5u

/* Indices of the consecutive registers read starting at INTERRUPTA. */
enum { IRQ_A, IRQ_B, LIVE0, LIVE1, IRQ, STATUS_COUNT };

void meter_pd_cable_cc(uint32_t cc1, uint32_t cc2);

static int configure(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value)
{
    uint8_t actual, expected = value;
    if (reg == CONTROL0) expected &= ~TX_FLUSH;
    if (reg == CONTROL1) expected &= ~RX_FLUSH;
    return t3p_pd_cable_write(bus, reg, value) ||
           t3p_pd_rx_read(bus, reg, &actual, 1) || actual != expected;
}

static int restore(struct t3p_meter_bus *bus, uint8_t reg, uint8_t value)
{
    return configure(bus, reg, value) && configure(bus, reg, value);
}

static enum t3p_pd_state check_vbus(struct t3p_meter_bus *bus)
{
    uint16_t mv;
    int16_t ma;
    if (t3p_meter_sample(bus, &mv, &ma)) return T3P_PD_BUS_ERROR;
    return mv >= VBUS_PRESENT_MV ? T3P_PD_CABLE_VBUS : T3P_PD_READY;
}

static enum t3p_pd_state compare_cc(struct t3p_bsp *board, struct t3p_meter_bus *bus,
                                    uint8_t mdac, uint8_t *status)
{
    if (configure(bus, MEASURE, mdac)) return T3P_PD_BUS_ERROR;
    if (t3p_bsp_delay(board, CC_PROBE_SETTLE_MS)) return T3P_PD_TIMER_ERROR;
    if (t3p_pd_rx_read(bus, STATUS0, status, 1)) return T3P_PD_BUS_ERROR;
    if (*status & VBUSOK) return T3P_PD_CABLE_VBUS;
    if (*status & ACTIVITY) return T3P_PD_CABLE_CC_BUSY;
    return T3P_PD_READY;
}

static enum t3p_pd_state check_external_rp(struct t3p_bsp *board, struct t3p_meter_bus *bus,
                                           uint8_t cc)
{
    uint8_t status;
    if (configure(bus, SWITCHES0, RD_BOTH | cc)) return T3P_PD_BUS_ERROR;
    enum t3p_pd_state state = compare_cc(board, bus, EXTERNAL_RP_MDAC, &status);
    if (state != T3P_PD_READY) return state;
    return status & COMP ? T3P_PD_CABLE_CC_BUSY : T3P_PD_READY;
}

static enum t3p_pd_state measure_cc(struct t3p_bsp *board, struct t3p_meter_bus *bus,
                                    uint8_t cc, unsigned *code)
{
    if (configure(bus, SWITCHES0, RP_CC1 | RP_CC2 | cc)) return T3P_PD_BUS_ERROR;
    /* Find the first DAC threshold above the pin voltage. Code 64 means
     * the pin was above the highest threshold (nominally 2.688 V). */
    unsigned low = 0, high = 64;
    while (low < high) {
        unsigned mid = (low + high) / 2;
        uint8_t status;
        enum t3p_pd_state state = compare_cc(board, bus, mid, &status);
        if (state != T3P_PD_READY) return state;
        if (status & COMP) low = mid + 1;
        else high = mid;
    }
    *code = low;
    return T3P_PD_READY;
}

static enum t3p_pd_state check_lines(struct t3p_bsp *board, struct t3p_meter_bus *bus)
{
    enum t3p_pd_state state = check_external_rp(board, bus, MEAS_CC1);
    if (state != T3P_PD_READY) return state;
    state = check_external_rp(board, bus, MEAS_CC2);
    if (state != T3P_PD_READY) return state;

    /* Match the stock cable probe bias (Rp on both pins at 330 uA).
     * Leave the transmitter and VCONN off while checking both pins. */
    unsigned cc1, cc2;
    state = measure_cc(board, bus, MEAS_CC1, &cc1);
    if (state != T3P_PD_READY) return state;
    state = measure_cc(board, bus, MEAS_CC2, &cc2);
    if (state != T3P_PD_READY) return state;
    meter_pd_cable_cc(cc1, cc2);
    if (cc2 < RA_MIN_CODE || cc2 > RA_MAX_CODE) return T3P_PD_CABLE_NO_RA;
    if (cc1 <= OPEN_CC_MDAC) return T3P_PD_CABLE_CC_BUSY;
    return T3P_PD_READY;
}

static enum t3p_pd_state read_status(struct t3p_meter_bus *bus, uint8_t status[STATUS_COUNT])
{
    if (t3p_pd_rx_read(bus, INTERRUPTA, status, STATUS_COUNT)) return T3P_PD_BUS_ERROR;
    if ((status[IRQ_A] & I_OCP_TEMP) || (status[LIVE1] & OCP_OR_OVERTEMP))
        return T3P_PD_CABLE_VCONN_FAULT;
    if (status[LIVE0] & VBUSOK) return T3P_PD_CABLE_VBUS;
    if ((status[IRQ_A] & I_HARDRST) || (status[IRQ] & I_COLLISION)) return T3P_PD_CABLE_CC_BUSY;
    if (status[LIVE1] & RX_FULL) return T3P_PD_FIFO_FULL;
    return T3P_PD_READY;
}

struct cable_link {
    uint16_t revision, svdm;
    uint8_t id;
    unsigned second;
};

static enum t3p_pd_state wait_idle(struct t3p_bsp *board, struct t3p_meter_bus *bus,
                                  unsigned settle_ms)
{
    uint32_t started = t3p_bsp_millis(board);
    for (;;) {
        uint8_t status[STATUS_COUNT];
        enum t3p_pd_state state = read_status(bus, status);
        if (state != T3P_PD_READY) return state;
        if (!(status[LIVE1] & RX_EMPTY)) return T3P_PD_CABLE_CC_BUSY;
        unsigned elapsed = t3p_bsp_millis(board) - started;
        if (elapsed >= settle_ms && !(status[LIVE0] & ACTIVITY) && (status[LIVE1] & TX_IDLE))
            return status[LIVE0] & COMP ? T3P_PD_READY : T3P_PD_CABLE_CC_BUSY;
        if (elapsed >= settle_ms + 5u) return T3P_PD_CABLE_CC_BUSY;
        if (t3p_bsp_delay(board, 1)) return T3P_PD_TIMER_ERROR;
    }
}

static int send_query(struct t3p_meter_bus *bus, const struct cable_link *link,
                      enum t3p_cable_query query, uint32_t index)
{
    uint16_t header = link->revision | ((uint16_t)link->id << 9);
    uint8_t payload[4];
    unsigned length = 0;
    switch (query) {
    case CABLE_ID:
    case CABLE_SVIDS:
    case CABLE_MODES: {
        uint16_t svid = query == CABLE_MODES ? meter_pd_cable_svid(index) : 0xff00;
        uint16_t vdm = 0x8000 | link->svdm | (query + 1);
        header |= 0x100f; /* One VDO, Vendor_Defined. */
        payload[0] = (uint8_t)vdm;
        payload[1] = (uint8_t)(vdm >> 8);
        payload[2] = (uint8_t)svid;
        payload[3] = (uint8_t)(svid >> 8);
        length = sizeof(payload);
        break;
    }
    case CABLE_MFR:
        header |= 0x9006; /* Get_Manufacturer_Info, one data object. */
        payload[0] = 2;
        payload[1] = 0x80; /* Chunk 0, two bytes of data. */
        payload[2] = 0;    /* Target = port/cable plug */
        payload[3] = 0;    /* Reference = 0 */
        length = sizeof(payload);
        break;
    case CABLE_SOP2_RESET:
        header |= 13; /* Soft_Reset */
        break;
    case CABLE_STATUS:
    case CABLE_SOP2_STATUS:
        header |= 18; /* Get_Status */
        break;
    }
    return t3p_pd_cable_tx(bus, link->second, header, payload, length);
}

static enum t3p_pd_state receive_reply(struct t3p_bsp *board, struct t3p_meter_bus *bus,
                                       struct cable_link *link, enum t3p_cable_query query,
                                       uint32_t index, enum t3p_cable_result *result)
{
    uint32_t started = t3p_bsp_millis(board);
    unsigned acknowledged = 0, rx_ready = 0;
    unsigned ignored = 0;
    uint8_t status[STATUS_COUNT];
    do {
        enum t3p_pd_state state = read_status(bus, status);
        if (state != T3P_PD_READY) return state;
        acknowledged |= status[IRQ_A] & I_TXSENT;
        rx_ready |= (status[IRQ_A] & I_TXSENT) || (status[IRQ_B] & I_GCRCSENT) ||
                    (status[IRQ] & I_CRC_CHK) || (status[LIVE0] & CRC_CHK);
        if (rx_ready && !(status[LIVE1] & RX_EMPTY) && !(status[LIVE0] & ACTIVITY)) {
            uint8_t frame[35];
            if (t3p_pd_rx_read(bus, FIFO, frame, 3)) return T3P_PD_BUS_ERROR;
            if (frame[0] >> 5 != (link->second ? SOP2_TOKEN : SOP1_TOKEN))
                return T3P_PD_CABLE_CC_BUSY;
            unsigned body = 4u + 4u * ((frame[2] >> 4) & 7u);
            if (t3p_pd_rx_read(bus, FIFO, frame + 3, body)) return T3P_PD_BUS_ERROR;
            int32_t reply = meter_pd_cable_reply(query, index, frame, 3 + body);
            if (reply < 0) return T3P_PD_BAD_FRAME;
            if (reply != CABLE_IGNORE) {
                *result = (enum t3p_cable_result)reply;
                link->id = (link->id + 1) & 7;
                return T3P_PD_READY;
            }
            /* Another complete record can already be queued behind GoodCRC. */
            if (++ignored == 16) return T3P_PD_CABLE_CC_BUSY;
            continue;
        }
        if (t3p_bsp_delay(board, 1)) return T3P_PD_TIMER_ERROR;
    } while (t3p_bsp_millis(board) - started < RESPONSE_MS);
    if (acknowledged) link->id = (link->id + 1) & 7;
    *result = acknowledged ? CABLE_NO_REPLY : CABLE_NO_CRC;
    return T3P_PD_READY;
}

static enum t3p_pd_state query_cable(struct t3p_bsp *board, struct t3p_meter_bus *bus,
                                     struct cable_link *link, enum t3p_cable_query query,
                                     uint32_t index, enum t3p_cable_result *result)
{
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        enum t3p_pd_state state = wait_idle(board, bus, attempt ? VDM_BUSY_MS : 0);
        if (state != T3P_PD_READY) return state;
        if (send_query(bus, link, query, index)) return T3P_PD_BUS_ERROR;
        state = receive_reply(board, bus, link, query, index, result);
        if (state != T3P_PD_READY) return state;
        meter_pd_cable_result(query, index, *result);
        if (*result != CABLE_BUSY) break;
    }
    return T3P_PD_READY;
}

static enum t3p_pd_state discover(struct t3p_bsp *board, struct t3p_meter_bus *bus)
{
    struct cable_link link = { .revision = PD3, .svdm = 0x2000 };
    enum t3p_cable_result result;
    enum t3p_pd_state state = query_cable(board, bus, &link, CABLE_ID, 0, &result);
    if (state != T3P_PD_READY) return state;
    if (result != CABLE_ACK && result != CABLE_BUSY) {
        /* Some older markers only answer a PD 2.0 Discover Identity. */
        link.revision = PD2;
        link.svdm = 0;
        state = query_cable(board, bus, &link, CABLE_ID, 0, &result);
        if (state != T3P_PD_READY) return state;
    }
    if (result != CABLE_ACK) {
        return result == CABLE_NO_CRC && !link.id ? T3P_PD_TIMEOUT : T3P_PD_CABLE_NO_ID;
    }

    uint32_t parameters = meter_pd_cable_parameters();
    link.revision = parameters & 0xc0;
    link.svdm = parameters & 0x7800;
    if (parameters & CABLE_MODAL) {
        for (unsigned page = 0; page < SVID_PAGE_LIMIT; ++page) {
            state = query_cable(board, bus, &link, CABLE_SVIDS, 0, &result);
            if (state != T3P_PD_READY) return state;
            if (result != CABLE_MORE) break;
        }
        if (result == CABLE_MORE) meter_pd_cable_result(CABLE_SVIDS, 0, CABLE_LIMIT);
        for (unsigned i = 0; meter_pd_cable_svid(i); ++i) {
            state = query_cable(board, bus, &link, CABLE_MODES, i, &result);
            if (state != T3P_PD_READY) return state;
        }
    }
    if (link.revision != PD3) goto done;

    state = query_cable(board, bus, &link, CABLE_MFR, 0, &result);
    if (state != T3P_PD_READY) return state;
    if (parameters & CABLE_ACTIVE) {
        state = query_cable(board, bus, &link, CABLE_STATUS, 0, &result);
        if (state != T3P_PD_READY) return state;
    }
    if (parameters & CABLE_SECOND) {
        state = wait_idle(board, bus, 0);
        if (state != T3P_PD_READY) return state;
        if (configure(bus, CONTROL1, ENSOP2)) return T3P_PD_BUS_ERROR;
        link.second = 1;
        link.id = 0;
        state = query_cable(board, bus, &link, CABLE_SOP2_RESET, 0, &result);
        if (state != T3P_PD_READY) return state;
        if (result == CABLE_ACK) {
            state = query_cable(board, bus, &link, CABLE_SOP2_STATUS, 0, &result);
            if (state != T3P_PD_READY) return state;
        }
    }
done:
    /* Give the controller time to finish the last automatic GoodCRC. */
    state = wait_idle(board, bus, 0);
    return state == T3P_PD_READY ? T3P_PD_CAPTURED : state;
}

static enum t3p_pd_state exchange(struct t3p_bsp *board, struct t3p_meter_bus *bus,
                                  unsigned *vconn_attempted)
{
    if (configure(bus, POWER, PD_POWER) || configure(bus, CONTROL0, HOST_CUR_330UA_MASKED))
        return T3P_PD_BUS_ERROR;
    enum t3p_pd_state state = check_lines(board, bus);
    if (state != T3P_PD_READY) return state;

    uint8_t status[STATUS_COUNT];
    if (t3p_pd_cable_write(bus, RESET, PD_RESET) ||
        configure(bus, CONTROL0, HOST_CUR_330UA_MASKED | TX_FLUSH) ||
        configure(bus, CONTROL1, ENSOP1 | RX_FLUSH) ||
        configure(bus, CONTROL3, RETRY_TWICE) ||
        configure(bus, OCPREG, CABLE_OCP_300MA) ||
        t3p_pd_rx_read(bus, INTERRUPTA, status, sizeof(status)))
        return T3P_PD_BUS_ERROR;
    if ((status[LIVE1] & 0x3f) != (RX_EMPTY | TX_IDLE)) return T3P_PD_FIFO_FULL;
    state = check_vbus(bus);
    if (state != T3P_PD_READY) return state;

    if (configure(bus, MEASURE, OPEN_CC_MDAC)) return T3P_PD_BUS_ERROR;
    *vconn_attempted = 1;
    if (configure(bus, SWITCHES0, RP_CC1 | MEAS_CC1 | VCONN_CC2))
        return T3P_PD_BUS_ERROR;
    uint32_t started = t3p_bsp_millis(board);
    do {
        state = read_status(bus, status);
        if (state != T3P_PD_READY) return state;
        if ((status[LIVE0] & (COMP | ACTIVITY)) != COMP || !(status[LIVE1] & RX_EMPTY))
            return T3P_PD_CABLE_CC_BUSY;
        if (t3p_bsp_delay(board, 1)) return T3P_PD_TIMER_ERROR;
    } while (t3p_bsp_millis(board) - started < VCONN_SETTLE_MS);

    if (configure(bus, SWITCHES1, SOURCE_CC1_AUTO_CRC)) return T3P_PD_BUS_ERROR;
    return discover(board, bus);
}

enum t3p_pd_state t3p_cable_read(struct t3p_bsp *board, struct t3p_meter_bus *bus)
{
    uint8_t regs[9], power, ocp, fifo;
    if (t3p_pd_rx_read(bus, DEVICE_ID, regs, sizeof(regs)) ||
        t3p_pd_rx_read(bus, POWER, &power, 1) || t3p_pd_rx_read(bus, OCPREG, &ocp, 1) ||
        t3p_pd_rx_read(bus, STATUS1, &fifo, 1) ||
        t3p_meter_identify(bus))
        return T3P_PD_BUS_ERROR;
    if (regs[0] != 0x91 || regs[1] != RD_BOTH || (regs[2] & 0x0f) || (regs[3] & 0xc0) ||
        (regs[5] & 0xd3) || regs[6] || regs[7] != 0x02 || regs[8] != 0x06 ||
        power != BASE_POWER || (fifo & 0x0f) != TX_IDLE)
        return T3P_PD_CONFIG;
    enum t3p_pd_state state = check_vbus(bus);
    if (state != T3P_PD_READY) return state;

    unsigned vconn_attempted = 0;
    state = exchange(board, bus, &vconn_attempted);

    /* Every path from exchange can have touched the PHY. Remove VCONN first
     * and attempt every restore even if a preceding I2C transaction fails. */
    int switch_failed = restore(bus, SWITCHES0, RD_BOTH);
    int failed = switch_failed;
    failed |= restore(bus, SWITCHES1, regs[2]);
    failed |= restore(bus, CONTROL3, regs[8]);
    failed |= restore(bus, POWER, OSC_POWER);
    failed |= t3p_pd_cable_write(bus, RESET, PD_RESET) && t3p_pd_cable_write(bus, RESET, PD_RESET);
    failed |= restore(bus, CONTROL0, regs[5] | TX_FLUSH);
    failed |= restore(bus, CONTROL1, RX_FLUSH);
    failed |= restore(bus, MEASURE, regs[3]);
    if (!switch_failed) failed |= restore(bus, OCPREG, ocp);
    failed |= restore(bus, POWER, power);
    if (failed) return T3P_PD_CABLE_RESTORE;
    if (vconn_attempted && t3p_bsp_delay(board, VCONN_OFF_MS)) return T3P_PD_TIMER_ERROR;
    return state;
}
