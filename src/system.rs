// SPDX-License-Identifier: GPL-2.0-or-later

pub const SYSTEM_PAGE: u32 = crate::pd::PD_PAGE_COUNT;
pub const SYSTEM_SECTIONS: u8 = 3;
#[cfg(not(feature = "capture-device"))]
pub const FIRMWARE_VERSION: &[u8] = b"T3P RS SYSTEM V2";
#[cfg(feature = "capture-device")]
pub const FIRMWARE_VERSION: &[u8] = b"T3P RS CAPTURE V1";
pub const BASE_IMAGE_VERSION: &[u8] = b"3.0.3";

/// Native snapshot, matching native/t3p_system.h. Unknown values use flags,
/// never guessed data. Owned boot counters saturate at u32::MAX; dbi_ops
/// snapshots the BSP's existing wrapping 32-bit operation counter.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SystemStats {
    pub uptime_low: u32,
    pub uptime_high: u32,
    pub reads: u32,
    pub read_errors: u32,
    pub writes: u32,
    pub write_errors: u32,
    pub meter_id: u32,
    pub meter_config: u32,
    pub meter_flags: u32,
    pub meter_state: u32,
    pub samples: u32,
    pub sample_errors: u32,
    pub frames: u32,
    pub frame_ms: u32,
    pub frame_max_ms: u32,
    pub dbi_ops: u32,
    pub short_presses: u32,
    pub double_presses: u32,
    pub long_presses: u32,
    pub key_event: u32,
    pub key_ms: u32,
    pub temp_centi: i32,
    pub temp_valid: u32,
}

impl SystemStats {
    pub fn uptime_ms(self) -> u64 {
        (u64::from(self.uptime_high) << 32) | u64::from(self.uptime_low)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn native_snapshot_layout_and_clock_carry_are_preserved() {
        assert_eq!(core::mem::size_of::<SystemStats>(), 23 * 4);
        assert_eq!(core::mem::offset_of!(SystemStats, key_ms), 20 * 4);
        let stats = SystemStats {
            uptime_low: 123,
            uptime_high: 1,
            ..Default::default()
        };
        assert_eq!(stats.uptime_ms(), (1u64 << 32) + 123);
    }

    #[test]
    fn energy_reset_preserves_system_snapshot() {
        let mut meter = crate::Meter::new(0);
        meter.sample(1000, Some(crate::Reading { mv: 5000, ma: 1000 }));
        let stats = SystemStats {
            uptime_low: 5000,
            frames: 7,
            reads: 12,
            ..Default::default()
        };
        let mut view = crate::Overview::default();
        view.set_system(Some(stats), 2);
        meter.reset_totals();
        meter.overview(&mut view);
        assert_eq!(view.system, Some(stats));
        assert_eq!(view.system_section, 2);
    }
}
