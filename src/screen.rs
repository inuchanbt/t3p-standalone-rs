// SPDX-License-Identifier: GPL-2.0-or-later
use crate::{Overview, SIDE, STRIP_ROWS};

const BG: u16 = 0x0863;
const CARD: u16 = 0x1106;
const GRID: u16 = 0x29eb;
const TEXT: u16 = 0xffff;
const MUTED: u16 = 0x9d5b;
const VOLTS: u16 = 0x5eff;
const AMPS: u16 = 0x7f75;
const WATTS: u16 = 0xfde8;

struct Strip<'a> {
    pixels: &'a mut [u16; SIDE * STRIP_ROWS],
    top: usize,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn low_power_noise_does_not_fill_graph() {
        let mut view = Overview::default();
        view.power_mw[59] = Some(5);
        let mut pixels = [0; SIDE * STRIP_ROWS];
        render_demo_strip(&view, 224, &mut pixels);
        let x = 13 + 59 * 214 / 60;
        assert_eq!(pixels[(228 - 224) * SIDE + x], WATTS);
        assert_ne!(pixels[(227 - 224) * SIDE + x], WATTS);
    }
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

    fn text(&mut self, x: usize, y: usize, value: &[u8], scale: usize, color: u16) {
        if y >= self.top + STRIP_ROWS || y + 5 * scale <= self.top {
            return;
        }
        for (index, &ch) in value.iter().enumerate() {
            let rows = glyph(ch);
            for row in 0..5 {
                for col in 0..3 {
                    if rows & (1 << (4 * (4 - row) + 2 - col)) != 0 {
                        self.rect(
                            x + (index * 4 + col) * scale,
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

fn glyph(ch: u8) -> u32 {
    match ch {
        b'0' => 0x75557,
        b'1' => 0x26227,
        b'2' => 0x71747,
        b'3' => 0x71717,
        b'4' => 0x55711,
        b'5' => 0x74717,
        b'6' => 0x74757,
        b'7' => 0x71111,
        b'8' => 0x75757,
        b'9' => 0x75717,
        b'A' => 0x25755,
        b'B' => 0x65656,
        b'C' => 0x74447,
        b'D' => 0x65556,
        b'E' => 0x74647,
        b'F' => 0x74644,
        b'G' => 0x74557,
        b'H' => 0x55755,
        b'I' => 0x72227,
        b'L' => 0x44447,
        b'M' => 0x57755,
        b'N' => 0x57775,
        b'O' => 0x75557,
        b'P' => 0x75744,
        b'R' => 0x75765,
        b'S' => 0x74717,
        b'T' => 0x72222,
        b'U' => 0x55557,
        b'V' => 0x55552,
        b'W' => 0x55775,
        b'X' => 0x55255,
        b'Y' => 0x55222,
        b'-' => 0x00700,
        b'.' => 0x00002,
        b'+' => 0x02720,
        b':' => 0x02020,
        _ => 0,
    }
}

// Fixed decimal output in a small stack buffer. Width is bounded by the
// available columns even if the meter has been running for years.
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

pub fn render_demo_strip(view: &Overview, top: usize, pixels: &mut [u16; SIDE * STRIP_ROWS]) {
    assert!(top < SIDE && top % STRIP_ROWS == 0);
    pixels.fill(BG);
    let mut p = Strip { pixels, top };
    p.text(
        12,
        12,
        if view.show_peaks {
            b"T3P MAX  "
        } else {
            b"T3P METER"
        },
        3,
        TEXT,
    );
    p.rect(172, 9, 56, 22, WATTS);
    p.text(
        179,
        15,
        if view.uncalibrated {
            b"UNCAL"
        } else {
            b"DEMO "
        },
        2,
        BG,
    );

    p.rect(12, 39, 216, 87, CARD);
    let mut digits = [0; 11];
    for (index, (label, color)) in [(b'V', VOLTS), (b'A', AMPS), (b'W', WATTS)]
        .into_iter()
        .enumerate()
    {
        let y = 45 + index * 27;
        p.text(21, y + 2, &[label], 3, color);
        let display = if view.show_peaks {
            view.peaks.map(|(v, a, w)| {
                (
                    match index {
                        0 => u64::from(v),
                        1 => u64::from(a),
                        _ => u64::from(w),
                    },
                    false,
                )
            })
        } else {
            view.reading.map(|reading| {
                let value = match index {
                    0 => u64::from(reading.mv),
                    1 => u64::from(reading.ma.unsigned_abs()),
                    _ => u64::from(reading.magnitude_mw()),
                };
                (value, index > 0 && reading.ma < 0)
            })
        };
        if let Some((value, negative)) = display {
            p.text(54, y, milli(value, negative, &mut digits), 4, color);
        } else {
            p.text(54, y, b"---", 4, MUTED);
        }
    }

    p.rect(12, 133, 105, 33, CARD);
    p.rect(123, 133, 105, 33, CARD);
    p.text(19, 138, b"AH", 2, MUTED);
    p.text(130, 138, b"WH", 2, MUTED);
    p.text(19, 151, milli(view.mah, false, &mut digits), 2, TEXT);
    p.text(130, 151, milli(view.mwh, false, &mut digits), 2, TEXT);

    if let Some(registers) = view.pd_diagnostic {
        p.text(12, 176, b"PD", 2, MUTED);
        if let Some((first, last)) = registers {
            let digits = b"0123456789ABCDEF";
            for index in 0..5 {
                let byte = if index == 4 {
                    last
                } else {
                    (first >> (index * 8)) as u8
                };
                p.text(
                    36 + index * 24,
                    176,
                    &digits[(byte >> 4) as usize..][..1],
                    2,
                    MUTED,
                );
                p.text(
                    44 + index * 24,
                    176,
                    &digits[(byte & 15) as usize..][..1],
                    2,
                    MUTED,
                );
            }
        } else {
            p.text(36, 176, b"-- -- -- -- --", 2, MUTED);
        }
    } else {
        p.text(12, 176, b"POWER 60S", 2, MUTED);
        let seconds = view.elapsed_seconds;
        let parts = [seconds / 3600, seconds / 60 % 60, seconds % 60];
        let digits = b"0123456789";
        p.text(136, 176, b"T", 2, MUTED);
        for (index, part) in parts.into_iter().enumerate() {
            let x = 152 + index * 24;
            p.text(x, 176, &digits[(part / 10) as usize..][..1], 2, MUTED);
            p.text(x + 8, 176, &digits[(part % 10) as usize..][..1], 2, MUTED);
            if index < 2 {
                p.text(x + 16, 176, b":", 2, MUTED);
            }
        }
    }
    p.rect(12, 191, 216, 39, CARD);
    if top >= 184 {
        for y in [191, 209, 229] {
            p.rect(12, y, 216, 1, GRID);
        }
        let max = view
            .power_mw
            .iter()
            .copied()
            .flatten()
            .max()
            // Keep milliwatt-level zero noise from filling the entire graph.
            .unwrap_or(1000)
            .max(1000);
        for (index, sample) in view.power_mw.iter().enumerate() {
            if let Some(mw) = sample {
                let height = (*mw * 37 / max).max(1) as usize;
                p.rect(13 + index * 214 / 60, 229 - height, 2, height, WATTS);
            }
        }
    }
}
