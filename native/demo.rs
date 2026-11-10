// SPDX-License-Identifier: GPL-2.0-or-later
#[cfg(feature = "pd-profiles")]
use crate::pd::CABLE_MODE;
use crate::pd::{CABLE_PAGE, EPR_PAGE, PD_PAGE_COUNT, PROFILES_PAGE, SNIFF_PAGE};
#[cfg(feature = "pd-charger")]
use crate::{CHARGER_PAGE, ChargerCapture};
use crate::{Meter, Overview, SIDE, STRIP_ROWS, demo_reading, render_demo_strip};

const PAGE_COUNT: u32 = if cfg!(feature = "pd-profiles") {
    PD_PAGE_COUNT
} else if cfg!(feature = "pd-rd-test") {
    PROFILES_PAGE + 1
} else {
    PROFILES_PAGE
};

unsafe extern "C" {
    fn t3p_signal_failure(stage: u32) -> !;
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    unsafe { t3p_signal_failure(6) }
}

// Only called from t3p_board_start on a single core with interrupts disabled.
// Startup initializes RAM and creates a snapshot before the first draw.
static mut METER: Meter = Meter::new(0);
static mut VIEW: Overview = Overview::new();
static mut UNCALIBRATED: bool = false;
static mut PD_PRESENT: bool = false;
static mut PD_VALID: bool = false;
static mut PD_FIRST: u32 = 0;
static mut PD_LAST: u8 = 0;
static mut PAGE: u32 = 0;
static mut GRAPH_WINDOW: u32 = 0;
#[cfg(feature = "pd-charger")]
static mut CHARGER: ChargerCapture = ChargerCapture::new();
#[cfg(feature = "pd-charger")]
static mut CHARGER_OFFER: bool = false;
#[cfg(feature = "pd-rd-test")]
static mut RD_TEST: crate::RdTest = crate::RdTest {
    state: 0,
    off_mv: u16::MAX,
    on_mv: u16::MAX,
    #[cfg(feature = "pd-ack-test")]
    pdos: 0,
    #[cfg(feature = "pd-ack-test")]
    goodcrc: false,
};
#[cfg(feature = "pd-profiles")]
static mut PD_STATE: u32 = 0;
#[cfg(feature = "pd-profiles")]
static mut PD_CC: u32 = 0;
#[cfg(feature = "pd-profiles")]
static mut PD_WARNINGS: u32 = 0;
#[cfg(feature = "pd-profiles")]
static mut PD_CAPS: Option<crate::SourceCaps> = None;
#[cfg(feature = "pd-profiles")]
static mut PD_MODE: u32 = 0;
#[cfg(feature = "pd-profiles")]
static mut PD_EPR_CAPS: Option<crate::EprCaps> = None;
#[cfg(feature = "pd-profiles")]
static mut PD_EPR_CAPTURE: crate::EprCapture = crate::EprCapture::new();
#[cfg(feature = "pd-profiles")]
static mut PD_SNIFF: crate::PdSniff = crate::PdSniff::new();
#[cfg(feature = "pd-profiles")]
static mut PD_SNIFF_PRESENT: bool = false;
#[cfg(feature = "pd-profiles")]
static mut PD_SNIFF_OFFSET: u8 = 0;
#[cfg(feature = "pd-profiles")]
static mut PD_CABLE: crate::CableCapture = crate::CableCapture::new();
#[cfg(feature = "pd-profiles")]
static mut PD_CABLE_SECTION: u8 = 0;

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_demo_sample(tick: u32) {
    unsafe { (&mut *(&raw mut METER)).sample(tick, demo_reading(tick)) };
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_live_sample(tick: u32, valid: u32, mv: u32, ma: i32) {
    unsafe {
        *(&raw mut UNCALIBRATED) = true;
        let reading = if valid != 0
            && mv <= u16::MAX as u32
            && ma >= i16::MIN as i32
            && ma <= i16::MAX as i32
        {
            Some(crate::Reading {
                mv: mv as u16,
                ma: ma as i16,
            })
        } else {
            None
        };
        (&mut *(&raw mut METER)).sample(tick, reading);
    }
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_diagnostic(valid: u32, first_four: u32, last: u32) {
    unsafe {
        *(&raw mut PD_PRESENT) = true;
        *(&raw mut PD_VALID) = valid != 0;
        *(&raw mut PD_FIRST) = first_four;
        *(&raw mut PD_LAST) = last as u8;
    }
}

#[cfg(feature = "pd-rd-test")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_on_rd_test_page() -> u32 {
    u32::from(unsafe { *(&raw const PAGE) } == PROFILES_PAGE)
}

#[cfg(feature = "pd-rd-test")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_rd_test(state: u32, off_mv: u32, on_mv: u32) {
    unsafe {
        let test = &mut *(&raw mut RD_TEST);
        test.state = state as u8;
        test.off_mv = off_mv as u16;
        test.on_mv = on_mv as u16;
    }
}

#[cfg(feature = "pd-ack-test")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_rd_ack(pdos: u32, goodcrc: u32) {
    unsafe {
        let test = &mut *(&raw mut RD_TEST);
        test.pdos = pdos.min(7) as u8;
        test.goodcrc = goodcrc != 0;
    }
}

#[cfg(feature = "pd-ack-test")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_ack_pdo_count(frame: *const u8, len: u32) -> i32 {
    if frame.is_null() || !(7..=35).contains(&len) {
        return -1;
    }
    // The native FIFO reader owns the initialized record until this call returns.
    let record = unsafe { core::slice::from_raw_parts(frame, len as usize) };
    match crate::SourceCaps::from_fifo(record) {
        Ok(Some(caps)) => caps.rows().len() as i32,
        Ok(None) => 0,
        Err(_) => -1,
    }
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_long_press() {
    unsafe {
        #[cfg(feature = "pd-charger")]
        if *(&raw const PAGE) == CHARGER_PAGE {
            *(&raw mut CHARGER_OFFER) = false;
            *(&raw mut PAGE) = 0;
            return;
        }
        #[cfg(feature = "pd-profiles")]
        if matches!(*(&raw const PAGE), SNIFF_PAGE | CABLE_PAGE) {
            *(&raw mut PD_SNIFF_OFFSET) = 0;
            *(&raw mut PD_CABLE_SECTION) = 0;
            *(&raw mut PAGE) = 0;
            return;
        }
        (&mut *(&raw mut METER)).reset_totals();
    }
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_next_page() {
    unsafe {
        #[cfg(feature = "pd-profiles")]
        if *(&raw const PAGE) == SNIFF_PAGE {
            if *(&raw const PD_SNIFF_PRESENT) {
                if let Some(older) =
                    (&*(&raw const PD_SNIFF)).older_offset(*(&raw const PD_SNIFF_OFFSET))
                {
                    *(&raw mut PD_SNIFF_OFFSET) = older;
                    return;
                }
            }
            *(&raw mut PD_SNIFF_OFFSET) = 0;
        }
        #[cfg(feature = "pd-profiles")]
        if *(&raw const PAGE) == CABLE_PAGE {
            let section = &mut *(&raw mut PD_CABLE_SECTION);
            if *section + 1 < (&*(&raw const PD_CABLE)).sections() {
                *section += 1;
                return;
            }
            *section = 0;
        }
        #[cfg(feature = "pd-charger")]
        if *(&raw const PAGE) == CHARGER_PAGE {
            if !*(&raw const CHARGER_OFFER) && (&*(&raw const CHARGER)).has_offer() {
                *(&raw mut CHARGER_OFFER) = true;
                return;
            }
            *(&raw mut CHARGER_OFFER) = false;
        }
        *(&raw mut PAGE) = (*(&raw const PAGE) + 1) % PAGE_COUNT;
    }
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_double_press() -> u32 {
    unsafe {
        let page = *(&raw const PAGE);
        if cfg!(feature = "pd-profiles") {
            match page {
                PROFILES_PAGE => return 1,
                EPR_PAGE => return 2,
                SNIFF_PAGE => return 3,
                CABLE_PAGE => return 4,
                #[cfg(feature = "pd-charger")]
                CHARGER_PAGE => return 5,
                _ => {}
            }
        }
        if page >= 2 {
            *(&raw mut GRAPH_WINDOW) = (*(&raw const GRAPH_WINDOW) + 1) % 4;
        } else {
            *(&raw mut PAGE) = (page + 1) % PAGE_COUNT;
        }
    }
    0
}

#[cfg(feature = "pd-charger")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_charger_clear() {
    unsafe {
        *(&raw mut CHARGER) = ChargerCapture::new();
        *(&raw mut CHARGER_OFFER) = false;
    }
}

#[cfg(feature = "pd-charger")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_charger_status(state: u32) {
    unsafe { (&mut *(&raw mut CHARGER)).set_state(state) };
}

#[cfg(feature = "pd-charger")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_charger_vbus(mv: u32) {
    unsafe { (&mut *(&raw mut CHARGER)).set_vbus(mv as u16) };
}

#[cfg(feature = "pd-charger")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_charger_request() -> u32 {
    let scan = unsafe { &*(&raw const CHARGER) };
    u32::from(scan.revision | scan.request_current)
}

#[cfg(feature = "pd-charger")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_charger_frame(frame: *const u8, len: u32, phase: u32) -> u32 {
    if frame.is_null() || !(7..=35).contains(&len) {
        return 6;
    }
    // The native FIFO reader owns the initialized record until this call returns.
    let record = unsafe { core::slice::from_raw_parts(frame, len as usize) };
    unsafe { (&mut *(&raw mut CHARGER)).record(record, phase) }
}

#[cfg(feature = "pd-profiles")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_warning(warning: u32) {
    unsafe {
        let mode = *(&raw const PD_MODE);
        if mode <= CABLE_MODE {
            let bit = 1u32 << mode;
            let warnings = &mut *(&raw mut PD_WARNINGS);
            if warning != 0 {
                *warnings |= bit;
            } else {
                *warnings &= !bit;
            }
        }
    }
}

#[cfg(feature = "pd-profiles")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_status(state: u32, cc: u32, mode: u32) {
    unsafe {
        *(&raw mut PD_STATE) = state;
        *(&raw mut PD_CC) = cc;
        *(&raw mut PD_MODE) = mode;
        #[cfg(feature = "pd-cable-read")]
        if state == 16 && mode == CABLE_MODE {
            *(&raw mut PD_CABLE) = crate::CableCapture::new();
        }
        if state == 1 {
            if mode == 0 {
                *(&raw mut PD_CAPS) = None;
            } else if mode == 1 {
                *(&raw mut PD_EPR_CAPS) = None;
                *(&raw mut PD_EPR_CAPTURE) = crate::EprCapture::new();
            } else if mode == 2 {
                *(&raw mut PD_SNIFF) = crate::PdSniff::new();
                *(&raw mut PD_SNIFF_PRESENT) = true;
                *(&raw mut PD_SNIFF_OFFSET) = 0;
            } else if mode == CABLE_MODE {
                *(&raw mut PD_CABLE) = crate::CableCapture::new();
                *(&raw mut PD_CABLE_SECTION) = 0;
            }
        }
    }
}

#[cfg(feature = "pd-cable-read")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_cable_cc(cc1: u32, cc2: u32) {
    unsafe { (&mut *(&raw mut PD_CABLE)).set_cc_probe(cc1, cc2) };
}

#[cfg(feature = "pd-cable-read")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_cable_reply(
    query: u32,
    index: u32,
    frame: *const u8,
    length: u32,
) -> i32 {
    if frame.is_null() || !(7..=35).contains(&length) {
        return -1;
    }
    // The native reader supplies a complete record from its live 35-byte buffer.
    let bytes = unsafe { core::slice::from_raw_parts(frame, length as usize) };
    match unsafe { (&mut *(&raw mut PD_CABLE)).record_query_reply(query, index, bytes) } {
        Ok(result) => result as i32,
        Err(_) => -1,
    }
}

#[cfg(feature = "pd-cable-read")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_cable_result(query: u32, index: u32, result: u32) {
    unsafe { (&mut *(&raw mut PD_CABLE)).record_query_result(query, index, result) };
}

#[cfg(feature = "pd-cable-read")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_cable_parameters() -> u32 {
    unsafe { (&*(&raw const PD_CABLE)).query_parameters() }
}

#[cfg(feature = "pd-cable-read")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_cable_svid(index: u32) -> u32 {
    u32::from(unsafe { (&*(&raw const PD_CABLE)).query_svid(index as usize) }.unwrap_or(0))
}

#[cfg(feature = "pd-profiles")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_record(frame: *const u8, length: u32, mode: u32, now: u32) -> i32 {
    // The board supplies a complete record from its live 35-byte buffer.
    if !(7..=35).contains(&length) {
        return -1;
    }
    let bytes = unsafe { core::slice::from_raw_parts(frame, length as usize) };
    if mode == CABLE_MODE {
        return match unsafe { (&mut *(&raw mut PD_CABLE)).push(bytes) } {
            Ok(captured) => i32::from(captured),
            Err(_) => -1,
        };
    }
    if mode == 2 {
        return if unsafe { (&mut *(&raw mut PD_SNIFF)).push(bytes) }.is_ok() {
            0
        } else {
            -1
        };
    }
    if mode == 1 {
        return match unsafe { (&mut *(&raw mut PD_EPR_CAPTURE)).push(bytes, now) } {
            Ok(crate::EprEvent::Started) => {
                unsafe { *(&raw mut PD_EPR_CAPS) = None };
                3
            }
            Ok(crate::EprEvent::Partial(caps)) => {
                unsafe { *(&raw mut PD_EPR_CAPS) = Some(caps) };
                2
            }
            Ok(crate::EprEvent::Complete(caps)) => {
                unsafe { *(&raw mut PD_EPR_CAPS) = Some(caps) };
                1
            }
            Ok(crate::EprEvent::Ignored) => 0,
            Err(_) => -1,
        };
    }
    match crate::SourceCaps::from_fifo(bytes) {
        Ok(Some(caps)) => {
            unsafe { *(&raw mut PD_CAPS) = Some(caps) };
            1
        }
        Ok(None) => 0,
        Err(_) => -1,
    }
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_snapshot() {
    unsafe {
        let view = &mut *(&raw mut VIEW);
        #[cfg(feature = "pd-rd-test")]
        view.set_rd_test((meter_on_rd_test_page() != 0).then_some(*(&raw const RD_TEST)));
        #[cfg(feature = "pd-profiles")]
        view.set_pd_profiles(
            *(&raw const PD_STATE),
            *(&raw const PD_CC),
            *(&raw const PD_CAPS),
        );
        #[cfg(feature = "pd-profiles")]
        view.set_epr_profiles(*(&raw const PD_MODE), *(&raw const PD_EPR_CAPS));
        #[cfg(feature = "pd-profiles")]
        view.set_pd_warnings(*(&raw const PD_WARNINGS));
        #[cfg(feature = "pd-profiles")]
        view.set_epr_counters((&*(&raw const PD_EPR_CAPTURE)).counters());
        #[cfg(feature = "pd-profiles")]
        view.set_pd_sniff(
            if *(&raw const PD_SNIFF_PRESENT) {
                Some(&*(&raw const PD_SNIFF))
            } else {
                None
            },
            *(&raw const PD_SNIFF_OFFSET),
        );
        #[cfg(feature = "pd-profiles")]
        view.set_pd_cable(&*(&raw const PD_CABLE), *(&raw const PD_CABLE_SECTION));
        #[cfg(feature = "pd-charger")]
        view.set_charger(*(&raw const CHARGER), *(&raw const CHARGER_OFFER));
        view.set_page(*(&raw const PAGE));
        view.set_graph_window(*(&raw const GRAPH_WINDOW));
        (&*(&raw const METER)).overview(view);
        if *(&raw const UNCALIBRATED) {
            view.set_uncalibrated();
        }
        if *(&raw const PD_PRESENT) {
            let registers = if *(&raw const PD_VALID) {
                Some((*(&raw const PD_FIRST), *(&raw const PD_LAST)))
            } else {
                None
            };
            view.set_pd_diagnostic(registers);
        }
    };
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_demo_render(top: u32, pixels: *mut [u16; SIDE * STRIP_ROWS]) {
    unsafe { render_demo_strip(&*(&raw const VIEW), top as usize, &mut *pixels) };
}
