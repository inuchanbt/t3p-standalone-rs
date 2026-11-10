// SPDX-License-Identifier: GPL-2.0-or-later
use super::cable::hex;
use super::{
    AMPS, GRID, MUTED, Overview, Strip, TEXT, VOLTS, WATTS, card, pd_number, spr_rows, value,
};

pub(super) fn controls(view: &Overview) -> &'static [u8] {
    if view.charger.state == 1 {
        b"TAP SCAN HOLD CANCEL"
    } else if view.charger.has_offer() && !view.charger_offer {
        b"TAP OFFER 2X SCAN HOLD EXIT"
    } else {
        b"TAP NEXT 2X SCAN HOLD EXIT"
    }
}

pub(super) fn render(p: &mut Strip<'_>, view: &Overview) {
    let scan = &view.charger;
    p.text(215, 36, b"CC1", 1, AMPS);
    if view.charger_offer
        && let Some(caps) = &scan.caps
    {
        p.text(8, 36, b"SPR OFFER", 1, VOLTS);
        p.text(94, 36, b"SOURCE CAPABILITIES", 1, TEXT);
        spr_rows(p, caps);
        return;
    }
    p.text(8, 36, b"5V SCAN", 1, VOLTS);
    let rev: &[u8] = match scan.revision {
        0x40 => b"PD 2.0",
        0x80 => b"PD 3.X",
        _ => b"",
    };
    p.text(94, 36, rev, 1, TEXT);

    let (status, color): (&[u8], u16) = match scan.state {
        0 => (b"READY", AMPS),
        1 => (b"RD OFF", AMPS),
        2 => (b"SOURCE EXT CAP OK", WATTS),
        3 => (b"5V CONTRACT OK", WATTS),
        4 => (b"EXT QUERY SKIPPED", VOLTS),
        5 => (b"NO EXT CAP REPLY", VOLTS),
        6 => (b"SOURCE REJECT", VOLTS),
        7 => (b"SOURCE WAIT", VOLTS),
        8 => (b"NOT SUPPORTED", VOLTS),
        9 => (b"NO SOURCE CAPS", VOLTS),
        10 => (b"NO ACCEPT", VOLTS),
        11 => (b"NO PS_RDY", VOLTS),
        12 => (b"NO GOODCRC", VOLTS),
        13 => (b"VBUS PRESENT", VOLTS),
        14 => (b"VBUS OUT OF RANGE", VOLTS),
        15 => (b"PD BUSY", VOLTS),
        16 => (b"CHECK PHY CONFIG", VOLTS),
        17 => (b"PHY OR METER I/O", VOLTS),
        18 => (b"RX ERROR", VOLTS),
        19 => (b"PD RESET SEEN", VOLTS),
        20 => (b"CANCELLED", AMPS),
        21 => (b"ARM TIMEOUT", VOLTS),
        22 => (b"PHY RESTORE ERROR", VOLTS),
        23 => (b"TIMER ERROR", VOLTS),
        _ => (b"SCAN ERROR", VOLTS),
    };
    p.text(8, 53, status, 2, color);

    if scan.state <= 1 {
        let (first, second, third): (&[u8], &[u8], &[u8]) = if scan.state == 0 {
            (
                b"POWER T3P FROM HID",
                b"DISCONNECT SRC AND LOAD",
                b"2X TO ARM THE SCAN",
            )
        } else {
            (
                b"CONNECT SRC TO RECEPTACLE",
                b"LEAVE THE PLUG OPEN",
                b"TAP TO SCAN ON CC1",
            )
        };
        p.outline(8, 82, 224, 118, GRID);
        p.text(18, 96, first, 1, TEXT);
        p.text(18, 121, second, 1, TEXT);
        p.text(18, 146, third, 1, AMPS);
        p.text(18, 176, b"FIXED 5V / 0.5A MAX REQUEST", 1, MUTED);
        return;
    }

    card(p, 8, 78, 108, 43, b"VBUS MEASURED", VOLTS);
    value(
        p,
        14,
        96,
        96,
        scan.measured_mv.map(|mv| (u64::from(mv), false)),
        b"V",
        VOLTS,
        2,
    );
    card(p, 124, 78, 108, 43, b"5V REQUEST", AMPS);
    let requested =
        (scan.request_current != 0).then_some((u64::from(scan.request_current) * 10, false));
    value(p, 130, 96, 96, requested, b"A", AMPS, 2);
    p.text(
        8,
        129,
        if scan.accepted {
            b"ACCEPT OK"
        } else {
            b"ACCEPT --"
        },
        1,
        MUTED,
    );
    p.text(
        124,
        129,
        if scan.ps_rdy {
            b"PS_RDY OK"
        } else {
            b"PS_RDY --"
        },
        1,
        MUTED,
    );
    p.rect(8, 144, 224, 1, GRID);
    if let Some(ext) = &scan.extended {
        p.text(8, 153, b"VID/PID", 1, MUTED);
        p.text(65, 153, &hex::<4>(u32::from(ext.vid)), 1, TEXT);
        p.text(86, 153, b"/", 1, MUTED);
        p.text(93, 153, &hex::<4>(u32::from(ext.pid)), 1, TEXT);
        p.text(124, 153, b"XID", 1, MUTED);
        p.text(150, 153, &hex::<8>(ext.xid), 1, TEXT);
        p.text(8, 171, b"FW/HW", 1, MUTED);
        p.text(65, 171, &hex::<2>(u32::from(ext.fw)), 1, TEXT);
        p.text(76, 171, b"/", 1, MUTED);
        p.text(83, 171, &hex::<2>(u32::from(ext.hw)), 1, TEXT);
        p.text(8, 189, b"SPR PDP", 1, MUTED);
        pd_number(p, 65, 189, u32::from(ext.spr_pdp) * 1000, b"W", 1, WATTS);
        p.text(124, 189, b"EPR PDP", 1, MUTED);
        if let Some(pdp) = ext.epr_pdp {
            pd_number(p, 181, 189, u32::from(pdp) * 1000, b"W", 1, WATTS);
        } else {
            p.text(181, 189, b"---", 1, MUTED);
        }
    } else {
        let hint: &[u8] = match scan.state {
            9 => b"CHECK CC1 ORIENTATION",
            13 => b"DISCONNECT SRC AND LOAD",
            _ => b"NO EXTENDED CAPABILITIES SAVED",
        };
        p.text(8, 153, hint, 1, MUTED);
    }
    let hint: &[u8] = if matches!(scan.state, 22 | 23) {
        b"UNPLUG SRC AND POWER CYCLE T3P"
    } else {
        b"UNPLUG SRC BEFORE RETRY"
    };
    p.text(8, 207, hint, 1, MUTED);
}
