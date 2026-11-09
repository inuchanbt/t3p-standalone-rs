// SPDX-License-Identifier: GPL-2.0-or-later
use std::{
    env,
    fs::File,
    io::{self, BufWriter, Write},
};
use t3p_standalone::{
    CableCapture, EprCapture, EprEvent, Meter, Overview, PdSniff, Reading, SIDE, STRIP_ROWS,
    SourceCaps, demo_reading, render_demo_strip,
};

fn main() -> io::Result<()> {
    let mut args = env::args_os().skip(1);
    let path = args.next().unwrap_or_else(|| "t3p-preview.ppm".into());
    let flags: Vec<_> = args.collect();
    let measurement = flags.iter().any(|x| x == "--measurement");
    let missing = flags.iter().any(|x| x == "--missing");
    let pd = flags.iter().any(|x| x == "--pd");
    let profiles_ui = flags.iter().any(|x| x == "--profiles-ui");
    let profiles_page = flags.iter().any(|x| x == "--profiles-page");
    let profiles_listening = flags.iter().any(|x| x == "--profiles-listening");
    let profiles_offer = flags.iter().any(|x| x == "--profiles-offer");
    let epr_page = flags.iter().any(|x| x == "--epr-page");
    let epr_timeout = flags.iter().any(|x| x == "--epr-timeout");
    let epr_offer = flags.iter().any(|x| x == "--epr-offer");
    let epr_partial = flags.iter().any(|x| x == "--epr-partial");
    let epr_diag = flags.iter().any(|x| x == "--epr-diag");
    let sniff_page = flags.iter().any(|x| x == "--sniff-page");
    let sniff_log = flags.iter().any(|x| x == "--sniff-log");
    let sniff_old = flags.iter().any(|x| x == "--sniff-old");
    let sniff_alert = flags.iter().any(|x| x == "--sniff-alert");
    let cable_view = flags.iter().find_map(|flag| {
        Some(match flag.to_str()? {
            "--cable-page" => (0, 0),
            "--cable-listening" => (1, 0),
            "--cable-summary" => (2, 0),
            "--cable-raw" => (2, 1),
            "--cable-flags" => (2, 2),
            "--cable-timeout" => (3, 0),
            "--cable-vbus" => (11, 0),
            "--cable-no-ra" => (13, 0),
            _ => return None,
        })
    });
    #[cfg(feature = "pd-cable-read")]
    let cable_scan = flags.iter().find_map(|flag| {
        flag.to_str()?
            .strip_prefix("--cable-scan=")?
            .parse::<u8>()
            .ok()
    });
    #[cfg(feature = "pd-cable-read")]
    let cable_view = cable_scan.map(|section| (2, section)).or(cable_view);
    let energy = flags.iter().any(|x| x == "--energy");
    let power = flags.iter().any(|x| x == "--power");
    let voltage = flags.iter().any(|x| x == "--voltage");
    let current = flags.iter().any(|x| x == "--current");
    let extreme = flags.iter().any(|x| x == "--extreme");
    let long_history = flags.iter().any(|x| x == "--long-history");
    let mut output = BufWriter::new(File::create(path)?);
    let mut meter = Meter::new(0);
    if long_history {
        for second in 0..=600 {
            let reading = if (300..310).contains(&second) {
                None
            } else {
                Some(Reading {
                    mv: 5000,
                    ma: if second < 300 { 1000 } else { 2000 },
                })
            };
            meter.sample(second * 1000, reading);
        }
    } else if measurement {
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
    if profiles_offer {
        let frame: Vec<u8> = include_str!("../../tests/pd_offer.hex")
            .split_whitespace()
            .map(|hex| u8::from_str_radix(hex, 16).unwrap())
            .collect();
        let caps = SourceCaps::from_fifo(&frame)
            .unwrap()
            .expect("test source offer");
        view.set_pd_profiles(2, 1, Some(caps));
    } else if profiles_ui
        || profiles_page
        || profiles_listening
        || epr_page
        || epr_timeout
        || epr_offer
        || epr_partial
        || epr_diag
        || sniff_page
        || sniff_log
        || sniff_old
        || sniff_alert
    {
        view.set_pd_profiles(0, 0, None);
    }
    if profiles_listening {
        view.set_pd_profiles(1, 1, None);
    }
    if epr_timeout {
        view.set_pd_profiles(3, 1, None);
        view.set_epr_profiles(1, None);
    }
    if epr_offer || epr_partial {
        let parse = |text: &str| -> Vec<u8> {
            text.split_whitespace()
                .map(|hex| u8::from_str_radix(hex, 16).unwrap())
                .collect()
        };
        let first = parse(include_str!("../../tests/epr_chunk0.hex"));
        let second = parse(include_str!("../../tests/epr_chunk1.hex"));
        let mut capture = EprCapture::new();
        if epr_offer {
            assert!(matches!(capture.push(&first, 100), Ok(EprEvent::Started)));
        }
        let caps = match capture.push(&second, 120).unwrap() {
            EprEvent::Complete(caps) if epr_offer => caps,
            EprEvent::Partial(caps) if epr_partial => caps,
            _ => panic!("unexpected test EPR frame"),
        };
        view.set_pd_profiles(if epr_offer { 2 } else { 10 }, 1, None);
        view.set_epr_profiles(1, Some(caps));
        view.set_epr_counters(capture.counters());
    }
    if epr_diag {
        view.set_pd_profiles(1, 1, None);
        view.set_epr_profiles(1, None);
        view.set_epr_counters(u32::from_le_bytes([1, 1, 0, 0]));
    }
    if sniff_log || sniff_old || sniff_alert {
        let parse = |text: &str| -> Vec<u8> {
            text.split_whitespace()
                .map(|hex| u8::from_str_radix(hex, 16).unwrap())
                .collect()
        };
        let mut sniff = PdSniff::new();
        sniff
            .push(&parse(include_str!("../../tests/pd_offer.hex")))
            .unwrap();
        for exchange in [
            include_str!("../../tests/sniff_exchange.hex"),
            include_str!("../../tests/sniff_epr.hex"),
            include_str!("../../tests/sniff_keepalive.hex"),
        ] {
            for line in exchange.lines() {
                sniff.push(&parse(line)).unwrap();
            }
        }
        view.set_pd_profiles(3, 1, None);
        view.set_epr_profiles(2, None);
        view.set_pd_warnings(if sniff_alert { 1 << 2 } else { 0 });
        view.set_pd_sniff(Some(&sniff), if sniff_old { 8 } else { 0 });
    }
    if let Some((state, section)) = cable_view {
        let mut cable = CableCapture::new();
        #[cfg(feature = "pd-cable-read")]
        if cable_scan.is_some() {
            for line in include_str!("../../tests/cable_scan.hex").lines() {
                if line.starts_with('#') {
                    continue;
                }
                let mut fields = line.split_whitespace();
                let mut number = || fields.next().unwrap().parse::<u32>().unwrap();
                let (query, index, expected) = (number(), number(), number());
                let frame: Vec<_> = fields
                    .map(|hex| u8::from_str_radix(hex, 16).unwrap())
                    .collect();
                if !frame.is_empty() {
                    assert_eq!(cable.record_query_reply(query, index, &frame), Ok(expected));
                }
                if expected != 0 {
                    cable.record_query_result(query, index, expected);
                }
            }
        }
        #[cfg(feature = "pd-cable-read")]
        if state == 13 {
            cable.set_cc_probe(7, 64);
        }
        if state == 2 && !cable.captured() {
            for line in include_str!("../../tests/cable_identity.hex").lines() {
                let frame: Vec<_> = line
                    .split_whitespace()
                    .map(|hex| u8::from_str_radix(hex, 16).unwrap())
                    .collect();
                cable.push(&frame).unwrap();
            }
            assert!(cable.captured());
        }
        view.set_pd_profiles(state, u32::from(state != 0), None);
        view.set_epr_profiles(3, None);
        view.set_pd_cable(&cable, section);
    }
    view.set_page(if cable_view.is_some() {
        8
    } else if sniff_page || sniff_log || sniff_old || sniff_alert {
        7
    } else if epr_page || epr_timeout || epr_offer || epr_partial || epr_diag {
        6
    } else if profiles_page || profiles_listening || profiles_offer {
        5
    } else if energy {
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
    view.set_graph_window(if flags.iter().any(|x| x == "--window2") {
        1
    } else if flags.iter().any(|x| x == "--window5") {
        2
    } else if flags.iter().any(|x| x == "--window10") {
        3
    } else {
        0
    });
    meter.overview(&mut view);
    if measurement || long_history {
        view.set_uncalibrated();
    }
    if pd {
        view.set_pd_diagnostic(Some((0x04030291, 0x05)));
    }
    #[cfg(feature = "pd-rd-test")]
    if let Some((state, on_mv)) = flags.iter().find_map(|flag| match flag.to_str()? {
        "--rd-off" => Some((1, u16::MAX)),
        "--rd-5v" => Some((3, 5094)),
        _ => None,
    }) {
        view.set_rd_test(Some(t3p_standalone::RdTest {
            state,
            off_mv: 3010,
            on_mv,
            #[cfg(feature = "pd-ack-test")]
            pdos: if state == 3 { 7 } else { 0 },
            #[cfg(feature = "pd-ack-test")]
            goodcrc: state == 3,
        }));
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
