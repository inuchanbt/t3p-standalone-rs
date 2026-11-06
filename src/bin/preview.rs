// SPDX-License-Identifier: GPL-2.0-or-later
use std::{
    env,
    fs::File,
    io::{self, BufWriter, Write},
};
use t3p_standalone::{Meter, Overview, Reading, SIDE, STRIP_ROWS, demo_reading, render_demo_strip};

fn main() -> io::Result<()> {
    let mut args = env::args_os().skip(1);
    let path = args.next().unwrap_or_else(|| "t3p-preview.ppm".into());
    let flags: Vec<_> = args.collect();
    let measurement = flags.iter().any(|x| x == "--measurement");
    let missing = flags.iter().any(|x| x == "--missing");
    let pd = flags.iter().any(|x| x == "--pd");
    let energy = flags.iter().any(|x| x == "--energy");
    let power = flags.iter().any(|x| x == "--power");
    let voltage = flags.iter().any(|x| x == "--voltage");
    let current = flags.iter().any(|x| x == "--current");
    let extreme = flags.iter().any(|x| x == "--extreme");
    let mut output = BufWriter::new(File::create(path)?);
    let mut meter = Meter::new(0);
    if measurement {
        for tick in (0..=1_000).step_by(100) {
            meter.sample(
                tick,
                Some(if extreme {
                    Reading {
                        mv: 52_830,
                        ma: i16::MIN,
                    }
                } else {
                    Reading {
                        mv: 5000,
                        ma: -2000,
                    }
                }),
            );
        }
        if missing {
            meter.sample(1100, None);
        }
    } else {
        for tick in (0..=62_000).step_by(100) {
            meter.sample(tick, demo_reading(tick));
        }
    }
    let mut view = Overview::default();
    view.set_page(if energy {
        1
    } else if power {
        2
    } else if voltage {
        3
    } else if current {
        4
    } else {
        0
    });
    meter.overview(&mut view);
    if measurement {
        view.set_uncalibrated();
    }
    if pd {
        view.set_pd_diagnostic(Some((0x04030291, 0x05)));
    }
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
