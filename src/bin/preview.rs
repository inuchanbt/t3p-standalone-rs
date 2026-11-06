// SPDX-License-Identifier: GPL-2.0-or-later
use std::{
    env,
    fs::File,
    io::{self, BufWriter, Write},
};
use t3p_standalone::{Meter, Overview, SIDE, STRIP_ROWS, demo_reading, render_demo_strip};

fn main() -> io::Result<()> {
    let path = env::args_os()
        .nth(1)
        .unwrap_or_else(|| "t3p-preview.ppm".into());
    let mut output = BufWriter::new(File::create(path)?);
    let mut meter = Meter::new(0);
    for tick in (0..=62_000).step_by(100) {
        meter.sample(tick, demo_reading(tick));
    }
    let mut view = Overview::default();
    meter.overview(&mut view);
    let mut pixels = [0; SIDE * STRIP_ROWS];
    write!(output, "P6\n{SIDE} {SIDE}\n255\n")?;
    for top in (0..SIDE).step_by(STRIP_ROWS) {
        render_demo_strip(&view, top, &mut pixels);
        for pixel in pixels {
            let r = ((pixel >> 11) & 31) * 255 / 31;
            let g = ((pixel >> 5) & 63) * 255 / 63;
            let b = (pixel & 31) * 255 / 31;
            output.write_all(&[r as u8, g as u8, b as u8])?;
        }
    }
    output.flush()
}
