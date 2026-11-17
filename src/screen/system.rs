// SPDX-License-Identifier: GPL-2.0-or-later
use super::cable::hex;
use super::{Overview, Strip, AMPS, GRID, MUTED, TEXT, VOLTS, WATTS};
use crate::system::{BASE_IMAGE_VERSION, FIRMWARE_VERSION, SYSTEM_SECTIONS};

fn decimal(mut value: u32) -> [u8; 10] {
    let mut text = [b'0'; 10];
    for digit in text.iter_mut().rev() {
        *digit += (value % 10) as u8;
        value /= 10;
    }
    text
}

fn uptime(ms: u64) -> Option<[u8; 20]> {
    let seconds = ms / 1000;
    let days = decimal(u32::try_from(seconds / 86_400).ok()?);
    let mut text = *b"0000000000D 00:00:00";
    text[..10].copy_from_slice(&days);
    for (at, value) in [
        (12, seconds / 3600 % 24),
        (15, seconds / 60 % 60),
        (18, seconds % 60),
    ] {
        text[at] += (value / 10) as u8;
        text[at + 1] += (value % 10) as u8;
    }
    Some(text)
}

fn temperature_label() -> &'static [u8] {
    if cfg!(any(
        feature = "temperature-ens220",
        feature = "capture-ens220"
    )) {
        b"ENS220 TEMP"
    } else {
        b"BMP TEMP"
    }
}

fn row(p: &mut Strip<'_>, index: usize, label: &[u8], value: &[u8], color: u16) {
    let y = 49 + index * 18;
    p.text(12, y, label, 1, MUTED);
    p.text(112, y, value, 1, color);
    p.rect(8, y + 12, 224, 1, GRID);
}

pub(super) fn render(p: &mut Strip<'_>, view: &Overview) {
    let section = view.system_section;
    #[cfg(all(feature = "temperature-diagnostics", not(feature = "capture-ens220")))]
    if section >= 3 {
        render_temperature(p, view);
        return;
    }
    p.text(
        8,
        36,
        [
            b"SYSTEM INFO" as &[u8],
            b"BUS AND FRAMES",
            b"INPUT AND RUNTIME",
        ][section as usize],
        1,
        VOLTS,
    );
    p.text(
        215,
        36,
        &[b'1' + section, b'/', b'0' + SYSTEM_SECTIONS],
        1,
        AMPS,
    );
    let stats = view.system;
    let number = |p: &mut Strip<'_>, index, label: &[u8], value: Option<u32>| {
        if let Some(value) = value {
            row(p, index, label, &decimal(value), WATTS);
        } else {
            row(p, index, label, b"N/A", MUTED);
        }
    };
    match section {
        0 => {
            row(p, 0, b"APP", FIRMWARE_VERSION, TEXT);
            row(p, 1, b"BASE IMAGE", BASE_IMAGE_VERSION, TEXT);
            if let Some(s) = stats {
                if let Some(time) = uptime(s.uptime_ms()) {
                    row(p, 2, b"UPTIME", &time, WATTS);
                } else {
                    row(p, 2, b"UPTIME", b"OVERFLOW", VOLTS);
                }
            } else {
                row(p, 2, b"UPTIME", b"N/A", MUTED);
            }
            for (index, label, flag, value) in [
                (3, b"METER DIE" as &[u8], 1, stats.map(|s| s.meter_id)),
                (4, b"METER CONFIG", 2, stats.map(|s| s.meter_config)),
            ] {
                if let Some(value) =
                    value.filter(|_| stats.is_some_and(|s| s.meter_flags & flag != 0))
                {
                    row(p, index, label, &hex::<4>(value), TEXT);
                } else {
                    row(p, index, label, b"N/A", MUTED);
                }
            }
            let (state, color): (&[u8], u16) = match stats.map(|s| s.meter_state) {
                Some(1) => (b"OK", WATTS),
                Some(2) => (b"CHECK FAIL", VOLTS),
                Some(3) => (b"SAMPLE FAIL", VOLTS),
                _ => (b"N/A", MUTED),
            };
            row(p, 5, b"METER STATE", state, color);
            row(p, 6, b"METER FW", b"N/A", MUTED);
            if let Some(s) =
                stats.filter(|s| s.temp_valid == 1 && (-4000..=8500).contains(&s.temp_centi))
            {
                let n = s.temp_centi.unsigned_abs();
                let mut text = *b"+00.00 C";
                text[0] = if s.temp_centi < 0 { b'-' } else { b'+' };
                text[1] += (n / 1000) as u8;
                text[2] += (n / 100 % 10) as u8;
                text[4] += (n / 10 % 10) as u8;
                text[5] += (n % 10) as u8;
                row(p, 7, temperature_label(), &text, WATTS);
            } else {
                row(p, 7, temperature_label(), b"N/A", MUTED);
            }
            row(p, 8, b"SERIAL", b"N/A", MUTED);
        }
        1 => {
            for (i, label, value) in [
                (0, b"I2C READS" as &[u8], stats.map(|s| s.reads)),
                (1, b"READ ERRORS", stats.map(|s| s.read_errors)),
                (2, b"I2C WRITES", stats.map(|s| s.writes)),
                (3, b"WRITE ERRORS", stats.map(|s| s.write_errors)),
                (4, b"SAMPLES", stats.map(|s| s.samples)),
                (5, b"SAMPLE ERRORS", stats.map(|s| s.sample_errors)),
                (6, b"FRAME COUNT", stats.map(|s| s.frames)),
                (
                    7,
                    b"FRAME LAST MS",
                    stats.filter(|s| s.frames != 0).map(|s| s.frame_ms),
                ),
                (
                    8,
                    b"FRAME MAX MS",
                    stats.filter(|s| s.frames != 0).map(|s| s.frame_max_ms),
                ),
            ] {
                number(p, i, label, value);
            }
        }
        _ => {
            number(p, 0, b"DBI OPS", stats.map(|s| s.dbi_ops));
            number(p, 1, b"SHORT COUNT", stats.map(|s| s.short_presses));
            number(p, 2, b"DOUBLE COUNT", stats.map(|s| s.double_presses));
            number(p, 3, b"HOLD COUNT", stats.map(|s| s.long_presses));
            let event: &[u8] = match stats.map(|s| s.key_event) {
                Some(1) => b"SHORT",
                Some(2) => b"DOUBLE",
                Some(3) => b"HOLD",
                _ => b"N/A",
            };
            row(p, 4, b"KEY EVENT", event, TEXT);
            number(
                p,
                5,
                b"KEY DELAY MS",
                stats
                    .filter(|s| (1..=3).contains(&s.key_event))
                    .map(|s| s.key_ms),
            );
            row(p, 6, b"STACK USED", b"N/A", MUTED);
            row(p, 7, b"RESET REASON", b"N/A", MUTED);
            row(p, 8, b"FLASH CHECK", b"N/A", MUTED);
        }
    }
}

#[cfg(all(feature = "temperature-diagnostics", not(feature = "capture-ens220")))]
fn render_temperature(p: &mut Strip<'_>, view: &Overview) {
    use crate::system::TemperatureDiagnostics;
    let d = view.system.map(|s| s.temperature).filter(|d| {
        d.magic == 0x31444d54 && (d.version == 1 || (d.version == 3 && d.sensor_kind == 2))
    });
    let section = view.system_section;
    let title: &[u8] = match section {
        3 => b"TEMP ACQUISITION",
        4 => b"TEMP PB2/PB1 R/F",
        _ => b"TEMP PA5/PA6 B/A",
    };
    p.text(8, 36, title, 1, VOLTS);
    p.text(
        215,
        36,
        &[b'1' + section, b'/', b'0' + SYSTEM_SECTIONS],
        1,
        AMPS,
    );
    let Some(d) = d else {
        row(p, 0, b"DIAGNOSTICS", b"N/A", MUTED);
        return;
    };
    if section == 3 {
        row(p, 0, b"ATTEMPTS", &decimal(d.attempts), TEXT);
        row(p, 1, b"FAILURES", &decimal(d.failures), TEXT);
        let phase: &[u8] = match d.phase {
            1 => b"ID",
            2 => b"PRE STATUS",
            3 => b"CALIBRATION",
            4 => b"TRIGGER",
            5 => b"WAIT",
            6 => b"POST STATUS",
            7 => b"RAW",
            8 => b"COMPENSATE",
            9 => b"DONE",
            10 => b"CONFIGURE",
            11 => b"CLEANUP",
            _ => b"IDLE",
        };
        row(p, 2, b"PHASE", phase, TEXT);
        let reason: &[u8] = if d.result == 1 {
            match d.bus_error {
                1 => b"IO DELAY",
                2 => b"IO SCL LOW",
                3 => b"IO SDA LOW",
                4 => b"IO NACK",
                5 => b"IO ARGUMENT",
                _ => {
                    if d.stop_error != 0 {
                        b"IO STOP"
                    } else {
                        b"IO"
                    }
                }
            }
        } else {
            match d.result {
                0 => b"OK",
                2 => b"BAD ID",
                3 => b"BUSY",
                4 => b"BAD CAL",
                5 => b"TIMEOUT",
                6 => b"SKIPPED",
                7 => b"RANGE",
                8 => b"CANCELLED",
                9 => b"CONFIG MISMATCH",
                _ => b"UNKNOWN",
            }
        };
        row(p, 3, b"RESULT", reason, VOLTS);
        let stage: &[u8] = match d.stage {
            1 => b"IDLE",
            2 => b"START",
            3 => b"ADDR W",
            4 => b"REG",
            5 => b"RESTART",
            6 => b"ADDR R",
            7 => b"DATA",
            8 => b"STOP",
            9 => b"DONE",
            _ => b"NONE",
        };
        let mut text = [b' '; 18];
        text[..2].copy_from_slice(&hex::<2>(d.reg));
        text[3..3 + stage.len()].copy_from_slice(stage);
        row(
            p,
            4,
            b"REG / STAGE",
            if d.transactions != 0 { &text } else { b"N/A" },
            TEXT,
        );
        let mut ack = *b"0000/0000";
        ack[..4].copy_from_slice(&hex::<4>(d.ack_valid));
        ack[5..].copy_from_slice(&hex::<4>(d.ack_seen));
        row(
            p,
            5,
            b"ACK V / OK",
            if d.transactions != 0 { &ack } else { b"N/A" },
            TEXT,
        );
        let ens = d.version == 3 && d.sensor_kind == 2;
        if ens {
            let mut ids = *b"---- / --";
            if d.valid & 1 != 0 {
                ids[..4].copy_from_slice(&hex::<4>(d.id));
            }
            if d.valid & 2 != 0 {
                ids[7..].copy_from_slice(&hex::<2>(d.status));
            }
            row(p, 6, b"ID / STATUS", &ids, TEXT);
        } else {
            let mut ids = *b"-- / --";
            if d.valid & 1 != 0 {
                ids[..2].copy_from_slice(&hex::<2>(d.id));
            }
            if d.valid & 2 != 0 {
                ids[5..].copy_from_slice(&hex::<2>(d.status));
            }
            row(p, 6, b"ID / STATUS", &ids, TEXT);
        }
        let adc = hex::<6>(d.adc);
        row(
            p,
            7,
            if ens { b"TEMP RAW" } else { b"RAW ADC" },
            if d.valid & 16 != 0 { &adc } else { b"N/A" },
            TEXT,
        );
        let mut gate = *b"00 / TIMER N/A";
        gate[..2].copy_from_slice(&hex::<2>(d.gate));
        if d.timer_ready != 0 {
            gate[11..].copy_from_slice(b"OK ");
        }
        row(p, 8, b"GATE / CLOCK", &gate, TEXT);
    } else {
        let (a, b, mask) = if section == 4 {
            (2, 3, 6)
        } else {
            (0, 1, 0x60)
        };
        fn field(d: &TemperatureDiagnostics, snapshot: usize, index: usize, mask: u32) -> [u8; 2] {
            if d.gpio_valid & (1 << snapshot) != 0 {
                hex::<2>(d.pins[snapshot][index] & mask)
            } else {
                *b"--"
            }
        }
        for (row_index, label, first, second) in [
            (0, b"IN" as &[u8], 0, None),
            (1, b"DIR", 2, None),
            (2, b"OUT", 1, None),
            (3, b"DIE / DIEH", 3, Some(4)),
            (4, b"PU0 / PU1", 5, Some(6)),
            (5, b"PD0 / PD1", 7, Some(8)),
            (6, b"HD0 / HD1", 9, Some(10)),
            (7, b"SPL / CON", 11, Some(12)),
        ] {
            let mut value = *b"--,--/--,--";
            value[..2].copy_from_slice(&field(&d, a, first, mask));
            value[6..8].copy_from_slice(&field(&d, b, first, mask));
            if let Some(second) = second {
                value[3..5].copy_from_slice(&field(&d, a, second, mask));
                value[9..].copy_from_slice(&field(&d, b, second, mask));
                row(p, row_index, label, &value, TEXT);
            } else {
                let pair = [value[0], value[1], b'/', value[6], value[7]];
                row(p, row_index, label, &pair, TEXT);
            }
        }
        row(
            p,
            8,
            b"LEFT / RIGHT",
            if section == 4 {
                b"RELEASE / FAULT"
            } else {
                b"BEFORE / AFTER"
            },
            TEXT,
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn uptime_retains_days_after_timer_wrap_and_formats_extremes() {
        assert_eq!(&uptime(4_294_967_296).unwrap(), b"0000000049D 17:02:47");
        assert_eq!(&decimal(u32::MAX), b"4294967295");
        assert_eq!(&uptime(0).unwrap(), b"0000000000D 00:00:00");
        assert_eq!(uptime(u64::MAX), None);
    }
}
