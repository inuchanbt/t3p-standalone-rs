// SPDX-License-Identifier: GPL-2.0-or-later
use super::{AMPS, GRID, MUTED, Strip, TEXT, VOLTS, WATTS, card, value};
use crate::RdTest;

pub(super) fn render(p: &mut Strip<'_>, test: RdTest) {
    p.text(8, 8, b"RD TEST", 2, AMPS);
    p.text(159, 10, b"CC1 / TYPE-C", 1, MUTED);
    p.rect(8, 28, 224, 1, AMPS);
    // State codes from native/t3p_rd_test.h.
    let (status, hint, color): (&[u8], &[u8], u16) = match test.state {
        0 => (b"READY", b"HID ONLY. UNPLUG SRC AND LOAD.", AMPS),
        1 => (b"RD OFF", b"CONNECT SRC. WAIT 2S.", AMPS),
        2 => (b"RD ON", b"WAITING FOR 5V", AMPS),
        3 => (b"5V SEEN", b"DEFAULT RD RESTORED", WATTS),
        4 => (b"NO 5V", b"DEFAULT RD RESTORED", VOLTS),
        5 => (b"TIMED OUT", b"DEFAULT RD RESTORED", VOLTS),
        6 => (b"CANCELLED", b"DEFAULT RD RESTORED", MUTED),
        7 => (b"VBUS PRESENT", b"UNPLUG SRC AND RETRY", VOLTS),
        8 => (b"CONFIG CHECK", b"CHECK PD CONTROLLER", VOLTS),
        9 => (b"BUS ERROR", b"CHECK CONNECTION AND RETRY", VOLTS),
        10 => (b"RESTORE ERROR", b"UNPLUG SRC. REPOWER T3P.", VOLTS),
        _ => (b"VBUS HIGH", b"UNPLUG SRC", VOLTS),
    };
    p.text(8, 38, status, 2, color);
    p.text(8, 58, hint, 1, TEXT);
    for (top, label, mv, color) in [
        (76, b"VBUS / RD OFF / SW0 04".as_slice(), test.off_mv, AMPS),
        (145, b"VBUS / RD ON  / SW0 05".as_slice(), test.on_mv, WATTS),
    ] {
        card(p, 8, top, 224, 59, label, color);
        value(
            p,
            14,
            top + 22,
            208,
            (mv != u16::MAX).then_some((u64::from(mv), false)),
            b"V",
            color,
            3,
        );
    }
    p.rect(8, 219, 224, 1, GRID);
    let controls: &[u8] = match test.state {
        0 => b"TAP EXIT   2X ARM",
        1 => b"TAP RD ON   HOLD CANCEL",
        2 => b"HOLD CANCEL",
        10 => b"HOLD EXIT",
        _ => b"2X RETRY   HOLD EXIT",
    };
    p.text(8, 226, controls, 1, MUTED);
}
