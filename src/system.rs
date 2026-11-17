// SPDX-License-Identifier: GPL-2.0-or-later

pub const SYSTEM_PAGE: u32 = crate::pd::PD_PAGE_COUNT;
pub const SYSTEM_SECTIONS: u8 =
    if cfg!(feature = "temperature-diagnostics") && !cfg!(feature = "capture-ens220") {
        6
    } else {
        3
    };
#[cfg(not(feature = "capture-device"))]
pub const FIRMWARE_VERSION: &[u8] = b"T3P RS SYSTEM V2";
#[cfg(all(
    feature = "capture-device",
    not(feature = "capture-sfdp-probe"),
    not(feature = "capture-timing-baseline"),
    not(feature = "temperature-diagnostics")
))]
pub const FIRMWARE_VERSION: &[u8] = b"T3P RS CAPTURE V5";
#[cfg(all(
    feature = "capture-timing-baseline",
    not(feature = "capture-sfdp-probe")
))]
pub const FIRMWARE_VERSION: &[u8] = b"T3P RS CAP V5 BASE";
#[cfg(feature = "capture-sfdp-probe")]
pub const FIRMWARE_VERSION: &[u8] = b"T3P RS SFDP PROBE";
#[cfg(all(
    feature = "temperature-diagnostics",
    not(feature = "capture-sfdp-probe"),
    not(feature = "capture-timing-baseline"),
    not(feature = "temperature-probe"),
    not(feature = "capture-ens220")
))]
pub const FIRMWARE_VERSION: &[u8] = b"T3P TEMP DIAG V1";
#[cfg(all(feature = "temperature-probe", not(feature = "temperature-ens220")))]
pub const FIRMWARE_VERSION: &[u8] = b"T3P TEMP PROBE V2";
#[cfg(all(feature = "temperature-ens220", not(feature = "capture-ens220")))]
pub const FIRMWARE_VERSION: &[u8] = b"T3P ENS220 TEMP V3";
#[cfg(feature = "capture-ens220")]
pub const FIRMWARE_VERSION: &[u8] = b"T3P RS CAPTURE V6.1";
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
    #[cfg(feature = "temperature-diagnostics")]
    pub temperature: TemperatureDiagnostics,
}

/// All words match native/t3p_temperature_diag.h; validity flags qualify data.
#[cfg(feature = "temperature-diagnostics")]
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TemperatureDiagnostics {
    pub magic: u32,
    pub version: u32,
    pub sequence: u32,
    pub attempts: u32,
    pub successes: u32,
    pub failures: u32,
    pub gate: u32,
    pub phase: u32,
    pub result: u32,
    pub transactions: u32,
    pub reg: u32,
    pub direction: u32,
    pub stage: u32,
    pub bus_error: u32,
    pub byte_index: u32,
    pub bit_index: u32,
    pub ack_seen: u32,
    pub ack_valid: u32,
    pub valid: u32,
    pub id: u32,
    pub status: u32,
    pub adc: u32,
    pub cal0: u32,
    pub cal1: u32,
    pub control: u32,
    pub timer_ready: u32,
    pub timer_con: u32,
    pub timer_count: u32,
    pub gpio_valid: u32,
    pub stop_error: u32,
    pub bus_port: u32,
    pub scl_mask: u32,
    pub sda_mask: u32,
    pub init_port: u32,
    pub active_port: u32,
    pub temp_centi: i32,
    pub temp_valid: u32,
    pub updated_ms: u32,
    pub attempted_ms: u32,
    pub sensor_kind: u32,
    pub pins: [[u32; 14]; 5],
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
        assert_eq!(
            core::mem::size_of::<SystemStats>(),
            (23 + if cfg!(feature = "temperature-diagnostics") {
                110
            } else {
                0
            }) * 4
        );
        #[cfg(feature = "temperature-diagnostics")]
        {
            assert_eq!(core::mem::size_of::<TemperatureDiagnostics>(), 440);
            assert_eq!(core::mem::offset_of!(SystemStats, temperature), 92);
            assert_eq!(core::mem::offset_of!(TemperatureDiagnostics, pins), 160);
        }
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
