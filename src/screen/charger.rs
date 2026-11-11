// SPDX-License-Identifier: GPL-2.0-or-later
use super::cable::hex;
use super::{
    AMPS, GRID, MUTED, Overview, Strip, TEXT, VOLTS, WATTS, card, pd_number, spr_rows, value,
};
use crate::pd::charger::{CHARGER_SECTIONS, ChargerCapture, QueryResult};

pub(super) fn controls(view: &Overview) -> &'static [u8] {
    if view.charger.state == 1 {
        b"TAP SCAN HOLD CANCEL"
    } else if view.charger.has_offer() && view.charger_section + 1 < CHARGER_SECTIONS {
        b"TAP MORE 2X SCAN HOLD EXIT"
    } else {
        b"TAP NEXT 2X SCAN HOLD EXIT"
    }
}

pub(super) fn render(p: &mut Strip<'_>, view: &Overview) {
    let scan = &view.charger;
    p.text(215, 36, b"CC1", 1, AMPS);
    let section = view.charger_section;
    if section > 0 {
        let title: &[u8] = match section {
            1 => b"CAPABILITIES",
            2 => b"IDENTITY",
            3 => b"PEAKS AND MAKER",
            4 => b"POWER STATUS",
            5 => b"STATUS DETAILS",
            6 => b"SOURCE LIMITS",
            7 => b"SOURCE INFO",
            _ => b"QUERY RESULTS",
        };
        p.text(8, 36, title, 1, VOLTS);
        p.text(
            188,
            36,
            &[b'1' + section, b'/', b'0' + CHARGER_SECTIONS],
            1,
            MUTED,
        );
        match section {
            1 => {
                if let Some(caps) = &scan.caps {
                    spr_rows(p, caps);
                }
            }
            2 => identity(p, scan),
            3 => peaks(p, scan),
            4 => power_status(p, scan),
            5 => status_details(p, scan),
            6 => source_limits(p, scan),
            7 => source_info(p, scan),
            _ => query_results(p, scan),
        }
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
        2 => (b"QUERIES COMPLETE", WATTS),
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
        24 => (b"PARTIAL RESULTS", VOLTS),
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

// Keep the KWS section names and field order, with larger values on 240x240.
fn row(p: &mut Strip<'_>, index: usize, label: &[u8], text: &[u8], color: u16) {
    let y = 54 + index * 21;
    p.outline(8, y, 224, 20, color);
    p.text(12, y + 6, label, 1, MUTED);
    let scale = if text.len() <= 13 { 2 } else { 1 };
    p.text(94, y + if scale == 2 { 3 } else { 6 }, text, scale, color);
}

fn number(p: &mut Strip<'_>, index: usize, label: &[u8], n: u32, suffix: &[u8], color: u16) {
    let mut text = [0; 16];
    let end = text.len() - suffix.len();
    text[end..].copy_from_slice(suffix);
    let mut start = end;
    let mut n = n;
    loop {
        start -= 1;
        text[start] = b'0' + (n % 10) as u8;
        n /= 10;
        if n == 0 {
            break;
        }
    }
    row(p, index, label, &text[start..], color);
}

fn missing(p: &mut Strip<'_>, result: QueryResult) {
    p.outline(8, 57, 224, 128, GRID);
    p.text(18, 78, result.label(), 2, VOLTS);
    p.text(18, 117, b"NO VALUE RECEIVED", 1, MUTED);
    p.text(18, 145, b"SEE QUERY RESULTS", 1, AMPS);
}

fn identity(p: &mut Strip<'_>, scan: &ChargerCapture) {
    row(
        p,
        0,
        b"EXT CAPS",
        scan.queries[0].label(),
        if scan.extended.is_some() {
            WATTS
        } else {
            MUTED
        },
    );
    if let Some(r) = scan.revision_data {
        let rev = [
            b'0' + (r >> 28) as u8,
            b'.',
            b'0' + ((r >> 24) & 15) as u8,
            b' ',
            b'V',
            b'0' + ((r >> 20) & 15) as u8,
            b'.',
            b'0' + ((r >> 16) & 15) as u8,
        ];
        // Preserve unknown version encodings as hexadecimal.
        if [r >> 28, (r >> 24) & 15, (r >> 20) & 15, (r >> 16) & 15]
            .iter()
            .all(|n| *n < 10)
        {
            row(p, 1, b"REVISION", &rev, AMPS);
        } else {
            row(p, 1, b"REVISION RAW", &hex::<8>(r), AMPS);
        }
    } else {
        row(p, 1, b"REVISION", scan.queries[3].label(), MUTED);
    }
    if let Some(e) = scan.extended {
        let mut id = [b' '; 9];
        id[..4].copy_from_slice(&hex::<4>(u32::from(e.vid)));
        id[4] = b'/';
        id[5..].copy_from_slice(&hex::<4>(u32::from(e.pid)));
        row(p, 2, b"VID/PID", &id, TEXT);
        row(p, 3, b"XID", &hex::<8>(e.xid), TEXT);
        let mut versions = [b'/'; 5];
        versions[..2].copy_from_slice(&hex::<2>(u32::from(e.fw)));
        versions[3..].copy_from_slice(&hex::<2>(u32::from(e.hw)));
        row(p, 4, b"FW/HW", &versions, TEXT);
        if e.spr_pdp <= 100 {
            number(p, 5, b"SPR PDP", e.spr_pdp.into(), b"W", WATTS);
        } else {
            row(p, 5, b"SPR PDP", b"N/A", MUTED);
        }
        if let Some(pdp) = e.epr_pdp.filter(|p| *p <= 240) {
            number(p, 6, b"EPR PDP", pdp.into(), b"W", WATTS);
        } else {
            row(p, 6, b"EPR PDP", b"N/A", MUTED);
        }
    } else {
        p.text(8, 113, b"SEE QUERY RESULTS FOR EACH REPLY", 1, MUTED);
    }
}

fn peaks(p: &mut Strip<'_>, scan: &ChargerCapture) {
    let Some(e) = scan.extended else {
        missing(p, scan.queries[0]);
        return;
    };
    p.text(8, 52, b"PEAK   OVERLOAD   PERIOD   DUTY  DROOP", 1, MUTED);
    for (i, peak) in e.peaks.iter().enumerate() {
        let y = 70 + i * 20;
        p.text(12, y, &[b'P', b'1' + i as u8], 1, AMPS);
        // Fields describe permitted excursions, not measured current peaks.
        let fields = [
            u32::from((peak & 31).min(25)) * 10,
            u32::from((peak >> 5) & 63) * 20,
            u32::from((peak >> 11) & 15) * 5,
        ];
        for (col, n) in fields.iter().enumerate() {
            let x = [49, 104, 155][col];
            let mut digits = [b'0'; 4];
            let mut n = *n;
            for digit in digits.iter_mut().rev() {
                *digit += (n % 10) as u8;
                n /= 10;
            }
            p.text(x, y, &digits, 1, TEXT);
            p.text(x + 21, y, if col == 1 { b"MS" } else { b"%" }, 1, MUTED);
        }
        p.text(
            210,
            y,
            if peak & 0x8000 != 0 { b"Y" } else { b"N" },
            1,
            TEXT,
        );
    }
    p.rect(8, 128, 224, 1, GRID);
    p.text(8, 136, b"TOUCH TEMP", 1, MUTED);
    p.text(
        93,
        136,
        match e.touch_temp {
            1 => b"62368 TS1",
            2 => b"62368 TS2",
            _ => b"60950-1",
        },
        1,
        TEXT,
    );
    p.text(8, 151, b"SOURCE INPUT", 1, MUTED);
    p.text(
        93,
        151,
        if e.inputs & 1 != 0 {
            if e.inputs & 2 != 0 {
                b"EXTERNAL UNCONSTRAINED"
            } else {
                b"EXTERNAL CONSTRAINED"
            }
        } else {
            b"INTERNAL"
        },
        1,
        TEXT,
    );
    p.text(8, 166, b"BATTERY/SLOTS", 1, MUTED);
    let fixed = (e.batteries & 15).min(5);
    let slots = (e.batteries >> 4).min(5);
    p.text(
        93,
        166,
        &[
            b'0' + if fixed < 5 { fixed } else { 0 },
            b'/',
            b'0' + if slots < 5 { slots } else { 0 },
        ],
        1,
        TEXT,
    );
    p.text(8, 181, b"HOLDUP / LOAD", 1, MUTED);
    let load: &[u8] = if e.regulation & 3 == 1 {
        b"500 MA/US"
    } else {
        b"150 MA/US"
    };
    let hold = [
        b'0' + e.hold_ms / 100,
        b'0' + e.hold_ms / 10 % 10,
        b'0' + e.hold_ms % 10,
    ];
    p.text(93, 181, &hold, 1, TEXT);
    p.text(109, 181, b"MS", 1, MUTED);
    p.text(130, 181, load, 1, TEXT);
    p.text(8, 196, b"COMPLIANCE", 1, MUTED);
    p.text(
        93,
        196,
        &[
            if e.compliance & 1 != 0 { b'L' } else { b'-' },
            if e.compliance & 2 != 0 { b'1' } else { b'-' },
            if e.compliance & 4 != 0 { b'2' } else { b'-' },
        ],
        1,
        TEXT,
    );
    p.text(130, 196, b"TOUCH RAW", 1, MUTED);
    p.text(184, 196, &hex::<2>(e.touch.into()), 1, TEXT);
    p.text(8, 210, b"SOURCE-ADVERTISED LIMITS", 1, MUTED);
}

fn power_status(p: &mut Strip<'_>, scan: &ChargerCapture) {
    if let Some(info) = scan.info {
        row(
            p,
            0,
            b"SRC INFO",
            if info.first & (1 << 31) != 0 {
                b"GUARANTEED"
            } else {
                b"MANAGED"
            },
            WATTS,
        );
        number(p, 1, b"MAX PDP", (info.first >> 16) & 255, b"W", WATTS);
        number(p, 2, b"PRESENT PDP", (info.first >> 8) & 255, b"W", WATTS);
        number(p, 3, b"REPORTED PDP", info.first & 255, b"W", WATTS);
    } else {
        row(p, 0, b"SRC INFO", scan.queries[2].label(), MUTED);
        for (i, label) in [b"MAX PDP" as &[u8], b"PRESENT PDP", b"REPORTED PDP"]
            .iter()
            .enumerate()
        {
            row(p, i + 1, label, b"N/A", MUTED);
        }
    }
    if let Some(status) = scan.status {
        let b = status.bytes;
        row(
            p,
            4,
            b"STATUS",
            match (b[4] >> 1) & 3 {
                1 => b"NORMAL",
                2 => b"WARNING",
                3 => b"OVER TEMP",
                _ => b"N/A",
            },
            AMPS,
        );
        row(
            p,
            5,
            b"INPUT",
            match (b[1] >> 1) & 3 {
                0 => b"INTERNAL",
                1 => b"DC",
                3 => b"AC",
                _ => b"N/A",
            },
            TEXT,
        );
        if b[0] >= 2 {
            number(p, 6, b"INTERNAL TEMP", b[0].into(), b"C", VOLTS);
        } else {
            row(
                p,
                6,
                b"INTERNAL TEMP",
                if b[0] == 1 { b"<2C" } else { b"N/A" },
                MUTED,
            );
        }
    } else {
        row(p, 4, b"STATUS", scan.queries[1].label(), MUTED);
    }
    p.text(8, 207, b"PDP IS ADVERTISED / REPORTED", 1, MUTED);
}

fn status_details(p: &mut Strip<'_>, scan: &ChargerCapture) {
    if let Some(s) = scan.status {
        let b = s.bytes;
        row(
            p,
            0,
            b"OCP/OTP/OVP",
            &[
                if b[3] & 2 != 0 { b'Y' } else { b'-' },
                b'/',
                if b[3] & 4 != 0 { b'Y' } else { b'-' },
                b'/',
                if b[3] & 8 != 0 { b'Y' } else { b'-' },
            ],
            VOLTS,
        );
        let limits = [b'C', b'P', b'X', b'E', b'T'];
        let mut flags = [b'-'; 5];
        for i in 0..5 {
            if b[5] & (2 << i) != 0 {
                flags[i] = limits[i];
            }
        }
        row(p, 1, b"LIMIT CPXET", &flags, TEXT);
        if b[1] & 8 != 0 {
            row(p, 2, b"BAT INPUT RAW", &hex::<2>(u32::from(b[2])), TEXT);
        } else {
            row(p, 2, b"BAT INPUT", b"NOT PRESENT", MUTED);
        }
        let state: &[u8] = if s.len < 7 {
            b"N/A"
        } else {
            match b[6] & 7 {
                1 => b"S0",
                2 => b"MOD STANDBY",
                3 => b"S3",
                4 => b"S4",
                5 => b"S5",
                6 => b"G3",
                _ => b"N/A",
            }
        };
        row(p, 3, b"POWER STATE", state, TEXT);
        let led: &[u8] = if s.len < 7 {
            b"N/A"
        } else {
            match (b[6] >> 3) & 7 {
                0 => b"OFF",
                1 => b"ON",
                2 => b"BLINK",
                3 => b"BREATHE",
                _ => b"N/A",
            }
        };
        row(p, 4, b"DESIRED LED", led, TEXT);
    } else {
        row(p, 0, b"STATUS", scan.queries[1].label(), MUTED);
    }
    if let Some(s) = scan.status {
        row(
            p,
            5,
            b"BAT/OTHER IN",
            &[
                if s.bytes[1] & 8 != 0 { b'Y' } else { b'-' },
                b'/',
                if s.bytes[1] & 16 != 0 { b'Y' } else { b'-' },
            ],
            TEXT,
        );
        let mut raw = [b' '; 20];
        for i in 0..usize::from(s.len) {
            raw[i * 3..i * 3 + 2].copy_from_slice(&hex::<2>(u32::from(s.bytes[i])));
        }
        row(p, 6, b"SDB RAW", &raw[..usize::from(s.len) * 3 - 1], TEXT);
    }
    p.text(8, 207, b"C CABLE P PORT X EXT E EVENT T TEMP", 1, MUTED);
}

fn source_limits(p: &mut Strip<'_>, scan: &ChargerCapture) {
    let Some(e) = scan.extended else {
        missing(p, scan.queries[0]);
        return;
    };
    number(
        p,
        0,
        b"LOAD STEP",
        if e.regulation & 3 == 1 { 500 } else { 150 },
        b"MA/US",
        TEXT,
    );
    number(
        p,
        1,
        b"IOC LIMIT",
        if e.regulation & 4 != 0 { 90 } else { 25 },
        b"%",
        TEXT,
    );
    if e.hold_ms == 0 {
        row(p, 2, b"HOLDUP", b"N/A", MUTED);
    } else {
        number(p, 2, b"HOLDUP", e.hold_ms.into(), b"MS", TEXT);
    }
    row(
        p,
        3,
        b"LPS/PS1/PS2",
        &[
            if e.compliance & 1 != 0 { b'Y' } else { b'-' },
            b'/',
            if e.compliance & 2 != 0 { b'Y' } else { b'-' },
            b'/',
            if e.compliance & 4 != 0 { b'Y' } else { b'-' },
        ],
        TEXT,
    );
    row(
        p,
        4,
        b"TOUCH L/G/E",
        &[
            if e.touch & 1 != 0 { b'Y' } else { b'-' },
            b'/',
            if e.touch & 2 != 0 { b'Y' } else { b'-' },
            b'/',
            if e.touch & 4 != 0 { b'Y' } else { b'-' },
        ],
        TEXT,
    );
    row(
        p,
        5,
        b"INTERNAL BAT",
        if e.inputs & 4 != 0 { b"YES" } else { b"NO" },
        TEXT,
    );
    row(p, 6, b"REG RAW", &hex::<2>(e.regulation.into()), TEXT);
    p.text(8, 207, b"L LOW TOUCH G GROUND E EARTH", 1, MUTED);
}

fn source_info(p: &mut Strip<'_>, scan: &ChargerCapture) {
    if let Some(info) = scan.info {
        row(p, 0, b"SIDO 1 RAW", &hex::<8>(info.first), TEXT);
        if let Some(second) = info.second {
            row(p, 1, b"SIDO 2 RAW", &hex::<8>(second), TEXT);
            row(
                p,
                2,
                b"PDP TYPE",
                if second & (1 << 31) != 0 {
                    b"GUARANTEED"
                } else {
                    b"MANAGED"
                },
                WATTS,
            );
            row(
                p,
                3,
                b"DYNAMIC PDP",
                if second & (1 << 30) != 0 {
                    b"YES"
                } else {
                    b"NO"
                },
                TEXT,
            );
            for (i, n) in [(second >> 9) & 511, second & 511].iter().enumerate() {
                let value = [
                    b'0' + (n / 2 / 100) as u8,
                    b'0' + (n / 2 / 10 % 10) as u8,
                    b'0' + (n / 2 % 10) as u8,
                    b'.',
                    if n % 2 == 0 { b'0' } else { b'5' },
                    b'W',
                ];
                row(
                    p,
                    4 + i,
                    if i == 0 { b"MAX PDP" } else { b"GUARANTEED" },
                    &value,
                    WATTS,
                );
            }
        } else {
            row(p, 1, b"SIDO 2", b"NOT RETURNED", MUTED);
        }
    } else {
        row(p, 0, b"SOURCE INFO", scan.queries[2].label(), MUTED);
    }
    if let Some(r) = scan.revision_data {
        row(p, 6, b"REVISION RAW", &hex::<8>(r), TEXT);
    } else {
        row(p, 6, b"REVISION", scan.queries[3].label(), MUTED);
    }
}

fn query_results(p: &mut Strip<'_>, scan: &ChargerCapture) {
    for (i, label) in [
        b"EXT CAPS" as &[u8],
        b"GET STATUS",
        b"SOURCE INFO",
        b"REVISION",
    ]
    .iter()
    .enumerate()
    {
        let result = scan.queries[i];
        row(
            p,
            i,
            label,
            result.label(),
            if result == QueryResult::Ok {
                WATTS
            } else {
                MUTED
            },
        );
    }
    p.text(8, 153, b"FIXED 5V / <=0.5A REQUEST", 1, AMPS);
    p.text(8, 170, b"QC / SCP / FCP / AFC: NOT SCANNED", 1, MUTED);
    p.text(8, 187, b"NO AUTOMATIC VOLTAGE SWEEP", 1, MUTED);
    p.text(8, 207, b"UNPLUG SRC BEFORE RETRY", 1, MUTED);
}
