// SPDX-License-Identifier: GPL-2.0-or-later
use crate::pd::PROFILES_PAGE;
use crate::{Meter, Overview, SIDE, STRIP_ROWS, demo_reading, render_demo_strip};

const PAGE_COUNT: u32 = PROFILES_PAGE + cfg!(feature = "pd-profiles") as u32;

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
#[cfg(feature = "pd-profiles")]
static mut PD_STATE: u32 = 0;
#[cfg(feature = "pd-profiles")]
static mut PD_CC: u32 = 0;
#[cfg(feature = "pd-profiles")]
static mut PD_CAPS: Option<crate::SourceCaps> = None;

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

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_reset_totals() {
    unsafe { (&mut *(&raw mut METER)).reset_totals() };
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_next_page() {
    unsafe { *(&raw mut PAGE) = (*(&raw const PAGE) + 1) % PAGE_COUNT };
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_double_press() -> u32 {
    unsafe {
        let page = *(&raw const PAGE);
        if cfg!(feature = "pd-profiles") && page == PROFILES_PAGE {
            return 1;
        }
        if page >= 2 {
            *(&raw mut GRAPH_WINDOW) = (*(&raw const GRAPH_WINDOW) + 1) % 4;
        } else {
            *(&raw mut PAGE) = (page + 1) % PAGE_COUNT;
        }
    }
    0
}

#[cfg(feature = "pd-profiles")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_status(state: u32, cc: u32) {
    unsafe {
        *(&raw mut PD_STATE) = state;
        *(&raw mut PD_CC) = cc;
        if state != 2 {
            *(&raw mut PD_CAPS) = None;
        }
    }
}

#[cfg(feature = "pd-profiles")]
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_pd_record(frame: *const u8, length: u32) -> i32 {
    // The board supplies a complete record from its live 35-byte buffer.
    if !(7..=35).contains(&length) {
        return -1;
    }
    match crate::SourceCaps::from_fifo(unsafe {
        core::slice::from_raw_parts(frame, length as usize)
    }) {
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
        #[cfg(feature = "pd-profiles")]
        view.set_pd_profiles(
            *(&raw const PD_STATE),
            *(&raw const PD_CC),
            *(&raw const PD_CAPS),
        );
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
