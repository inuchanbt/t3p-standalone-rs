// SPDX-License-Identifier: GPL-2.0-or-later
use super::{AMPS, GRID, MUTED, Strip, TEXT, VOLTS};
use crate::{SIDE, STRIP_ROWS};
pub fn render(
    result: i32,
    status: u32,
    jedec: u32,
    sfdp: u32,
    top: usize,
    pixels: &mut [u16; SIDE * STRIP_ROWS],
) {
    pixels.fill(0);
    let p = &mut Strip { pixels, top };
    p.text(8, 12, b"SCREEN CAPTURE", 2, AMPS);
    p.rect(8, 34, 224, 1, GRID);
    let message: &[u8] = match result {
        1..=9999 => b"SAVED TO FLASH",
        -2 => b"STORAGE FULL",
        -3 => b"BUSY - TRY AGAIN",
        0 => b"SAVING - PLEASE WAIT",
        _ => b"SAVE FAILED / LOCKED",
    };
    p.text(8, 55, message, 1, if result > 0 { AMPS } else { VOLTS });
    if result > 0 {
        let mut name = *b"SCRN0000.BMP";
        let mut n = result as u32;
        for i in (4..8).rev() {
            name[i] += (n % 10) as u8;
            n /= 10;
        }
        p.text(8, 78, &name, 2, TEXT);
    }
    for (y, label, value) in [
        (115, b"STORE" as &[u8], status),
        (135, b"JEDEC", jedec),
        (155, b"SFDP", sfdp),
    ] {
        let mut hex = [b'0'; 8];
        for (i, b) in hex.iter_mut().enumerate() {
            *b = b"0123456789ABCDEF"[((value >> (28 - i * 4)) & 15) as usize];
        }
        p.text(8, y, label, 1, MUTED);
        p.text(
            90,
            y,
            if y != 115 && value == 0 { b"N/A" } else { &hex },
            1,
            TEXT,
        );
    }
    p.text(8, 187, b"PC: CAPTURE_EXPORT.PY", 1, AMPS);
    p.text(8, 206, b"HOLD 3S SAVE / HOLD 1S ON RELEASE", 1, MUTED);
    p.text(8, 222, b"6 SLOTS / USB READ ONLY", 1, MUTED);
}
