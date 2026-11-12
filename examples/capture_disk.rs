// SPDX-License-Identifier: GPL-2.0-or-later
//! Offline integration fixture, NOT a device capture or a flash utility.
//! cargo run --example capture_disk -- OUTPUT_DIR NAME=preview.ppm [...]
use std::{
    env, fs,
    io::{self, Write},
    path::PathBuf,
};
use t3p_standalone::capture::disk::{BLOCKS, Disk};
use t3p_standalone::capture::{self, Error, SLOT_BYTES, STRIP_PIXELS, Storage, Writer};
struct FileNor(Vec<u8>);
impl Storage for FileNor {
    fn len(&self) -> u32 {
        self.0.len() as u32
    }
    fn read(&self, offset: u32, bytes: &mut [u8]) -> Result<(), Error> {
        bytes.copy_from_slice(
            self.0
                .get(offset as usize..offset as usize + bytes.len())
                .ok_or(Error::Bounds)?,
        );
        Ok(())
    }
    fn program(&mut self, offset: u32, bytes: &[u8]) -> Result<(), Error> {
        let dest = self
            .0
            .get_mut(offset as usize..offset as usize + bytes.len())
            .ok_or(Error::Bounds)?;
        for (d, b) in dest.iter_mut().zip(bytes) {
            if *d & b != *b {
                return Err(Error::Invalid);
            }
            *d = *b;
        }
        Ok(())
    }
}
fn checked<T>(r: Result<T, Error>) -> io::Result<T> {
    r.map_err(|e| io::Error::other(format!("capture: {e:?}")))
}
fn main() -> io::Result<()> {
    let mut args = env::args_os().skip(1);
    let out = PathBuf::from(
        args.next()
            .ok_or_else(|| io::Error::other("expected output directory"))?,
    );
    let inputs: Vec<_> = args.collect();
    if inputs.is_empty() || inputs.len() > capture::MAX_FILES {
        return Err(io::Error::other("expected 1..8 NAME=PPM inputs"));
    }
    let mut nor = FileNor(vec![255; SLOT_BYTES as usize * inputs.len()]);
    for input in inputs {
        let input = input
            .to_str()
            .ok_or_else(|| io::Error::other("invalid argument"))?;
        let (name, path) = input
            .split_once('=')
            .ok_or_else(|| io::Error::other("expected NAME=PPM"))?;
        let name: [u8; 4] = name
            .as_bytes()
            .try_into()
            .map_err(|_| io::Error::other("name must be four uppercase ASCII letters"))?;
        let ppm = fs::read(path)?;
        let header = b"P6\n240 240\n255\n";
        if !ppm.starts_with(header) || ppm.len() != header.len() + 240 * 240 * 3 {
            return Err(io::Error::other("expected preview's 240x240 binary PPM"));
        }
        let mut writer = checked(Writer::begin(&mut nor, name))?;
        let mut strip = [0; STRIP_PIXELS];
        for (i, rgb) in ppm[header.len()..]
            .chunks_exact(STRIP_PIXELS * 3)
            .enumerate()
        {
            for (p, c) in strip.iter_mut().zip(rgb.chunks_exact(3)) {
                *p = ((c[0] as u16 >> 3) << 11) | ((c[1] as u16 >> 2) << 5) | (c[2] as u16 >> 3);
            }
            checked(writer.strip(i * 8, &strip))?;
        }
        checked(writer.finish())?;
    }
    fs::create_dir_all(&out)?;
    fs::write(out.join("OFFLINE-capture-store.bin"), &nor.0)?;
    let mut disk = checked(Disk::mount(&nor))?;
    let mut image = fs::File::create(out.join("OFFLINE-capture-fat16.img"))?;
    let mut block = [0; 512];
    for lba in 0..BLOCKS {
        checked(disk.read(lba, &mut block))?;
        image.write_all(&block)?;
    }
    println!(
        "OFFLINE fixture: {} captures, {} disk bytes. No USB/device access.",
        disk.count(),
        BLOCKS * 512
    );
    Ok(())
}
