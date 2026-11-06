//! Charge and energy integration from fresh, timestamped meter samples, and
//! the time since the totals were last reset.

use crate::settings::TimerMode;

/// A gap beyond ten nominal 100 ms samples is unknown usage, not a reading
/// to extrapolate indefinitely across a failed bus or a stalled task.
const MAX_GAP_MS: u64 = 1000;
/// Stock firmware's active-time threshold: 0.2 W, in microwatts. Strictly
/// greater power counts; the stock statistics timer uses this threshold.
const ACTIVE_POWER_UW: u64 = 200_000;
/// mA*ms per mAh, and uW*ms per mWh (3600 seconds per hour).
const MA_MS_PER_MAH: u64 = 3_600_000;
const UW_MS_PER_MWH: u64 = 3_600_000_000;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Totals {
    pub ma_ms: u64,
    pub uw_ms: u64,
    /// Time counted since the last reset, according to the selected mode.
    pub elapsed_ms: u64,
}

impl Totals {
    pub fn milliamp_hours(self) -> u64 {
        self.ma_ms / MA_MS_PER_MAH
    }

    pub fn milliwatt_hours(self) -> u64 {
        self.uw_ms / UW_MS_PER_MWH
    }
}

pub struct Tracker {
    pub totals: Totals,
    previous: Option<(u64, u32, u32)>,
    /// When the last sample was taken, reading or not, for elapsed mode.
    last_ms: Option<u64>,
}

impl Tracker {
    pub const fn new(totals: Totals) -> Self {
        Self {
            totals,
            previous: None,
            last_ms: None,
        }
    }

    /// Integrate fresh samples, counting current in either direction. Active
    /// time requires two above-threshold readings and a bounded interval;
    /// missing samples and long gaps cannot establish that power was flowing.
    pub fn sample(&mut self, now_ms: u64, reading: Option<(u32, i32)>, mode: TimerMode) {
        if mode == TimerMode::Elapsed
            && let Some(then) = self.last_ms
        {
            let dt = now_ms.saturating_sub(then);
            self.totals.elapsed_ms = self.totals.elapsed_ms.saturating_add(dt);
        }
        self.last_ms = Some(now_ms);
        let next = reading.map(|(mv, ma)| (now_ms, mv, ma.unsigned_abs()));
        if let (Some((then, mv, ma)), Some((now, next_mv, next_ma))) = (self.previous, next)
            && let Some(dt) = now.checked_sub(then).filter(|dt| *dt <= MAX_GAP_MS)
        {
            let charge = (u64::from(ma) + u64::from(next_ma)) * dt / 2;
            let power = u64::from(mv) * u64::from(ma);
            let next_power = u64::from(next_mv) * u64::from(next_ma);
            if mode == TimerMode::Active && power > ACTIVE_POWER_UW && next_power > ACTIVE_POWER_UW
            {
                self.totals.elapsed_ms = self.totals.elapsed_ms.saturating_add(dt);
            }
            let energy = power.saturating_add(next_power).saturating_mul(dt) / 2;
            self.totals.ma_ms = self.totals.ma_ms.saturating_add(charge);
            self.totals.uw_ms = self.totals.uw_ms.saturating_add(energy);
        }
        self.previous = next;
    }

    /// Zero the totals; elapsed time restarts from the last sample.
    pub fn reset(&mut self) {
        self.totals = Totals::default();
        self.previous = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn integrates_both_directions_and_variable_sample_spacing() {
        let mut tracker = Tracker::new(Totals::default());
        tracker.sample(0, Some((5000, -2000)), TimerMode::Elapsed);
        for time in [100, 350, 1000] {
            tracker.sample(time, Some((5000, 2000)), TimerMode::Elapsed);
        }
        assert_eq!(tracker.totals.ma_ms, 2_000_000);
        assert_eq!(tracker.totals.uw_ms, 10_000_000_000);
        // Repeat one second of 5 V / 2 A for a full hour.
        for time in 2..=3600 {
            tracker.sample(time * 1000, Some((5000, 2000)), TimerMode::Elapsed);
        }
        assert_eq!(tracker.totals.milliamp_hours(), 2000);
        assert_eq!(tracker.totals.milliwatt_hours(), 10_000);
        assert_eq!(tracker.totals.elapsed_ms, 3_600_000);
    }

    // Protect the power boundary, bidirectional discharge and unknown gaps
    // from silently changing what the active timer counts.
    #[test]
    fn active_timer_counts_only_known_above_threshold_intervals() {
        let mut tracker = Tracker::new(Totals::default());
        for (time, reading) in [
            (0, Some((5000, 40))), // Exactly 0.2 W does not count.
            (100, Some((5000, 40))),
            (200, Some((5000, 41))),
            (300, Some((5000, -41))), // Either direction counts.
            (400, None),
            (500, Some((20000, 11))),
            (2000, Some((20000, 11))), // Long gaps are unknown.
            (2100, Some((20000, 11))),
        ] {
            tracker.sample(time, reading, TimerMode::Active);
        }
        assert_eq!(tracker.totals.elapsed_ms, 200);
        tracker.reset();
        tracker.sample(2200, Some((20000, 11)), TimerMode::Active);
        assert_eq!(tracker.totals.elapsed_ms, 0);
        tracker.sample(2300, Some((20000, 11)), TimerMode::Active);
        assert_eq!(tracker.totals.elapsed_ms, 100);
    }

    #[test]
    fn switching_modes_keeps_restored_time() {
        let mut tracker = Tracker::new(Totals {
            elapsed_ms: 5000,
            ..Totals::default()
        });
        tracker.sample(0, Some((5000, 0)), TimerMode::Elapsed);
        tracker.sample(100, Some((5000, 0)), TimerMode::Elapsed);
        tracker.sample(200, Some((5000, 0)), TimerMode::Active);
        tracker.sample(300, Some((5000, 100)), TimerMode::Active);
        tracker.sample(400, Some((5000, 100)), TimerMode::Active);
        tracker.sample(500, None, TimerMode::Elapsed);
        assert_eq!(tracker.totals.elapsed_ms, 5300);
    }

    #[test]
    fn missing_samples_and_long_gaps_are_not_counted_and_reset_breaks_interval() {
        let mut tracker = Tracker::new(Totals::default());
        tracker.sample(0, Some((5000, 1000)), TimerMode::Elapsed);
        tracker.sample(100, None, TimerMode::Elapsed);
        tracker.sample(200, Some((5000, 1000)), TimerMode::Elapsed);
        tracker.sample(2000, Some((5000, 1000)), TimerMode::Elapsed);
        assert_eq!((tracker.totals.ma_ms, tracker.totals.uw_ms), (0, 0));
        assert_eq!(tracker.totals.elapsed_ms, 2000);
        tracker.sample(2100, Some((5000, 1000)), TimerMode::Elapsed);
        assert_eq!(tracker.totals.ma_ms, 100_000);
        tracker.reset();
        tracker.sample(2200, Some((5000, 1000)), TimerMode::Elapsed);
        assert_eq!((tracker.totals.ma_ms, tracker.totals.uw_ms), (0, 0));
        assert_eq!(tracker.totals.elapsed_ms, 100);
    }
}
