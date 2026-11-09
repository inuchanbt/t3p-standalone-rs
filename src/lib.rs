// SPDX-License-Identifier: GPL-2.0-or-later
#![no_std]

#[cfg(all(feature = "board", target_os = "none"))]
#[path = "../native/demo.rs"]
mod demo;
#[path = "../vendor/kws-x1/energy.rs"]
mod energy;
mod pd;
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
pub use pd::{CableCapture, EprCaps, EprCapture, EprEvent, PdSniff, SourceCaps};
use pd::{PD_PAGE_COUNT, PROFILES_PAGE, Profiles};
use power_history::{GraphTrace, GraphWindow, History, Sample};
use settings::TimerMode;

pub const SIDE: usize = 240;
pub const STRIP_ROWS: usize = 8;

#[cfg(feature = "pd-rd-test")]
#[derive(Clone, Copy)]
pub struct RdTest {
    pub state: u8,
    pub off_mv: u16,
    pub on_mv: u16,
    #[cfg(feature = "pd-ack-test")]
    pub pdos: u8,
    #[cfg(feature = "pd-ack-test")]
    pub goodcrc: bool,
}

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
    uncalibrated: bool,
    pd_diagnostic: Option<Option<(u32, u8)>>,
    pd_profiles: Option<Profiles>,
    #[cfg(feature = "pd-rd-test")]
    rd_test: Option<RdTest>,
    mah: u64,
    mwh: u64,
    elapsed_seconds: u32,
    peaks: Option<(u16, u16, u32)>,
    page: u32,
    graph_window: u32,
    graph_values: [Option<u32>; 60],
    graph_breaks: [bool; 60],
    graph_max: u32,
    graph_heights: [u8; 60],
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
            uncalibrated: false,
            pd_diagnostic: None,
            pd_profiles: None,
            #[cfg(feature = "pd-rd-test")]
            rd_test: None,
            mah: 0,
            mwh: 0,
            elapsed_seconds: 0,
            peaks: None,
            page: 0,
            graph_window: 0,
            graph_values: [None; 60],
            graph_breaks: [false; 60],
            graph_max: 1000,
            graph_heights: [255; 60],
        }
    }

    pub fn set_uncalibrated(&mut self) {
        self.uncalibrated = true;
    }

    pub fn set_pd_diagnostic(&mut self, registers: Option<(u32, u8)>) {
        self.pd_diagnostic = Some(registers);
    }

    #[cfg(feature = "pd-rd-test")]
    pub fn set_rd_test(&mut self, test: Option<RdTest>) {
        self.rd_test = test;
    }

    pub fn set_pd_profiles(&mut self, state: u32, cc: u32, caps: Option<SourceCaps>) {
        self.pd_profiles = Some(Profiles {
            state,
            cc,
            caps,
            epr: None,
            mode: 0,
            epr_counters: 0,
            sniff: None,
            cable: CableCapture::new(),
            cable_section: 0,
            fifo_warnings: 0,
        });
    }

    pub fn set_pd_warnings(&mut self, warnings: u32) {
        if let Some(ref mut profiles) = self.pd_profiles {
            profiles.fifo_warnings = warnings;
        }
    }

    pub fn set_epr_profiles(&mut self, mode: u32, caps: Option<EprCaps>) {
        if let Some(ref mut profiles) = self.pd_profiles {
            profiles.mode = mode;
            profiles.epr = caps;
        }
    }

    pub fn set_epr_counters(&mut self, counters: u32) {
        if let Some(ref mut profiles) = self.pd_profiles {
            profiles.epr_counters = counters;
        }
    }

    pub fn set_pd_sniff(&mut self, sniff: Option<&PdSniff>, offset: u8) {
        if let Some(ref mut profiles) = self.pd_profiles {
            profiles.sniff = sniff.map(|history| history.window(offset));
        }
    }

    pub fn set_pd_cable(&mut self, capture: &CableCapture, section: u8) {
        if let Some(ref mut profiles) = self.pd_profiles {
            profiles.cable = *capture;
            profiles.cable_section = section.min(capture.sections() - 1);
        }
    }

    pub fn set_page(&mut self, page: u32) {
        self.page = page
            % if self.pd_profiles.is_some() {
                PD_PAGE_COUNT
            } else {
                PROFILES_PAGE
            };
    }

    pub fn set_graph_window(&mut self, window: u32) {
        self.graph_window = window % 4;
    }

    fn update_graph(&mut self) {
        self.graph_max = screen::graph_scale(
            self.graph_values
                .iter()
                .copied()
                .flatten()
                .max()
                .unwrap_or(0),
        )
        .max(1);
        for (out, sample) in self.graph_heights.iter_mut().zip(self.graph_values.iter()) {
            *out = sample
                .map(|value| (u64::from(value) * 85 / u64::from(self.graph_max)) as u8)
                .unwrap_or(255);
        }
    }
}

pub struct Meter {
    tracker: Tracker,
    history: History,
    reading: Option<Reading>,
    last_tick: u32,
    now_ms: u64,
    peaks: Option<(u16, u16, u32)>,
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
            peaks: None,
        }
    }

    // The board supplies monotonic milliseconds modulo 2^32. A missing
    // reading breaks the integration interval and is visible in the graph.
    pub fn sample(&mut self, tick: u32, reading: Option<Reading>) {
        self.now_ms += u64::from(tick.wrapping_sub(self.last_tick));
        self.last_tick = tick;
        self.reading = reading;
        if let Some(r) = reading {
            let (v, a, w) = self.peaks.unwrap_or((0, 0, 0));
            self.peaks = Some((
                v.max(r.mv),
                a.max(r.ma.unsigned_abs()),
                w.max(r.magnitude_mw()),
            ));
        }
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
        self.peaks = None;
    }

    pub fn overview(&self, view: &mut Overview) {
        view.reading = self.reading;
        view.peaks = self.peaks;
        view.mah = self.tracker.totals.milliamp_hours();
        view.mwh = self.tracker.totals.milliwatt_hours();
        view.elapsed_seconds = (self.tracker.totals.elapsed_ms / 1000).min(359_999) as u32;
        if (2..PROFILES_PAGE).contains(&view.page) {
            let trace = match view.page {
                3 => GraphTrace::Voltage,
                4 => GraphTrace::Current,
                _ => GraphTrace::Power,
            };
            let window = match view.graph_window {
                1 => GraphWindow::Min2,
                2 => GraphWindow::Min5,
                3 => GraphWindow::Min10,
                _ => GraphWindow::Min1,
            };
            let per_bin = window.seconds() / 60;
            let mut history = self.history.recent(window);
            for (out, gap) in view
                .graph_values
                .iter_mut()
                .zip(view.graph_breaks.iter_mut())
            {
                let mut sum = 0u32;
                let mut count = 0u32;
                let mut missing = false;
                for _ in 0..per_bin {
                    if let Some(Some(sample)) = history.next() {
                        sum = sum.saturating_add(trace.value(sample));
                        count += 1;
                    } else {
                        missing = true;
                    }
                }
                *out = if count == 0 {
                    None
                } else {
                    Some(sum / count.max(1))
                };
                *gap = missing && count != 0;
            }
            view.update_graph();
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
        view.set_page(2);
        meter.overview(&mut view);
        assert_eq!((view.mah, view.mwh), (1, 5));
        assert_eq!(view.graph_values[59], Some(18_000));
        meter.reset_totals();
        meter.sample(first.wrapping_add(1100), None);
        meter.overview(&mut view);
        assert_eq!((view.mah, view.mwh), (0, 0));
        assert_eq!(view.graph_values[59], Some(18_000));
        assert!(view.reading.is_none());
    }

    #[test]
    fn reports_missing_seconds_as_gaps() {
        let mut meter = Meter::new(0);
        meter.sample(0, Some(Reading { mv: 5000, ma: 1000 }));
        meter.sample(1000, None);
        meter.sample(2000, Some(Reading { mv: 5000, ma: 2000 }));
        let mut view = Overview::default();
        view.set_page(2);
        meter.overview(&mut view);
        assert_eq!(view.graph_values[58..], [Some(5000), None]);
        assert_eq!((view.mah, view.mwh), (0, 0));
    }

    #[test]
    fn elapsed_time_counts_missing_samples_and_resets() {
        let mut meter = Meter::new(0);
        meter.sample(0, None);
        meter.sample(65_999, None);
        let mut view = Overview::default();
        meter.overview(&mut view);
        assert_eq!(view.elapsed_seconds, 65);
        meter.reset_totals();
        meter.overview(&mut view);
        assert_eq!(view.elapsed_seconds, 0);
        meter.sample(66_999, None);
        meter.overview(&mut view);
        assert_eq!(view.elapsed_seconds, 1);
        meter.sample(u32::MAX, None);
        meter.overview(&mut view);
        assert_eq!(view.elapsed_seconds, 359_999);
    }

    #[test]
    fn peaks_use_independent_magnitudes_and_reset_without_losing_reading() {
        let mut meter = Meter::new(0);
        meter.sample(
            0,
            Some(Reading {
                mv: 5000,
                ma: -3000,
            }),
        );
        meter.sample(100, Some(Reading { mv: 20000, ma: 500 }));
        meter.sample(200, None);
        let mut view = Overview::default();
        meter.overview(&mut view);
        assert_eq!(view.peaks, Some((20000, 3000, 15000)));
        meter.sample(300, Some(Reading { mv: 5000, ma: 1000 }));
        meter.reset_totals();
        meter.overview(&mut view);
        assert_eq!(view.peaks, None);
        assert_eq!(view.reading.map(|r| (r.mv, r.ma)), Some((5000, 1000)));
        meter.sample(400, Some(Reading { mv: 5000, ma: 1000 }));
        meter.overview(&mut view);
        assert_eq!(view.peaks, Some((5000, 1000, 5000)));
    }

    #[test]
    fn graph_pages_select_the_existing_history_without_losing_it() {
        let mut meter = Meter::new(0);
        meter.sample(
            0,
            Some(Reading {
                mv: 5000,
                ma: -2000,
            }),
        );
        meter.sample(1000, None);
        let mut view = Overview::default();
        for (page, expected) in [(2, 10000), (3, 5000), (4, 2000), (7, 10000)] {
            view.set_page(page);
            meter.overview(&mut view);
            assert_eq!(view.graph_values[59], Some(expected));
        }
    }

    #[test]
    fn longer_windows_average_bins_and_mark_partial_gaps() {
        let mut meter = Meter::new(0);
        meter.sample(0, Some(Reading { mv: 5000, ma: 1000 }));
        meter.sample(1000, Some(Reading { mv: 5000, ma: 2000 }));
        meter.sample(2000, None);
        meter.sample(3000, Some(Reading { mv: 5000, ma: 3000 }));
        meter.sample(4000, None);
        let mut view = Overview::default();
        view.set_page(2);
        view.set_graph_window(1);
        meter.overview(&mut view);
        assert_eq!(view.graph_values[58..], [Some(7500), Some(15000)]);
        assert_eq!(view.graph_breaks[58..], [false, true]);
        view.set_graph_window(2);
        meter.overview(&mut view);
        assert_eq!(view.graph_values[59], Some(10000));
        assert!(view.graph_breaks[59]);
        view.set_graph_window(3);
        meter.overview(&mut view);
        assert_eq!(view.graph_values[59], Some(10000));
        assert!(view.graph_breaks[59]);
        view.set_graph_window(4);
        meter.overview(&mut view);
        assert_eq!(view.graph_window, 0);
        assert_eq!(
            view.graph_values[56..],
            [Some(5000), Some(10000), None, Some(15000)]
        );
    }
}
