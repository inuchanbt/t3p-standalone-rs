// SPDX-License-Identifier: GPL-2.0-or-later
use super::cable::hex;
use super::{AMPS, GRID, MUTED, Overview, Strip, TEXT, VOLTS, WATTS};
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

fn row(p: &mut Strip<'_>, index: usize, label: &[u8], value: &[u8], color: u16) {
    let y = 49 + index * 18;
    p.text(12, y, label, 1, MUTED);
    p.text(112, y, value, 1, color);
    p.rect(8, y + 12, 224, 1, GRID);
}

pub(super) fn render(p: &mut Strip<'_>, view: &Overview) {
    let section = view.system_section;
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
            row(p, 7, b"TEMPERATURE", b"N/A", MUTED);
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
