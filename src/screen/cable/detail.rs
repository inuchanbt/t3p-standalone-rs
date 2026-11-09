// SPDX-License-Identifier: GPL-2.0-or-later
use super::{AMPS, CableIdentity, MUTED, PANEL, Strip, TEXT, codes, hex, hex_digit, row};
use crate::pd::cable::{
    CableDiscovery, CableSection, ExtendedReply, QueryResult, SVIDS_PER_SCREEN, VdmReply,
};

pub(super) fn title(page: CableSection) -> &'static [u8] {
    match page {
        CableSection::IdFields => b"ID DETAILS",
        CableSection::CableFields => b"CABLE FIELDS",
        CableSection::ActiveUsb => b"ACTIVE USB",
        CableSection::SvidList(_) => b"SVID LIST",
        CableSection::SvidRaw => b"LAST SVID RAW",
        CableSection::Modes(_) => b"SVID MODES",
        CableSection::Manufacturer => b"MANUFACTURER",
        CableSection::Status => b"CABLE STATUS",
    }
}

pub(super) fn result(value: QueryResult) -> &'static [u8] {
    match value {
        QueryResult::Ignore => b"NOT QUERIED",
        QueryResult::Ack => b"ACK",
        QueryResult::More => b"INCOMPLETE",
        QueryResult::Nak => b"NAK",
        QueryResult::Busy => b"BUSY",
        QueryResult::Unsupported => b"NOT SUPPORTED",
        QueryResult::Reject => b"REJECTED",
        QueryResult::Invalid => b"BAD REPLY",
        QueryResult::NoCrc => b"NO GOODCRC",
        QueryResult::NoReply => b"NO REPLY",
        QueryResult::Limit => b"LIST LIMIT",
    }
}

pub(super) fn render(p: &mut Strip<'_>, d: &CableDiscovery, id: CableIdentity, page: CableSection) {
    match page {
        CableSection::IdFields => id_fields(p, id),
        CableSection::CableFields => cable_fields(p, id),
        CableSection::ActiveUsb => active_usb(p, id),
        CableSection::SvidList(index) => svid_list(p, d, id, index),
        CableSection::SvidRaw => vdm_raw(p, &d.svid_reply),
        CableSection::Modes(index) => {
            p.text(154, 48, &hex::<4>(u32::from(d.svids()[index])), 1, AMPS);
            vdm_raw(p, d.modes(index));
        }
        CableSection::Manufacturer => manufacturer(p, id, &d.manufacturer),
        CableSection::Status => status(p, d, id),
    }
}

pub(super) fn summary(p: &mut Strip<'_>, d: &CableDiscovery) {
    p.text(12, 178, b"SVID", 1, MUTED);
    if d.svids().is_empty() {
        let label = if d.svid_reply.result == QueryResult::Ack {
            b"NONE"
        } else {
            result(d.svid_reply.result)
        };
        p.text(81, 178, label, 1, TEXT);
    } else {
        for (i, &svid) in d.svids().iter().take(4).enumerate() {
            let raw = hex::<4>(u32::from(svid));
            let name = svid_name(svid);
            p.text(
                81 + i * 26,
                178,
                if name.is_empty() { &raw } else { name },
                1,
                TEXT,
            );
        }
        if d.svids().len() > 4 {
            p.text(187, 178, b"+", 1, AMPS);
        }
    }
    p.text(12, 192, b"MAKER", 1, MUTED);
    let name = d.manufacturer.maker();
    if d.manufacturer.result == QueryResult::Ack && !name.is_empty() {
        maker_text(p, 81, 192, name);
    } else {
        p.text(
            81,
            192,
            if name.is_empty() && d.manufacturer.result == QueryResult::Ack {
                b"NO NAME"
            } else {
                result(d.manufacturer.result)
            },
            1,
            TEXT,
        );
    }
}

fn id_fields(p: &mut Strip<'_>, id: CableIdentity) {
    let idh = id.words()[1];
    let vdo = id.vdo();
    let layout: &[u8] = if vdo.is_none() {
        b"RAW ONLY"
    } else if id.pd2() {
        b"PD 2.0"
    } else {
        b"PD 3.X"
    };
    row(p, 53, b"LAYOUT", layout, AMPS);
    row(p, 71, b"PRODUCT TYPE", &[hex_digit(idh >> 27 & 7)], TEXT);
    row(p, 89, b"ID CONNECTOR", &[hex_digit(idh >> 21 & 3)], TEXT);
    row(
        p,
        107,
        b"CURRENT GUESS",
        yes_no(id.pd2() && vdo.is_some_and(|vdo| vdo.current == 0)),
        TEXT,
    );
    row(p, 125, b"VID", &hex::<4>(idh), TEXT);
    row(p, 143, b"PID", &hex::<4>(id.words()[3] >> 16), TEXT);
    row(p, 161, b"CERT ID / XID", &hex::<8>(id.words()[2]), TEXT);
}

fn yes_no(value: bool) -> &'static [u8] {
    if value { b"YES" } else { b"NO" }
}

fn cable_fields(p: &mut Strip<'_>, id: CableIdentity) {
    let vdo = id.words()[4];
    row(p, 53, b"VBUS THROUGH", yes_no(vdo & (1 << 4) != 0), TEXT);
    row(
        p,
        71,
        b"SOP2 PRESENT",
        yes_no(id.active() && vdo & (1 << 3) != 0),
        TEXT,
    );
    if id.pd2() {
        row(p, 89, b"PLUG B17 RAW", &[hex_digit(vdo >> 17 & 1)], TEXT);
        row(p, 107, b"SOP2 B3 RAW", &[hex_digit(vdo >> 3 & 1)], TEXT);
        let dir = |bit: u32| hex_digit(vdo >> bit & 1);
        row(
            p,
            125,
            b"SSTX1 / SSTX2",
            &[dir(10), b' ', b'/', b' ', dir(9)],
            TEXT,
        );
        row(
            p,
            143,
            b"SSRX1 / SSRX2",
            &[dir(8), b' ', b'/', b' ', dir(7)],
            TEXT,
        );
        return;
    }
    let features = id.words()[5];
    let sbu: &[u8] = match (vdo >> 7) & 3 {
        0 => b"PASSIVE",
        1 => b"ACTIVE",
        _ => b"UNSUPPORTED",
    };
    row(p, 89, b"SBU", sbu, TEXT);
    row(
        p,
        107,
        b"BUILD",
        if features & 0x400 != 0 {
            b"OPTICAL"
        } else {
            b"COPPER"
        },
        TEXT,
    );
    row(
        p,
        125,
        b"SIGNAL",
        if features & 0x200 != 0 {
            b"RETIMER"
        } else {
            b"REDRIVER"
        },
        TEXT,
    );
    row(p, 143, b"MAX TEMP", &degrees((features >> 24) as u8), TEXT);
    row(
        p,
        161,
        b"SHUTDOWN TEMP",
        &degrees((features >> 16) as u8),
        TEXT,
    );
    row(
        p,
        179,
        b"U3 POWER/U3S",
        &[
            hex_digit(features >> 12 & 7),
            b' ',
            b'/',
            b' ',
            hex_digit(features >> 11 & 1),
        ],
        TEXT,
    );
}

fn active_usb(p: &mut Strip<'_>, id: CableIdentity) {
    let word = id.words()[5];
    let bit = |shift| hex_digit(word >> shift & 1u32);
    row(p, 53, b"USB2/3/4 OFF", &codes(bit(5), bit(4), bit(8)), TEXT);
    row(p, 71, b"USB2 HUB HOPS", &[hex_digit(word >> 6 & 3)], TEXT);
    row(p, 89, b"USB LANES", &[b'1' + (word >> 3 & 1) as u8], TEXT);
    row(p, 107, b"ISOLATED", yes_no(word & 4 != 0), TEXT);
    row(p, 125, b"ASYMMETRIC", yes_no(word & 2 != 0), TEXT);
    row(p, 143, b"GEN2 OR MORE", yes_no(word & 1 != 0), TEXT);
}

fn svid_name(svid: u16) -> &'static [u8] {
    match svid {
        0x8087 => b"TBT",
        0xff01 => b"DP",
        _ => b"",
    }
}

fn svid_list(p: &mut Strip<'_>, d: &CableDiscovery, id: CableIdentity, page: usize) {
    let status = if id.modal() {
        result(d.svid_reply.result)
    } else {
        b"MODAL BIT CLEAR"
    };
    p.text(8, 48, status, 1, TEXT);
    let start = page * SVIDS_PER_SCREEN;
    for (i, &svid) in d.svids()[start..].iter().take(SVIDS_PER_SCREEN).enumerate() {
        let y = 60 + 18 * i;
        row(p, y, b"SVID", &hex::<4>(u32::from(svid)), AMPS);
        p.text(146, y + 5, svid_name(svid), 1, TEXT);
    }
    if d.svids().is_empty() {
        row(p, 60, b"SVID LIST", b"EMPTY", MUTED);
    }
}

fn vdm_raw(p: &mut Strip<'_>, reply: &VdmReply) {
    p.text(8, 48, result(reply.result), 1, TEXT);
    if reply.words().is_empty() {
        return;
    }
    row(
        p,
        60,
        b"PD HEADER",
        &hex::<4>(u32::from(reply.header)),
        AMPS,
    );
    for (i, &word) in reply.words().iter().enumerate() {
        let label = [b'V', b'D', b'O', b' ', b'0' + i as u8];
        row(
            p,
            78 + i * 18,
            if i == 0 { b"VDM HEADER" } else { &label },
            &hex::<8>(word),
            TEXT,
        );
    }
}

fn maker_text(p: &mut Strip<'_>, x: usize, y: usize, name: &[u8]) {
    let mut text = [0; 21];
    let len = name.len().min(text.len());
    for (dst, &src) in text.iter_mut().zip(&name[..len]) {
        *dst = src.to_ascii_uppercase();
    }
    p.text(x, y, &text[..len], 1, TEXT);
}

fn manufacturer(p: &mut Strip<'_>, id: CableIdentity, reply: &ExtendedReply) {
    let outcome = if id.pd2() {
        b"PD2 N/A"
    } else {
        result(reply.result)
    };
    row(p, 53, b"RESULT", outcome, AMPS);
    if reply.header != 0 {
        row(
            p,
            71,
            b"PD HEADER",
            &hex::<4>(u32::from(reply.header)),
            TEXT,
        );
        row(
            p,
            89,
            b"EXT HEADER",
            &hex::<4>(u32::from(reply.ext_header)),
            TEXT,
        );
    }
    let &[v0, v1, p0, p1, ..] = reply.bytes() else {
        return;
    };
    let vid = hex::<4>(u32::from(u16::from_le_bytes([v0, v1])));
    let pid = hex::<4>(u32::from(u16::from_le_bytes([p0, p1])));
    let ids = [
        vid[0], vid[1], vid[2], vid[3], b'/', pid[0], pid[1], pid[2], pid[3],
    ];
    row(p, 107, b"VID/PID", &ids, TEXT);
    p.rect(8, 125, 224, 17, PANEL);
    p.text(12, 130, b"MAKER", 1, MUTED);
    maker_text(p, 81, 130, reply.maker());
    for (i, bytes) in reply.bytes().chunks(9).enumerate() {
        let y = 143 + 18 * i;
        let mut digits = [0; 18];
        for (pair, &byte) in digits.chunks_exact_mut(2).zip(bytes) {
            pair.copy_from_slice(&hex::<2>(u32::from(byte)));
        }
        p.rect(8, y, 224, 17, PANEL);
        p.text(
            12,
            y + 5,
            &[b'R', b'A', b'W', b' ', b'1' + i as u8],
            1,
            MUTED,
        );
        p.text(81, y + 5, &digits[..bytes.len() * 2], 1, TEXT);
    }
}

fn degrees(value: u8) -> [u8; 4] {
    [
        if value >= 100 {
            b'0' + value / 100
        } else {
            b' '
        },
        if value >= 10 {
            b'0' + value / 10 % 10
        } else {
            b' '
        },
        b'0' + value % 10,
        b'C',
    ]
}

fn status(p: &mut Strip<'_>, d: &CableDiscovery, id: CableIdentity) {
    let sop1 = if !id.active() {
        b"PASSIVE"
    } else if id.pd2() {
        b"PD2 N/A"
    } else {
        result(d.status.result)
    };
    status_rows(p, 53, b"SOP1 STATUS", sop1, &d.status);
    let sop2 = match id.vdo() {
        _ if id.pd2() => b"PD2 N/A" as &[u8],
        _ if !id.active() => b"NOT PRESENT",
        None => b"UNKNOWN LAYOUT",
        Some(vdo) if !vdo.sop2_present => b"NOT PRESENT",
        Some(_) => result(d.sop2_sync),
    };
    row(p, 116, b"SOP2 SYNC", sop2, AMPS);
    status_rows(
        p,
        134,
        b"SOP2 STATUS",
        result(d.sop2_status.result),
        &d.sop2_status,
    );
    super::sniff_count(p, 116, 207, b"SOP2 RX:", u16::from(d.sop2_rx));
}

fn status_rows(p: &mut Strip<'_>, y: usize, label: &[u8], outcome: &[u8], reply: &ExtendedReply) {
    row(p, y, label, outcome, AMPS);
    if let &[temp, flags] = reply.bytes() {
        let value = degrees(temp);
        row(
            p,
            y + 18,
            b"TEMPERATURE",
            match temp {
                0 => b"N/A",
                1 => b"BELOW 2C",
                _ => &value,
            },
            TEXT,
        );
        row(p, y + 36, b"SHUTDOWN", yes_no(flags & 1 != 0), TEXT);
        p.text(158, y + 41, b"RAW", 1, MUTED);
        p.text(
            182,
            y + 41,
            &hex::<4>(u32::from(u16::from_be_bytes([temp, flags]))),
            1,
            TEXT,
        );
    }
    if reply.header != 0 {
        p.text(175, y + 5, b"HDR", 1, MUTED);
        p.text(199, y + 5, &hex::<4>(u32::from(reply.header)), 1, TEXT);
    }
}
