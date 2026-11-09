// SPDX-License-Identifier: GPL-2.0-or-later
use super::{AMPS, GRID, MUTED, Strip, TEXT, VOLTS, WATTS, card, value};
use crate::RdTest;

pub(super) fn render(p: &mut Strip<'_>, test: RdTest) {
    let ack_test = cfg!(feature = "pd-ack-test");
    let title: &[u8] = if ack_test { b"PD ACK TEST" } else { b"RD TEST" };
    p.text(8, 8, title, 2, AMPS);
    p.text(159, 10, b"CC1 / TYPE-C", 1, MUTED);
    p.rect(8, 28, 224, 1, AMPS);
    // State codes from native/t3p_rd_test.h.
    let (status, hint, color): (&[u8], &[u8], u16) = match test.state {
        0 => (b"READY", b"HID ONLY. UNPLUG SRC AND LOAD.", AMPS),
        1 => (b"RD OFF", b"CONNECT SRC. WAIT 2S.", AMPS),
        2 if ack_test => (b"RD ON", b"WAITING FOR 5V AND SRC CAPS", AMPS),
        2 => (b"RD ON", b"WAITING FOR 5V", AMPS),
        3 if ack_test => (b"PD ACK SEEN", b"DEFAULT CONFIG RESTORED", WATTS),
        3 => (b"5V SEEN", b"DEFAULT RD RESTORED", WATTS),
        4 => (b"NO 5V", b"DEFAULT RD RESTORED", VOLTS),
        5 => (b"TIMED OUT", b"DEFAULT RD RESTORED", VOLTS),
        6 => (b"CANCELLED", b"DEFAULT RD RESTORED", MUTED),
        7 => (b"VBUS PRESENT", b"UNPLUG SRC AND RETRY", VOLTS),
        8 => (b"CONFIG CHECK", b"CHECK PD CONTROLLER", VOLTS),
        9 => (b"BUS ERROR", b"CHECK CONNECTION AND RETRY", VOLTS),
        10 => (b"RESTORE ERROR", b"UNPLUG SRC. REPOWER T3P.", VOLTS),
        12 => (b"NO SRC CAPS", b"DEFAULT CONFIG RESTORED", VOLTS),
        13 => (b"NO GOODCRC", b"DEFAULT CONFIG RESTORED", VOLTS),
        14 => (b"PD RX ERROR", b"DEFAULT CONFIG RESTORED", VOLTS),
        15 => (b"TIMER ERROR", b"REPOWER T3P", VOLTS),
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
    #[cfg(feature = "pd-ack-test")]
    {
        let count = if test.pdos == 0 {
            b'-'
        } else {
            b'0' + test.pdos.min(7)
        };
        p.text(8, 208, b"SRC PDO:", 1, MUTED);
        p.text(53, 208, &[count], 1, TEXT);
        p.text(112, 208, b"GOODCRC:", 1, MUTED);
        let ack: &[u8] = if test.goodcrc { b"SEEN" } else { b"---" };
        p.text(157, 208, ack, 1, TEXT);
    }
    p.rect(8, 219, 224, 1, GRID);
    let controls: &[u8] = match test.state {
        0 => b"TAP EXIT   2X ARM",
        1 => b"TAP RD ON   HOLD CANCEL",
        2 => b"HOLD CANCEL",
        10 | 15 => b"HOLD EXIT",
        _ => b"2X RETRY   HOLD EXIT",
    };
    p.text(8, 226, controls, 1, MUTED);
}
