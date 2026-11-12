// SPDX-License-Identifier: GPL-2.0-or-later
mod cable;
#[cfg(feature = "flash-reader")]
pub(crate) mod flash_reader;
#[cfg(feature = "capture-probe")]
pub(crate) mod capture_probe;
#[cfg(feature = "system")]
mod system;
#[cfg(feature = "system")]
use crate::SYSTEM_PAGE;
#[cfg(feature = "pd-charger")]
mod charger;
#[cfg(feature = "pd-rd-test")]
mod rd_test;

#[cfg(feature = "pd-charger")]
use crate::pd::CHARGER_PAGE;
use crate::pd::{
    CABLE_PAGE, EPR_PAGE, PD_PAGE_COUNT, PROFILES_PAGE, Pdo, Profiles, SNIFF_PAGE, SourceCaps,
};
use crate::{Overview, SIDE, STRIP_ROWS};

const BG: u16 = 0x0000;
const PANEL: u16 = BG;
const GRID: u16 = 0x19c8;
const TEXT: u16 = 0xffff;
const MUTED: u16 = 0xb65a;
const VOLTS: u16 = 0xff05;
const AMPS: u16 = 0x3e9f;
const WATTS: u16 = 0x4f8c;
const PURPLE: u16 = 0xc37f;

struct Strip<'a> {
    pixels: &'a mut [u16; SIDE * STRIP_ROWS],
    top: usize,
}

impl Strip<'_> {
    fn rect(&mut self, x: usize, y: usize, w: usize, h: usize, color: u16) {
        let left = x.min(SIDE);
        let right = (x + w).min(SIDE);
        let start = y.max(self.top);
        let end = (y + h).min(self.top + STRIP_ROWS);
        for row in start..end {
            let offset = (row - self.top) * SIDE;
            self.pixels[offset + left..offset + right].fill(color);
        }
    }

    fn outline(&mut self, x: usize, y: usize, w: usize, h: usize, color: u16) {
        self.rect(x, y, w, h, PANEL);
        self.rect(x, y, w, 1, color);
        self.rect(x, y + h - 1, w, 1, color);
        self.rect(x, y, 1, h, color);
        self.rect(x + w - 1, y, 1, h, color);
    }

    fn text(&mut self, x: usize, y: usize, value: &[u8], scale: usize, color: u16) {
        if y >= self.top + STRIP_ROWS || y + 7 * scale <= self.top {
            return;
        }
        for (index, &ch) in value.iter().enumerate() {
            let rows = glyph(ch);
            for row in 0..7 {
                for col in 0..4 {
                    if rows & (1 << (4 * (6 - row) + 3 - col)) != 0 {
                        self.rect(
                            x + (index * 5 + col) * scale,
                            y + row * scale,
                            scale,
                            scale,
                            color,
                        );
                    }
                }
            }
        }
    }
}

const fn rows(r: [u8; 7]) -> u32 {
    ((r[0] as u32) << 24)
        | ((r[1] as u32) << 20)
        | ((r[2] as u32) << 16)
        | ((r[3] as u32) << 12)
        | ((r[4] as u32) << 8)
        | ((r[5] as u32) << 4)
        | r[6] as u32
}

// Compact 4x7 typeface for the 240-pixel panel.
fn glyph(ch: u8) -> u32 {
    match ch {
        b'0' => rows([6, 9, 11, 13, 9, 9, 6]),
        b'1' => rows([2, 6, 2, 2, 2, 2, 7]),
        b'2' => rows([6, 9, 1, 2, 4, 8, 15]),
        b'3' => rows([14, 1, 1, 6, 1, 1, 14]),
        b'4' => rows([2, 6, 10, 10, 15, 2, 2]),
        b'5' => rows([15, 8, 8, 14, 1, 1, 14]),
        b'6' => rows([6, 8, 8, 14, 9, 9, 6]),
        b'7' => rows([15, 1, 2, 2, 4, 4, 4]),
        b'8' => rows([6, 9, 9, 6, 9, 9, 6]),
        b'9' => rows([6, 9, 9, 7, 1, 1, 6]),
        b'A' => rows([6, 9, 9, 15, 9, 9, 9]),
        b'B' => rows([14, 9, 9, 14, 9, 9, 14]),
        b'C' => rows([7, 8, 8, 8, 8, 8, 7]),
        b'D' => rows([14, 9, 9, 9, 9, 9, 14]),
        b'E' => rows([15, 8, 8, 14, 8, 8, 15]),
        b'F' => rows([15, 8, 8, 14, 8, 8, 8]),
        b'G' => rows([7, 8, 8, 11, 9, 9, 7]),
        b'H' => rows([9, 9, 9, 15, 9, 9, 9]),
        b'I' => rows([7, 2, 2, 2, 2, 2, 7]),
        b'J' => rows([3, 1, 1, 1, 9, 9, 6]),
        b'K' => rows([9, 10, 12, 8, 12, 10, 9]),
        b'L' => rows([8, 8, 8, 8, 8, 8, 15]),
        b'M' => rows([9, 15, 15, 9, 9, 9, 9]),
        b'N' => rows([9, 13, 13, 11, 11, 9, 9]),
        b'O' => rows([6, 9, 9, 9, 9, 9, 6]),
        b'P' => rows([14, 9, 9, 14, 8, 8, 8]),
        b'Q' => rows([6, 9, 9, 9, 11, 10, 5]),
        b'R' => rows([14, 9, 9, 14, 12, 10, 9]),
        b'S' => rows([7, 8, 8, 6, 1, 1, 14]),
        b'T' => rows([15, 2, 2, 2, 2, 2, 2]),
        b'U' => rows([9, 9, 9, 9, 9, 9, 6]),
        b'V' => rows([9, 9, 9, 9, 9, 6, 6]),
        b'W' => rows([9, 9, 9, 9, 15, 15, 9]),
        b'X' => rows([9, 9, 6, 6, 6, 9, 9]),
        b'Y' => rows([9, 9, 6, 2, 2, 2, 2]),
        b'Z' => rows([15, 1, 2, 4, 8, 8, 15]),
        b'-' => rows([0, 0, 0, 15, 0, 0, 0]),
        b'.' => rows([0, 0, 0, 0, 0, 6, 6]),
        b':' => rows([0, 6, 6, 0, 6, 6, 0]),
        b'/' => rows([1, 1, 2, 2, 4, 8, 8]),
        b'+' => rows([0, 2, 2, 15, 2, 2, 0]),
        b'%' => rows([9, 10, 2, 4, 4, 5, 9]),
        b'<' => rows([1, 2, 4, 8, 4, 2, 1]),
        b'=' => rows([0, 0, 15, 0, 15, 0, 0]),
        b'_' => rows([0, 0, 0, 0, 0, 0, 15]),
        _ => 0,
    }
}

fn milli(value: u64, negative: bool, output: &mut [u8; 11]) -> &[u8] {
    let mut value = value.min(999_999_999);
    let mut start = output.len();
    for _ in 0..3 {
        start -= 1;
        output[start] = b'0' + (value % 10) as u8;
        value /= 10;
    }
    start -= 1;
    output[start] = b'.';
    loop {
        start -= 1;
        output[start] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 {
            break;
        }
    }
    if negative {
        start -= 1;
        output[start] = b'-';
    }
    &output[start..]
}

// Value and unit always fit the card, including signed extremes.
fn value(
    p: &mut Strip<'_>,
    x: usize,
    y: usize,
    width: usize,
    reading: Option<(u64, bool)>,
    unit: &[u8],
    color: u16,
    preferred: usize,
) {
    let mut buffer = [0; 11];
    let number = if let Some((n, negative)) = reading {
        milli(n, negative, &mut buffer)
    } else {
        b"---"
    };
    let mut scale = preferred;
    while scale > 1 && number.len() * 5 * scale + unit.len() * 5 + 2 > width {
        scale -= 1;
    }
    p.text(x, y, number, scale, color);
    let ux = x + number.len() * 5 * scale + 2;
    p.text(ux, y + 7 * (scale - 1), unit, 1, color);
}

fn time_text(seconds: u32) -> [u8; 8] {
    let hours = seconds / 3600;
    let mins = seconds / 60 % 60;
    let secs = seconds % 60;
    [
        b'0' + (hours / 10) as u8,
        b'0' + (hours % 10) as u8,
        b':',
        b'0' + (mins / 10) as u8,
        b'0' + (mins % 10) as u8,
        b':',
        b'0' + (secs / 10) as u8,
        b'0' + (secs % 10) as u8,
    ]
}

// Round to an easy-to-read upper limit, retaining a 1-unit minimum.
pub(crate) fn graph_scale(max: u32) -> u32 {
    let mut unit = 1000u32;
    loop {
        for candidate in [
            unit,
            unit.saturating_mul(2),
            unit.saturating_mul(5) / 2,
            unit.saturating_mul(5),
        ] {
            if max <= candidate {
                return candidate;
            }
        }
        let next = unit.saturating_mul(10);
        if next == unit {
            return u32::MAX;
        }
        unit = next;
    }
}

fn header(p: &mut Strip<'_>, view: &Overview) {
    let (title, color): (&[u8], u16) = match view.page {
        0 => (b"OVERVIEW", AMPS),
        1 => (b"ENERGY", WATTS),
        3 => (b"GRAPH", VOLTS),
        4 => (b"GRAPH", AMPS),
        PROFILES_PAGE => (b"PD PROFILES", WATTS),
        EPR_PAGE => (b"EPR OFFER", PURPLE),
        SNIFF_PAGE => (b"PD SNIFF", AMPS),
        CABLE_PAGE => (b"CABLE", AMPS),
        #[cfg(feature = "pd-charger")]
        CHARGER_PAGE => (b"CHARGER", VOLTS),
        #[cfg(feature = "system")]
        SYSTEM_PAGE => (b"SYSTEM", AMPS),
        _ => (b"GRAPH", WATTS),
    };
    // The selected title and neighboring color tabs follow the KWS layout.
    // Keep the uncalibrated indicator visible on the square T3P display.
    let title_width = title.len() * 10 + 8;
    p.rect(8, 6, title_width, 18, color);
    p.text(12, 8, title, 2, BG);
    let pd_tabs = view.pd_profiles.is_some();
    let tabs = if pd_tabs {
        PD_PAGE_COUNT + cfg!(feature = "system") as u32
    } else {
        PROFILES_PAGE
    };
    let start = 12 + title_width;
    let stride = ((178 - start) / tabs as usize).clamp(3, 7);
    for index in 0..tabs {
        let tab_color = match index {
            1 | 2 | PROFILES_PAGE => WATTS,
            3 => VOLTS,
            EPR_PAGE => PURPLE,
            #[cfg(feature = "pd-charger")]
            CHARGER_PAGE => VOLTS,
            _ => AMPS,
        };
        p.rect(
            start + index as usize * stride,
            if view.page == index { 8 } else { 10 },
            stride - 1,
            if view.page == index { 14 } else { 10 },
            tab_color,
        );
    }
    p.text(
        182,
        10,
        if view.uncalibrated {
            b"UNCAL"
        } else {
            b"DEMO "
        },
        2,
        VOLTS,
    );
    p.rect(8, 28, 224, 1, color);
    p.rect(8, 219, 224, 1, GRID);
    p.text(
        8,
        226,
        match view.page {
            SNIFF_PAGE
                if view
                    .pd_profiles
                    .as_ref()
                    .and_then(|pd| pd.sniff)
                    .is_some_and(|sniff| sniff.start > 0) =>
            {
                b"TAP OLDER 2X CAP HOLD EXIT"
            }
            SNIFF_PAGE => b"TAP NEXT 2X CAP HOLD EXIT",
            CABLE_PAGE
                if view
                    .pd_profiles
                    .as_ref()
                    .is_some_and(|pd| pd.cable_section + 1 < pd.cable.sections()) =>
            {
                if cfg!(feature = "pd-cable-read") {
                    b"TAP MORE 2X READ HOLD EXIT"
                } else {
                    b"TAP MORE 2X CAP HOLD EXIT"
                }
            }
            CABLE_PAGE if cfg!(feature = "pd-cable-read") => b"TAP NEXT 2X READ HOLD EXIT",
            CABLE_PAGE => b"TAP NEXT 2X CAP HOLD EXIT",
            #[cfg(feature = "pd-charger")]
            CHARGER_PAGE => charger::controls(view),
            #[cfg(feature = "system")]
            SYSTEM_PAGE if view.system_section + 1 < crate::system::SYSTEM_SECTIONS => {
                b"TAP MORE 2X PAGE HOLD EXIT"
            }
            #[cfg(feature = "system")]
            SYSTEM_PAGE => b"TAP NEXT 2X PAGE HOLD EXIT",
            PROFILES_PAGE | EPR_PAGE => b"TAP NEXT 2X CAPTURE HOLD RESET",
            2..PROFILES_PAGE => b"TAP NEXT 2X RANGE HOLD RESET",
            _ => b"TAP NEXT  HOLD RESET",
        },
        1,
        MUTED,
    );
    p.text(186, 226, b"RAM ONLY", 1, MUTED);
}

fn card(p: &mut Strip<'_>, x: usize, y: usize, w: usize, h: usize, title: &[u8], color: u16) {
    p.outline(x, y, w, h, color);
    p.text(x + 5, y + 4, title, 1, color);
}

fn overview(p: &mut Strip<'_>, view: &Overview) {
    card(p, 8, 35, 109, 57, b"VOLTAGE", VOLTS);
    card(p, 123, 35, 109, 57, b"CURRENT", AMPS);
    card(p, 8, 99, 134, 76, b"POWER", WATTS);
    card(p, 148, 99, 84, 76, b"MAX", PURPLE);
    value(
        p,
        14,
        56,
        97,
        view.reading.map(|r| (u64::from(r.mv), false)),
        b"V",
        VOLTS,
        3,
    );
    value(
        p,
        129,
        56,
        97,
        view.reading
            .map(|r| (u64::from(r.ma.unsigned_abs()), r.ma < 0)),
        b"A",
        AMPS,
        3,
    );
    value(
        p,
        14,
        128,
        122,
        view.reading
            .map(|r| (u64::from(r.magnitude_mw()), r.ma < 0)),
        b"W",
        WATTS,
        4,
    );
    value(
        p,
        154,
        116,
        72,
        view.peaks.map(|(v, _, _)| (u64::from(v), false)),
        b"V",
        VOLTS,
        2,
    );
    value(
        p,
        154,
        135,
        72,
        view.peaks.map(|(_, a, _)| (u64::from(a), false)),
        b"A",
        AMPS,
        2,
    );
    value(
        p,
        154,
        154,
        72,
        view.peaks.map(|(_, _, w)| (u64::from(w), false)),
        b"W",
        WATTS,
        2,
    );

    card(p, 8, 181, 224, 31, b"PROTOCOL", AMPS);
    if let Some(registers) = view.pd_diagnostic {
        p.text(14, 196, b"PD", 1, AMPS);
        if let Some((first, last)) = registers {
            let digits = b"0123456789ABCDEF";
            for index in 0..5 {
                let byte = if index == 4 {
                    last
                } else {
                    (first >> (index * 8)) as u8
                };
                p.text(
                    29 + index * 15,
                    196,
                    &digits[(byte >> 4) as usize..][..1],
                    1,
                    TEXT,
                );
                p.text(
                    34 + index * 15,
                    196,
                    &digits[(byte & 15) as usize..][..1],
                    1,
                    TEXT,
                );
            }
        } else {
            p.text(29, 196, b"-- -- -- -- --", 1, TEXT);
        }
    } else if let Some(profiles) = &view.pd_profiles {
        p.text(14, 196, b"PD PROFILES", 1, AMPS);
        p.text(84, 196, profiles.status(profiles.mode), 1, TEXT);
    } else {
        p.text(14, 196, b"PD NOT AVAILABLE", 1, MUTED);
    }
}

// PDO fields use coarser units than the meter, so two decimals are sufficient.
fn pd_number(
    p: &mut Strip<'_>,
    x: usize,
    y: usize,
    milli_units: u32,
    suffix: &[u8],
    scale: usize,
    color: u16,
) -> usize {
    let mut buffer = [0; 11];
    let number = milli(u64::from(milli_units), false, &mut buffer);
    let number = &number[..number.len() - 1];
    p.text(x, y, number, scale, color);
    p.text(x + number.len() * 5 * scale, y, suffix, scale, color);
    x + (number.len() + suffix.len()) * 5 * scale
}

fn profiles(p: &mut Strip<'_>, pd: &Profiles) {
    p.text(8, 36, b"SPR OFFER", 1, WATTS);
    p.text(94, 36, pd.status(0), 1, TEXT);
    if pd.mode == 0 && pd.cc != 0 {
        p.text(215, 36, b"CC1", 1, AMPS);
    }
    let Some(caps) = pd.caps else {
        p.outline(8, 54, 224, 142, GRID);
        p.text(18, 70, b"RX ONLY RD ON", 2, WATTS);
        let state = if pd.mode == 0 || matches!(pd.state, 4 | 5 | 9) {
            pd.state
        } else {
            0
        };
        let (message, hint): (&[u8], &[u8]) = match state {
            0 => (b"2X TO START ON CC1", b"USE A 5V TEST SETUP"),
            1 => (b"WAITING FOR SOURCE CAPABILITIES", b"2X TO RESTART CAPTURE"),
            4 | 5 | 9 => (b"CAPTURE UNAVAILABLE", b"POWER CYCLE TO RETRY"),
            _ => (b"NO OFFER SAVED", b"2X TO RETRY ON CC1"),
        };
        p.text(18, 108, message, 1, TEXT);
        p.text(18, 129, hint, 1, MUTED);
        return;
    };
    spr_rows(p, &caps);
}

fn spr_rows(p: &mut Strip<'_>, caps: &SourceCaps) {
    let mut has_avs = false;
    for (index, &word) in caps.rows().iter().enumerate() {
        let top = 54 + index * 21;
        let pdo = Pdo::decode(word);
        has_avs |= matches!(
            pdo,
            Pdo::SprAvs {
                ma_above_15v: 1..,
                ..
            }
        );
        if top >= p.top + STRIP_ROWS || top + 20 <= p.top {
            continue;
        }
        p.outline(8, top, 224, 20, GRID);
        let y = top + 3;
        p.text(13, y + 4, &[b'1' + index as u8], 1, MUTED);
        match pdo {
            Pdo::Fixed { mv, ma } => {
                p.text(26, y + 4, b"FIX", 1, MUTED);
                pd_number(p, 52, y, mv, b"V", 2, VOLTS);
                pd_number(p, 124, y, ma, b"A", 2, AMPS);
                pd_number(p, 192, y + 4, mv * ma / 1000, b"W", 1, WATTS);
            }
            Pdo::Pps { min_mv, max_mv, ma } => {
                p.text(26, y + 4, b"PPS", 1, MUTED);
                let end = pd_number(p, 52, y + 4, min_mv, b"-", 1, VOLTS);
                pd_number(p, end, y + 4, max_mv, b"V", 1, VOLTS);
                pd_number(p, 124, y, ma, b"A", 2, AMPS);
            }
            Pdo::SprAvs {
                ma_to_15v,
                ma_above_15v,
            } => {
                p.text(26, y + 4, b"AVS", 1, MUTED);
                if ma_above_15v == 0 {
                    p.text(52, y, b"9-15V", 2, VOLTS);
                    pd_number(p, 124, y, ma_to_15v, b"A", 2, AMPS);
                } else {
                    p.text(52, y, b"9-20V", 2, VOLTS);
                    let end = pd_number(p, 124, y + 4, ma_to_15v, b"/", 1, AMPS);
                    pd_number(p, end, y + 4, ma_above_15v, b"A", 1, AMPS);
                }
            }
            Pdo::Raw(_) | Pdo::EprAvs { .. } => {
                p.text(26, y + 4, b"RAW", 1, MUTED);
                let digits = b"0123456789ABCDEF";
                let mut hex = [0; 8];
                for (i, digit) in hex.iter_mut().enumerate() {
                    *digit = digits[((word >> (28 - i * 4)) & 15) as usize];
                }
                p.text(52, y, &hex, 2, TEXT);
            }
        }
    }
    p.text(
        8,
        204,
        if has_avs {
            b"AVS A: 9-15V / 15-20V"
        } else {
            b"SOURCE ADVERTISED VALUES"
        },
        1,
        MUTED,
    );
}

fn pd_count(p: &mut Strip<'_>, x: usize, y: usize, label: &[u8], value: u8, scale: usize) {
    let digits = [
        b'0' + value / 100,
        b'0' + value / 10 % 10,
        b'0' + value % 10,
    ];
    p.text(x, y, label, scale, MUTED);
    p.text(x + label.len() * 5 * scale, y, &digits, scale, TEXT);
}

fn epr_profiles(p: &mut Strip<'_>, pd: &Profiles) {
    p.text(8, 36, b"EPR PDO 8-11", 1, PURPLE);
    p.text(94, 36, pd.status(1), 1, TEXT);
    if pd.mode == 1 && pd.cc != 0 {
        p.text(215, 36, b"CC1", 1, AMPS);
    }
    let Some(caps) = pd.epr else {
        p.outline(8, 54, 224, 142, GRID);
        p.text(18, 70, b"RX ONLY RD ON", 2, PURPLE);
        let state = if pd.mode == 1 || matches!(pd.state, 4 | 5 | 9) {
            pd.state
        } else {
            0
        };
        let (message, hint): (&[u8], &[u8]) = match state {
            1 => (b"WAITING FOR EPR OFFER", b"QUERY FROM A PD SINK"),
            3 => (b"NO EPR OFFER RECEIVED", b"2X TO RETRY ON CC1"),
            4 | 5 | 9 => (b"CAPTURE UNAVAILABLE", b"POWER CYCLE TO RETRY"),
            6 => (b"RX FIFO FULL", b"PACKETS MAY BE LOST"),
            7 => (b"BAD RX FRAME", b"2X TO RETRY"),
            8 => (b"LINK RESET", b"2X TO RETRY"),
            _ => (b"2X TO LISTEN FOR EPR OFFER", b"QUERY FROM A PD SINK"),
        };
        p.text(18, 108, message, 1, TEXT);
        p.text(18, 129, hint, 1, MUTED);
        let [rx, spr, chunk0, chunk1] = pd.epr_counters.to_le_bytes();
        pd_count(p, 18, 149, b"RX:", rx, 2);
        pd_count(p, 130, 149, b"SPR:", spr, 2);
        pd_count(p, 18, 171, b"E0:", chunk0, 2);
        pd_count(p, 130, 171, b"E1:", chunk1, 2);
        return;
    };
    if caps.rows().is_empty() {
        p.text(16, 70, b"NO EPR PDO IN OFFER", 1, MUTED);
    }
    for (index, &word) in caps.rows().iter().enumerate() {
        let top = 54 + index * 30;
        if top >= p.top + STRIP_ROWS || top + 25 <= p.top {
            continue;
        }
        p.outline(8, top, 224, 25, GRID);
        let y = top + 5;
        let numbers: [&[u8]; 4] = [b"8", b"9", b"10", b"11"];
        p.text(12, y + 4, numbers[index], 1, MUTED);
        match Pdo::decode(word) {
            Pdo::Fixed { mv, ma } if word != 0 => {
                p.text(27, y + 4, b"FIX", 1, MUTED);
                pd_number(p, 52, y, mv, b"V", 2, VOLTS);
                pd_number(p, 124, y, ma, b"A", 2, AMPS);
                pd_number(p, 192, y + 4, mv * ma / 1000, b"W", 1, WATTS);
            }
            Pdo::EprAvs {
                min_mv,
                max_mv,
                pdp_w,
            } => {
                p.text(27, y + 4, b"AVS", 1, MUTED);
                let end = pd_number(p, 52, y + 4, min_mv, b"-", 1, VOLTS);
                pd_number(p, end, y + 4, max_mv, b"V", 1, VOLTS);
                p.text(124, y + 4, b"PDP", 1, MUTED);
                pd_number(p, 146, y, pdp_w * 1000, b"W", 2, WATTS);
            }
            _ => {
                p.text(27, y + 4, b"RAW", 1, MUTED);
                let digits = b"0123456789ABCDEF";
                let mut hex = [0; 8];
                for (i, digit) in hex.iter_mut().enumerate() {
                    *digit = digits[((word >> (28 - i * 4)) & 15) as usize];
                }
                p.text(52, y, &hex, 2, TEXT);
            }
        }
    }
    let [rx, spr, chunk0, chunk1] = pd.epr_counters.to_le_bytes();
    pd_count(p, 8, 183, b"RX:", rx, 1);
    pd_count(p, 65, 183, b"SPR:", spr, 1);
    pd_count(p, 110, 183, b"E0:", chunk0, 1);
    pd_count(p, 170, 183, b"E1:", chunk1, 1);
    p.text(
        8,
        204,
        if caps.complete {
            b"SOURCE ADVERTISED VALUES"
        } else {
            b"CHUNK 1 ONLY - PARTIAL"
        },
        1,
        MUTED,
    );
}

fn sniff_count(p: &mut Strip<'_>, x: usize, y: usize, label: &[u8], value: u16) {
    let value = value.min(999);
    let digits = [
        b'0' + (value / 100) as u8,
        b'0' + (value / 10 % 10) as u8,
        b'0' + (value % 10) as u8,
    ];
    p.text(x, y, label, 1, MUTED);
    p.text(x + label.len() * 5, y, &digits, 1, TEXT);
}

fn pd_sniff(p: &mut Strip<'_>, pd: &Profiles) {
    p.text(8, 35, b"PD LOG", 1, AMPS);
    p.text(69, 35, pd.status(2), 1, TEXT);
    if pd.mode == 2 && pd.cc != 0 {
        p.text(215, 35, b"CC1", 1, AMPS);
    }
    let (total, good_crc, keepalive, keepalive_ack) = pd
        .sniff
        .map(|sniff| {
            (
                sniff.total,
                sniff.good_crc,
                sniff.keepalive,
                sniff.keepalive_ack,
            )
        })
        .unwrap_or((0, 0, 0, 0));
    sniff_count(p, 8, 46, b"MSG:", total);
    sniff_count(p, 83, 46, b"GCRC:", good_crc);
    sniff_count(p, 8, 207, b"EPR KEEPALIVE:", keepalive);
    sniff_count(p, 105, 207, b"ACK:", keepalive_ack);
    if pd.sniff.is_none_or(|sniff| sniff.end == 0) {
        p.outline(8, 60, 224, 141, GRID);
        p.text(18, 75, b"RX ONLY RD ON", 2, AMPS);
        p.text(
            18,
            112,
            if keepalive != 0 || keepalive_ack != 0 {
                b"EPR KEEPALIVE ONLY"
            } else if pd.mode == 2 && pd.state == 1 {
                b"WAITING FOR PD TRAFFIC"
            } else {
                b"NO LOGGED MESSAGES"
            },
            1,
            TEXT,
        );
        p.text(18, 133, b"GOODCRC COUNTED SEPARATELY", 1, MUTED);
        p.text(
            18,
            154,
            if pd.mode == 2 && pd.state == 1 {
                b"LISTENING FOR 30S"
            } else {
                b"2X TO CAPTURE FOR 30S"
            },
            1,
            MUTED,
        );
        return;
    }
    let sniff = pd.sniff.unwrap();
    let decimal = |value: u8| {
        [
            b'0' + value / 100,
            b'0' + value / 10 % 10,
            b'0' + value % 10,
        ]
    };
    let first = decimal(sniff.start + 1);
    let last = decimal(sniff.end);
    let retained = decimal(sniff.retained);
    let range = [
        b'R',
        b':',
        first[0],
        first[1],
        first[2],
        b'-',
        last[0],
        last[1],
        last[2],
        b'/',
        retained[0],
        retained[1],
        retained[2],
    ];
    p.text(164, 46, &range, 1, MUTED);
    for (index, event) in sniff
        .rows
        .iter()
        .copied()
        .take(usize::from(sniff.end - sniff.start))
        .enumerate()
    {
        let y = 59 + index * 18;
        p.rect(8, y, 224, 16, PANEL);
        p.text(
            12,
            y + 1,
            if event.source() { b"SRC" } else { b"SNK" },
            2,
            if event.source() { VOLTS } else { AMPS },
        );
        if let Some((action, data, id)) = event.epr_mode() {
            let hex = |value: u8| b"0123456789ABCDEF"[usize::from(value & 15)];
            let raw_action = [b'E', b'P', b'R', b'_', b'A', hex(action >> 4), hex(action)];
            let label: &[u8] = match action {
                1 => b"EPR_ENTER",
                2 => b"EPR_ACK",
                3 => b"EPR_OK",
                4 => b"EPR_FAIL",
                5 => b"EPR_EXIT",
                _ => &raw_action,
            };
            p.text(52, y + 1, label, 2, TEXT);
            p.text(159, y + 1, &[b'I', b'0', b'0' + id], 2, MUTED);
            p.text(199, y + 1, &[b'D', hex(data >> 4), hex(data)], 2, MUTED);
        } else {
            let (label, suffix, value) = event.label();
            p.text(52, y + 1, label, 2, TEXT);
            let detail = [suffix, b'0' + value / 10, b'0' + value % 10];
            p.text(199, y + 1, &detail, 2, MUTED);
        }
    }
}

fn energy(p: &mut Strip<'_>, view: &Overview) {
    card(p, 8, 35, 224, 55, b"CHARGE", AMPS);
    card(p, 8, 96, 224, 55, b"ENERGY", WATTS);
    card(p, 8, 157, 224, 55, b"TIME", PURPLE);
    value(p, 15, 55, 210, Some((view.mah, false)), b"AH", AMPS, 3);
    value(p, 15, 116, 210, Some((view.mwh, false)), b"WH", WATTS, 3);
    p.text(15, 177, &time_text(view.elapsed_seconds), 3, TEXT);
}

fn graph(p: &mut Strip<'_>, view: &Overview) {
    let (title, unit, color): (&[u8], &[u8], u16) = match view.page {
        3 => (b"VOLTAGE", b"V", VOLTS),
        4 => (b"CURRENT", b"A", AMPS),
        _ => (b"POWER", b"W", WATTS),
    };
    let live = view.reading.map(|r| match view.page {
        3 => (u64::from(r.mv), false),
        4 => (u64::from(r.ma.unsigned_abs()), r.ma < 0),
        _ => (u64::from(r.magnitude_mw()), r.ma < 0),
    });
    let max = view.peaks.map(|(v, a, w)| match view.page {
        3 => (u64::from(v), false),
        4 => (u64::from(a), false),
        _ => (u64::from(w), false),
    });
    card(p, 8, 35, 109, 51, b"NOW", color);
    card(p, 123, 35, 109, 51, b"MAX", PURPLE);
    value(p, 14, 55, 97, live, unit, color, 3);
    value(p, 129, 55, 97, max, unit, color, 3);
    p.text(8, 95, title, 2, color);
    p.text(
        170,
        95,
        match view.graph_window {
            1 => b"2 MIN ",
            2 => b"5 MIN ",
            3 => b"10 MIN",
            _ => b"1 MIN ",
        },
        2,
        color,
    );
    p.outline(8, 112, 224, 100, color);
    if p.top >= 104 && p.top < 216 {
        let mut digits = [0; 11];
        let label = milli(u64::from(view.graph_max), false, &mut digits);
        let mut end = label.len();
        while end > 0 && label[end - 1] == b'0' {
            end -= 1;
        }
        if end > 0 && label[end - 1] == b'.' {
            end -= 1;
        }
        p.text(10, 114, &label[..end], 2, MUTED);
        p.text(10 + end * 10, 114, unit, 2, MUTED);
        p.text(10, 193, b"0", 2, MUTED);
        for y in [118, 160, 203] {
            p.rect(65, y, 166, 1, GRID);
        }

        let mut previous: Option<(usize, usize)> = None;
        for (index, height) in view.graph_heights.iter().enumerate() {
            if *height == 255 {
                previous = None;
                continue;
            }
            if view.graph_breaks[index] {
                previous = None;
            }
            let x = 65 + index * 165 / 59;
            let y = 203 - usize::from(*height);
            if let Some((px, py)) = previous {
                let dx = x - px;
                let mut last_y = py;
                for step in 1..=dx {
                    let delta = (y as i32 - py as i32) * step as i32;
                    let delta = match dx {
                        2 => delta / 2,
                        3 => delta / 3,
                        _ => delta,
                    };
                    let current_y = (py as i32 + delta) as usize;
                    p.rect(
                        px + step,
                        last_y.min(current_y),
                        1,
                        last_y.abs_diff(current_y) + 1,
                        color,
                    );
                    last_y = current_y;
                }
            } else {
                p.rect(x, y, 1, 1, color);
            }
            previous = if view.graph_breaks[index] {
                None
            } else {
                Some((x, y))
            };
        }
    }
}

pub fn render_demo_strip(view: &Overview, top: usize, pixels: &mut [u16; SIDE * STRIP_ROWS]) {
    assert!(top < SIDE && top % STRIP_ROWS == 0);
    pixels.fill(BG);
    let mut p = Strip { pixels, top };
    #[cfg(feature = "pd-rd-test")]
    if let Some(test) = view.rd_test {
        rd_test::render(&mut p, test);
        return;
    }
    header(&mut p, view);
    match view.page {
        0 => overview(&mut p, view),
        1 => energy(&mut p, view),
        PROFILES_PAGE => {
            if let Some(pd) = &view.pd_profiles {
                profiles(&mut p, pd);
            }
        }
        EPR_PAGE => {
            if let Some(pd) = &view.pd_profiles {
                epr_profiles(&mut p, pd);
            }
        }
        SNIFF_PAGE => {
            if let Some(pd) = &view.pd_profiles {
                pd_sniff(&mut p, pd);
            }
        }
        CABLE_PAGE => {
            if let Some(pd) = &view.pd_profiles {
                cable::render(&mut p, pd);
            }
        }
        #[cfg(feature = "pd-charger")]
        CHARGER_PAGE => charger::render(&mut p, view),
        #[cfg(feature = "system")]
        SYSTEM_PAGE => system::render(&mut p, view),
        _ => graph(&mut p, view),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn graph_scale_rounds_up_and_preserves_gaps() {
        assert_eq!(graph_scale(0), 1000);
        assert_eq!(graph_scale(2000), 2000);
        assert_eq!(graph_scale(2001), 2500);
        assert_eq!(graph_scale(237_934), 250_000);
        assert_eq!(graph_scale(1_731_123), 2_000_000);
        let mut view = Overview::default();
        view.set_page(2);
        view.graph_values[0] = Some(2000);
        view.graph_values[2] = Some(2000);
        view.update_graph();
        let mut pixels = [0; SIDE * STRIP_ROWS];
        render_demo_strip(&view, 112, &mut pixels);
        let x0 = 65;
        let x2 = 65 + 2 * 165 / 59;
        assert_eq!(pixels[(118 - 112) * SIDE + x0], WATTS);
        assert_ne!(pixels[(118 - 112) * SIDE + x0 + 1], WATTS);
        assert_eq!(pixels[(118 - 112) * SIDE + x2], WATTS);
        assert_ne!(pixels[(119 - 112) * SIDE + x0], WATTS);
    }

    #[test]
    fn low_power_does_not_fill_the_graph() {
        let mut view = Overview::default();
        view.set_page(2);
        view.graph_values[59] = Some(5);
        view.update_graph();
        let mut pixels = [0; SIDE * STRIP_ROWS];
        render_demo_strip(&view, 200, &mut pixels);
        let x = 65 + 59 * 165 / 59;
        assert_eq!(pixels[(203 - 200) * SIDE + x], WATTS);
        assert_ne!(pixels[(202 - 200) * SIDE + x], WATTS);
    }

    #[test]
    fn partly_missing_bins_do_not_join_neighbouring_points() {
        let mut view = Overview::default();
        view.set_page(2);
        view.graph_values[0..3].fill(Some(2000));
        view.graph_breaks[1] = true;
        view.update_graph();
        let mut pixels = [0; SIDE * STRIP_ROWS];
        render_demo_strip(&view, 112, &mut pixels);
        let row = (118 - 112) * SIDE;
        let x0 = 65;
        let x1 = 65 + 165 / 59;
        let x2 = 65 + 2 * 165 / 59;
        assert_eq!(pixels[row + x0], WATTS);
        assert_eq!(pixels[row + x1], WATTS);
        assert_eq!(pixels[row + x2], WATTS);
        assert_ne!(pixels[row + x0 + 1], WATTS);
        assert_ne!(pixels[row + x1 + 1], WATTS);
    }

    #[test]
    fn values_fit_their_cards_at_measurement_extremes() {
        for (number, negative, width, preferred) in [
            (52_830, false, 97, 3),
            (32_768, true, 97, 3),
            (1_731_133, true, 122, 4),
            (1_731_133, false, 72, 2),
            (999_999_999, false, 210, 3),
        ] {
            let mut any = false;
            for top in (0..32).step_by(STRIP_ROWS) {
                let mut pixels = [0; SIDE * STRIP_ROWS];
                let mut p = Strip {
                    pixels: &mut pixels,
                    top,
                };
                value(
                    &mut p,
                    10,
                    0,
                    width,
                    Some((number, negative)),
                    b"W",
                    WATTS,
                    preferred,
                );
                for row in 0..STRIP_ROWS {
                    assert!(
                        pixels[row * SIDE..row * SIDE + 10]
                            .iter()
                            .all(|&px| px == 0)
                    );
                    assert!(
                        pixels[row * SIDE + 10 + width..(row + 1) * SIDE]
                            .iter()
                            .all(|&px| px == 0)
                    );
                    any |= pixels[row * SIDE + 10..row * SIDE + 10 + width].contains(&WATTS);
                }
            }
            assert!(any);
        }
    }

    #[test]
    fn time_labels_show_hours_minutes_and_seconds() {
        assert_eq!(&time_text(0), b"00:00:00");
        assert_eq!(&time_text(3661), b"01:01:01");
        assert_eq!(&time_text(359_999), b"99:59:59");
    }
}
