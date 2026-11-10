// SPDX-License-Identifier: GPL-2.0-or-later
use super::{AMPS, GRID, MUTED, PANEL, Strip, TEXT, VOLTS, sniff_count};
use crate::pd::{CABLE_MODE, Profiles, cable::CableIdentity};

#[cfg(feature = "pd-cable-read")]
mod detail;

pub(super) fn render(p: &mut Strip<'_>, pd: &Profiles) {
    let section = pd.cable_section;
    let title: &[u8] = match section {
        1 => b"RAW 2/3",
        2 => b"FLAGS 3/3",
        _ => b"SUMMARY 1/3",
    };
    #[cfg(feature = "pd-cable-read")]
    let extra = pd.cable.extra_section(section);
    #[cfg(feature = "pd-cable-read")]
    let (title, gap) = if pd.cable.sections() > 3 {
        let title = extra.map_or_else(
            || match section {
                1 => b"RAW" as &[u8],
                2 => b"FLAGS",
                _ => b"SUMMARY",
            },
            detail::title,
        );
        let digits = |n| [b'0' + n / 10, b'0' + n % 10];
        let [a, b] = digits(section + 1);
        let [c, d] = digits(pd.cable.sections());
        p.text(180, 35, &[a, b, b'/', c, d], 1, MUTED);
        (title, 20)
    } else {
        (title, 24)
    };
    #[cfg(not(feature = "pd-cable-read"))]
    let gap = 24;
    p.text(8, 35, title, 1, AMPS);
    p.text(80, 35, pd.status(CABLE_MODE), 1, TEXT);
    p.text(215, 35, b"CC1", 1, AMPS);
    sniff_count(p, 8, 207, b"SOP1 RX:", u16::from(pd.cable.sop_prime));

    let Some(identity) = pd.cable.identity else {
        p.outline(8, 55, 224, 141, GRID);
        if cfg!(feature = "pd-cable-read") {
            let hint: &[u8] = match (pd.mode, pd.state) {
                (_, 4 | 5 | 9 | 16) => b"POWER CYCLE T3P",
                (CABLE_MODE, 1) => b"READING SOP1",
                (CABLE_MODE, 11 | 12) => b"DISCONNECT SOURCE AND SINK",
                (CABLE_MODE, 13) => b"RA REQUIRED ON CC2",
                (CABLE_MODE, 14) => b"UNPLUG AND CHECK CABLE",
                _ => b"2X TO READ ON CC1",
            };
            p.text(18, 73, b"DISCOVER IDENTITY", 2, AMPS);
            p.text(18, 109, b"HID POWER. CABLE ONLY.", 1, TEXT);
            p.text(18, 132, b"LEAVE FAR END OPEN", 1, MUTED);
            p.text(18, 155, hint, 1, MUTED);
            #[cfg(feature = "pd-cable-read")]
            if let Some([cc1, cc2]) = pd.cable.cc_probe {
                p.text(18, 176, b"CC1", 1, AMPS);
                p.text(39, 172, &cc_voltage(cc1), 2, TEXT);
                p.text(126, 176, b"CC2", 1, AMPS);
                p.text(147, 172, &cc_voltage(cc2), 2, TEXT);
            }
            #[cfg(feature = "pd-cable-read")]
            if pd.cable.discovery.identity_result != crate::pd::cable::QueryResult::Ignore {
                p.text(91, 207, b"ID:", 1, MUTED);
                p.text(
                    111,
                    207,
                    detail::result(pd.cable.discovery.identity_result),
                    1,
                    TEXT,
                );
            }
        } else {
            p.text(18, 73, b"E-MARKER RX", 2, AMPS);
            p.text(18, 109, b"WAIT FOR SOURCE TO QUERY CABLE", 1, TEXT);
            p.text(
                18,
                132,
                if pd.mode == CABLE_MODE && pd.state == 1 {
                    b"RECONNECT SOURCE NOW"
                } else {
                    b"2X TO LISTEN FOR 30S"
                },
                1,
                MUTED,
            );
            p.text(18, 155, b"USE A KNOWN E-MARKED CABLE", 1, MUTED);
        }
        return;
    };

    #[cfg(feature = "pd-cable-read")]
    if let Some(page) = extra {
        detail::render(p, &pd.cable.discovery, identity, page);
        return;
    }
    match section {
        1 => raw(p, identity),
        2 => flags(p, identity),
        _ => {
            summary(p, identity, gap);
            #[cfg(feature = "pd-cable-read")]
            if gap == 20 {
                detail::summary(p, &pd.cable.discovery);
            }
        }
    }
}

#[cfg(feature = "pd-cable-read")]
fn cc_voltage(code: u8) -> [u8; 6] {
    match code {
        0 => *b" LOW  ",
        64 => *b" HIGH ",
        _ => {
            // Display the midpoint between two nominal 42 mV thresholds.
            let centivolts = (u16::from(code) * 42 + 26) / 10;
            [
                b' ',
                b'0' + (centivolts / 100) as u8,
                b'.',
                b'0' + (centivolts / 10 % 10) as u8,
                b'0' + (centivolts % 10) as u8,
                b'V',
            ]
        }
    }
}

fn row(p: &mut Strip<'_>, y: usize, label: &[u8], value: &[u8], color: u16) {
    p.rect(8, y, 224, 17, PANEL);
    p.text(12, y + 5, label, 1, MUTED);
    let (offset, scale) = if value.len() > 15 { (5, 1) } else { (2, 2) };
    p.text(81, y + offset, value, scale, color);
}

fn hex_digit(value: u32) -> u8 {
    b"0123456789ABCDEF"[(value & 15) as usize]
}

pub(super) fn hex<const N: usize>(value: u32) -> [u8; N] {
    let mut out = [0; N];
    for (i, digit) in out.iter_mut().rev().enumerate() {
        *digit = hex_digit(value >> (i * 4));
    }
    out
}

fn summary(p: &mut Strip<'_>, id: CableIdentity, gap: usize) {
    let words = id.words();
    let active = id.active();
    row(
        p,
        53,
        b"TYPE",
        match (active, id.vdo().is_some_and(|vdo| vdo.plug == 3)) {
            (true, true) => b"ACTIVE CAPTIVE",
            (false, true) => b"PASSIVE CAPTIVE",
            (true, false) => b"ACTIVE",
            (false, false) => b"PASSIVE",
        },
        AMPS,
    );
    let (vid, pid) = (hex::<4>(words[1]), hex::<4>(words[3] >> 16));
    let ids = [
        vid[0], vid[1], vid[2], vid[3], b'/', pid[0], pid[1], pid[2], pid[3],
    ];
    row(p, 53 + gap * 4, b"VID/PID", &ids, TEXT);
    row(p, 53 + gap * 5, b"XID", &hex::<8>(words[2]), TEXT);
    let Some(vdo) = id.vdo() else {
        row(p, 53 + gap, b"VDO LAYOUT", b"RAW ONLY", VOLTS);
        return;
    };
    let active_vdo2 = if active && !id.pd2() { words[5] } else { 0 };
    let two_lanes = !active || active_vdo2 & 8 != 0;
    let speed: &[u8] = match vdo.speed {
        0 if active_vdo2 & (1 << 5) == 0 => b"USB 2.0 480MBPS",
        1 => b"USB 3.2 GEN1",
        2 if id.pd2() => b"USB 3.2 GEN2",
        2 => match active_vdo2 & 0x110 {
            0 => b"USB3.2/USB4 G2",
            0x010 => b"USB4 GEN2",
            0x100 => b"USB 3.2 GEN2",
            _ => b"--",
        },
        3 if !id.pd2() && two_lanes => b"USB4 G3 40GBPS",
        3 if !id.pd2() => b"USB4 GEN3",
        4 if !id.pd2() && two_lanes => b"USB4 G4 80GBPS",
        4 if !id.pd2() => b"USB4 GEN4",
        _ => b"--",
    };
    row(p, 53 + gap, b"USB", speed, AMPS);

    let voltage: &[u8] = match vdo.voltage {
        3 => b"50",
        _ => b"20",
    };
    let current = match vdo.current {
        1 => b'3',
        2 => b'5',
        _ => b'-',
    };
    let rating = [
        voltage[0], voltage[1], b'V', b' ', current, b'A', b' ', b'E', b'P', b'R',
    ];
    let rating: &[u8] = if active && !vdo.vbus_through {
        b"NO VBUS"
    } else if id.pd2() && vdo.current == 0 {
        b"3A ASSUMED"
    } else if id.pd2() {
        &rating[4..6]
    } else if vdo.epr {
        &rating
    } else {
        &rating[..6]
    };
    row(p, 53 + gap * 2, b"RATING", rating, VOLTS);
    let length_range = [
        b'0' + vdo.latency.saturating_sub(1),
        b'-',
        b'0' + vdo.latency,
        b'M',
    ];
    let length: &[u8] = match vdo.latency {
        1..=7 => &length_range,
        8 if !active => b"OVER 7M",
        8 => b"100M",
        9 if active => b"200M",
        10 if active => b"300M",
        _ => b"--",
    };
    row(p, 53 + gap * 3, b"LENGTH EST", length, TEXT);
}

fn raw(p: &mut Strip<'_>, id: CableIdentity) {
    row(p, 53, b"PD HEADER", &hex::<4>(u32::from(id.header)), AMPS);
    let labels: [&[u8]; 7] = [
        b"VDM HEADER",
        b"ID HEADER",
        b"CERT STAT",
        b"PRODUCT",
        b"CABLE VDO1",
        b"CABLE VDO2",
        b"VDO 7",
    ];
    for (i, (&label, &word)) in labels.iter().zip(id.words()).enumerate() {
        row(p, 71 + i * 18, label, &hex::<8>(word), TEXT);
    }
}

fn codes(a: u8, b: u8, c: u8) -> [u8; 9] {
    [a, b' ', b'/', b' ', b, b' ', b'/', b' ', c]
}

fn flags(p: &mut Strip<'_>, id: CableIdentity) {
    let words = id.words();
    let (vdm, identity, product) = (words[0], words[1], words[3]);
    row(
        p,
        53,
        b"PD / SVDM",
        &codes(
            hex_digit(u32::from(id.header >> 6) & 3),
            hex_digit((vdm >> 13) & 3),
            hex_digit((vdm >> 11) & 3),
        ),
        TEXT,
    );
    row(
        p,
        71,
        b"HOST/DEV/MOD",
        &codes(
            hex_digit(identity >> 31),
            hex_digit((identity >> 30) & 1),
            hex_digit((identity >> 26) & 1),
        ),
        TEXT,
    );
    row(p, 89, b"BCD DEVICE", &hex::<4>(product), TEXT);
    let Some(vdo) = id.vdo() else {
        row(p, 107, b"VDO LAYOUT", b"RAW ONLY", VOLTS);
        return;
    };
    let digit = |value| hex_digit(u32::from(value));
    row(
        p,
        107,
        b"HW / FW / VDO",
        &codes(
            digit(vdo.hardware),
            digit(vdo.firmware),
            if id.pd2() { b'-' } else { digit(vdo.version) },
        ),
        TEXT,
    );
    row(
        p,
        125,
        b"PLUG/EPR/LAT",
        &codes(
            digit(vdo.plug),
            if id.pd2() {
                b'-'
            } else {
                digit(u8::from(vdo.epr))
            },
            digit(vdo.latency),
        ),
        TEXT,
    );
    row(
        p,
        143,
        b"TERM/VBUS/CUR",
        &codes(
            digit(vdo.termination),
            if id.pd2() { b'-' } else { digit(vdo.voltage) },
            digit(vdo.current),
        ),
        TEXT,
    );
    row(p, 161, b"SPEED CODE", &[digit(vdo.speed)], TEXT);
    if id.active() {
        row(
            p,
            179,
            b"SOP2 PRESENT",
            if vdo.sop2_present { b"YES" } else { b"NO" },
            TEXT,
        );
    }
}
