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

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_demo_sample(tick: u32) {
    unsafe { (&mut *(&raw mut METER)).sample(tick, demo_reading(tick)) };
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_reset_totals() {
    unsafe { (&mut *(&raw mut METER)).reset_totals() };
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_snapshot() {
    unsafe { (&*(&raw const METER)).overview(&mut *(&raw mut VIEW)) };
}

#[unsafe(no_mangle)]
unsafe extern "C" fn meter_demo_render(top: u32, pixels: *mut [u16; SIDE * STRIP_ROWS]) {
    unsafe { render_demo_strip(&*(&raw const VIEW), top as usize, &mut *pixels) };
}
