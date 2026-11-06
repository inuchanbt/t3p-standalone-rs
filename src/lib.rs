// SPDX-License-Identifier: GPL-2.0-or-later
#![no_std]

#[cfg(all(feature = "board", target_os = "none"))]
#[path = "../native/demo.rs"]
mod demo;
#[path = "../vendor/kws-x1/energy.rs"]
mod energy;
#[allow(dead_code)]
#[path = "../vendor/kws-x1/power_history.rs"]
mod power_history;
mod screen;

// The imported energy module only needs this part of the KWS-X1 settings API.
mod settings {
    #[allow(dead_code)]
    #[derive(Clone, Copy, PartialEq, Eq)]
    pub enum TimerMode {
        Elapsed,
        Active,
    }
}

use energy::{Totals, Tracker};
use power_history::{GraphWindow, History, Sample};
use settings::TimerMode;

pub const SIDE: usize = 240;
pub const STRIP_ROWS: usize = 8;

#[derive(Clone, Copy)]
pub struct Reading {
    pub mv: u16,
    pub ma: i16,
}

impl Reading {
    fn magnitude_mw(self) -> u32 {
        u32::from(self.mv) * u32::from(self.ma.unsigned_abs()) / 1000
    }
}

pub struct Overview {
    reading: Option<Reading>,
    mah: u64,
    mwh: u64,
    power_mw: [Option<u32>; 60],
}

impl Default for Overview {
    fn default() -> Self {
        Self::new()
    }
}

impl Overview {
    const fn new() -> Self {
        Self {
            reading: None,
            mah: 0,
            mwh: 0,
            power_mw: [None; 60],
        }
    }
}

pub struct Meter {
    tracker: Tracker,
    history: History,
    reading: Option<Reading>,
    last_tick: u32,
    now_ms: u64,
}

impl Meter {
    pub const fn new(tick: u32) -> Self {
        Self {
            tracker: Tracker::new(Totals {
                ma_ms: 0,
                uw_ms: 0,
                elapsed_ms: 0,
            }),
            history: History::new(),
            reading: None,
            last_tick: tick,
            now_ms: 0,
        }
    }

    // The board supplies monotonic milliseconds modulo 2^32. A missing
    // reading breaks the integration interval and is visible in the graph.
    pub fn sample(&mut self, tick: u32, reading: Option<Reading>) {
        self.now_ms += u64::from(tick.wrapping_sub(self.last_tick));
        self.last_tick = tick;
        self.reading = reading;
        self.tracker.sample(
            self.now_ms,
            reading.map(|r| (u32::from(r.mv), i32::from(r.ma))),
            // Active-time mode has no UI here and its Pi32v2 code generation
            // needs separate verification before being enabled.
            TimerMode::Elapsed,
        );
        self.history.sample(
            self.now_ms,
            reading.map(|r| Sample {
                mv: r.mv,
                ma: r.ma.unsigned_abs(),
                mw: r.magnitude_mw(),
            }),
        );
    }

    pub fn reset_totals(&mut self) {
        self.tracker.reset();
    }

    pub fn overview(&self, view: &mut Overview) {
        view.reading = self.reading;
        view.mah = self.tracker.totals.milliamp_hours();
        view.mwh = self.tracker.totals.milliwatt_hours();
        for (out, sample) in view
            .power_mw
            .iter_mut()
            .zip(self.history.recent(GraphWindow::Min1))
        {
            *out = sample.map(|s| s.mw);
        }
    }
}

// Deterministic input shared by the board demo and the PC preview.
pub fn demo_reading(tick: u32) -> Option<Reading> {
    let second = (tick / 1000) % 24;
    let reading = match second {
        0..=5 => Reading { mv: 5038, ma: 750 },
        6..=11 => Reading { mv: 9032, ma: 1690 },
        12..=13 => return None,
        14..=17 => Reading {
            mv: 12012,
            ma: -1210,
        },
        _ => Reading { mv: 5038, ma: 280 },
    };
    Some(reading)
}

pub use screen::render_demo_strip;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn handles_clock_wrap_and_resets_only_totals() {
        let first = u32::MAX - 499;
        let mut meter = Meter::new(first);
        let reading = Some(Reading {
            mv: 5000,
            ma: -3600,
        });
        meter.sample(first, reading);
        meter.sample(first.wrapping_add(1000), reading);
        let mut view = Overview::default();
        meter.overview(&mut view);
        assert_eq!((view.mah, view.mwh), (1, 5));
        assert_eq!(view.power_mw[59], Some(18_000));
        meter.reset_totals();
        meter.sample(first.wrapping_add(1100), None);
        meter.overview(&mut view);
        assert_eq!((view.mah, view.mwh), (0, 0));
        assert_eq!(view.power_mw[59], Some(18_000));
        assert!(view.reading.is_none());
    }

    #[test]
    fn reports_missing_seconds_as_gaps() {
        let mut meter = Meter::new(0);
        meter.sample(0, Some(Reading { mv: 5000, ma: 1000 }));
        meter.sample(1000, None);
        meter.sample(2000, Some(Reading { mv: 5000, ma: 2000 }));
        let mut view = Overview::default();
        meter.overview(&mut view);
        assert_eq!(view.power_mw[58..], [Some(5000), None]);
        assert_eq!((view.mah, view.mwh), (0, 0));
    }
}
