// SPDX-License-Identifier: GPL-2.0-or-later
use crate::{Meter, Overview, SIDE, STRIP_ROWS, demo_reading, render_demo_strip};

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
    unsafe { *(&raw mut PAGE) = (*(&raw const PAGE) + 1) % 5 };
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_snapshot() {
    unsafe {
        let view = &mut *(&raw mut VIEW);
        view.set_page(*(&raw const PAGE));
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
