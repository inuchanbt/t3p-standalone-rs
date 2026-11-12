// SPDX-License-Identifier: GPL-2.0-or-later
use super::cable::hex;
use super::{AMPS, BG, GRID, MUTED, Strip, TEXT, VOLTS};
use crate::{SIDE, STRIP_ROWS};

pub fn render(words: &[u32; 36], section: u32, top: usize, pixels: &mut [u16; SIDE * STRIP_ROWS]) {
    pixels.fill(BG);
    if section >= 4 || top >= SIDE {
        return;
    }
    let section = section as usize;
    let mut p = Strip { pixels, top };
    p.text(8, 8, b"CAPTURE PREFLIGHT", 2, AMPS);
    p.text(8, 27, b"V1  READ ONLY  NOT A CAPTURE", 1, VOLTS);
    p.rect(8, 40, 224, 1, GRID);
    let labels: [[&[u8]; 9]; 4] = [
        [
            b"ENTRY R0",
            b"ENTRY R1",
            b"ENTRY R2",
            b"ENTRY IRQ",
            b"BOOT FLAGS",
            b"CONTEXT 0",
            b"CONTEXT 1",
            b"CONTEXT 2",
            b"CONTEXT 3",
        ],
        [
            b"CONTEXT 4",
            b"CONTEXT 5",
            b"AUX 0",
            b"AUX 1",
            b"AUX 2",
            b"AUX 3",
            b"AUX 4",
            b"AUX 5",
            b"AUX 6",
        ],
        [
            b"AUX 7",
            b"SFC CON0",
            b"SFC CON1",
            b"SFC OFFSET",
            b"NDEC START",
            b"NDEC END",
            b"SFC CLOCK",
            b"SFC DTR",
            b"SAVE BACKEND",
        ],
        [
            b"HEADER 0",
            b"HEADER 1",
            b"HEADER 2",
            b"HEADER 3",
            b"HEADER 4",
            b"HEADER 5",
            b"HEADER 6",
            b"HEADER 7",
            b"HEADER COPY",
        ],
    ];
    for (row, label) in labels[section].iter().enumerate() {
        let i = section * 9 + row;
        let y = 49 + row * 18;
        p.text(12, y, label, 1, MUTED);
        if i == 26 {
            p.text(132, y, b"LOCKED", 1, VOLTS);
        } else if i == 35 {
            p.text(
                132,
                y,
                if words[4] & 4 != 0 { b"OK" } else { b"N/A" },
                1,
                TEXT,
            );
        } else if (5..=10).contains(&i) && words[4] & 1 == 0
            || (11..=18).contains(&i) && words[4] & 2 == 0
            || (27..=34).contains(&i) && words[4] & 4 == 0
        {
            p.text(132, y, b"N/A", 1, MUTED);
        } else {
            p.text(132, y, &hex::<8>(words[i]), 1, TEXT);
        }
        p.rect(8, y + 12, 224, 1, GRID);
    }
    p.text(8, 219, b"TAP NEXT  HOLD METER", 1, AMPS);
    p.text(213, 219, &[b'1' + section as u8, b'/', b'4'], 1, AMPS);
    p.text(8, 231, b"HEX VALUES - NO FLASH WRITE", 1, MUTED);
}
