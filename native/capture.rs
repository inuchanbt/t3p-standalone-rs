// SPDX-License-Identifier: GPL-2.0-or-later
use crate::capture::{Error, Storage, Writer};
use crate::{SIDE, STRIP_ROWS};

unsafe extern "C" {
    fn t3p_capture_store_read(at: u32, out: *mut u8, n: u32) -> i32;
    fn t3p_capture_store_program(at: u32, p: *const u8, n: u32) -> i32;
    fn t3p_capture_store_flush() -> i32;
}
struct NativeStorage;
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_capture_notice(
    words: *const [u32; 4],
    top: u32,
    pixels: *mut [u16; SIDE * STRIP_ROWS],
) {
    let [result, status, jedec, sfdp] = unsafe { *words };
    unsafe {
        crate::screen::capture_status::render(
            result as i32,
            status,
            jedec,
            sfdp,
            top as usize,
            &mut *pixels,
        );
    }
}
impl Storage for NativeStorage {
    fn len(&self) -> u32 {
        0xc0000
    }
    fn read(&self, at: u32, out: &mut [u8]) -> Result<(), Error> {
        if unsafe { t3p_capture_store_read(at, out.as_mut_ptr(), out.len() as u32) } == 0 {
            Ok(())
        } else {
            Err(Error::Io)
        }
    }
    fn program(&mut self, at: u32, p: &[u8]) -> Result<(), Error> {
        if unsafe { t3p_capture_store_program(at, p.as_ptr(), p.len() as u32) } == 0 {
            Ok(())
        } else {
            Err(Error::Io)
        }
    }
}
/// The board calls this between frames with protocol scans idle. The VIEW
/// remains the exact last displayed snapshot throughout all thirty strips.
#[unsafe(no_mangle)]
unsafe extern "C" fn meter_capture_save(pixels: *mut [u16; SIDE * STRIP_ROWS]) -> i32 {
    let save = || -> Result<u16, Error> {
        let mut s = NativeStorage;
        let mut writer = Writer::begin(&mut s, *b"SCRN")?;
        for top in (0..SIDE).step_by(STRIP_ROWS) {
            unsafe {
                super::demo::meter_demo_render(top as u32, pixels);
            }
            writer.strip(top, unsafe { &*pixels })?;
            if unsafe { t3p_capture_store_flush() } != 0 {
                return Err(Error::Io);
            }
        }
        Ok(writer.finish()?.sequence)
    };
    match save() {
        Ok(n) => i32::from(n),
        Err(Error::Full) => -2,
        Err(Error::Io) => -1,
        Err(Error::Bounds) => -4,
        Err(Error::Invalid) => -5,
        Err(Error::Corrupt) => -6,
        Err(Error::Sequence) => -7,
    }
}
