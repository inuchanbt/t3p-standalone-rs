// SPDX-License-Identifier: GPL-2.0-or-later
//! Read-only FAT16 sector generator, independent of USB transport.
//! Owns an immutable borrow of storage for the entire export session.
use super::{Decoder, Error, HEIGHT, MAX_FILES, Record, Storage, WIDTH, scan};

pub const BLOCK_BYTES: usize = 512;
pub const BLOCKS: u32 = 8192;
pub const BMP_BYTES: u32 = 66 + (WIDTH * HEIGHT * 2) as u32;
const FILE_BLOCKS: u32 = BMP_BYTES.div_ceil(BLOCK_BYTES as u32);
const FAT_BLOCKS: u32 = 32;
const ROOT: u32 = 1 + FAT_BLOCKS * 2;
const DATA: u32 = ROOT + 2;

pub struct Disk<'a, S> {
    storage: &'a S,
    files: [Option<Record>; MAX_FILES],
    decoder: Option<(usize, Decoder)>,
}
fn put16(out: &mut [u8], at: usize, n: u16) {
    out[at..at + 2].copy_from_slice(&n.to_le_bytes());
}
fn put32(out: &mut [u8], at: usize, n: u32) {
    out[at..at + 4].copy_from_slice(&n.to_le_bytes());
}

impl<'a, S: Storage> Disk<'a, S> {
    pub fn mount(storage: &'a S) -> Result<Self, Error> {
        let records = scan(storage)?;
        let mut files = [None; MAX_FILES];
        for (i, r) in records.iter().flatten().enumerate() {
            files[i] = Some(*r);
        }
        files.sort_unstable_by_key(|r| r.map(|r| r.sequence).unwrap_or(u16::MAX));
        Ok(Self {
            storage,
            files,
            decoder: None,
        })
    }
    pub fn count(&self) -> usize {
        self.files.iter().flatten().count()
    }
    pub fn read(&mut self, lba: u32, out: &mut [u8; BLOCK_BYTES]) -> Result<(), Error> {
        if lba >= BLOCKS {
            return Err(Error::Bounds);
        }
        out.fill(0);
        if lba == 0 {
            out[..3].copy_from_slice(&[0xeb, 0x3c, 0x90]);
            out[3..11].copy_from_slice(b"T3PCAP01");
            put16(out, 11, 512);
            out[13] = 1;
            put16(out, 14, 1);
            out[16] = 2;
            put16(out, 17, 32);
            put16(out, 19, BLOCKS as u16);
            out[21] = 0xf8;
            put16(out, 22, FAT_BLOCKS as u16);
            put16(out, 24, 32);
            put16(out, 26, 64);
            out[36] = 0x80;
            out[38] = 0x29;
            put32(out, 39, 0x54334350);
            out[43..54].copy_from_slice(b"T3P CAPTURE");
            out[54..62].copy_from_slice(b"FAT16   ");
            out[510..].copy_from_slice(&[0x55, 0xaa]);
        } else if lba < ROOT {
            let fat_sector = (lba - 1) % FAT_BLOCKS;
            for i in 0..256 {
                let cluster = fat_sector * 256 + i as u32;
                let value = if cluster == 0 {
                    0xfff8
                } else if cluster == 1 {
                    0xffff
                } else {
                    let file = ((cluster - 2) / FILE_BLOCKS) as usize;
                    if file < self.count() {
                        if (cluster - 1) % FILE_BLOCKS == 0 {
                            0xffff
                        } else {
                            (cluster + 1) as u16
                        }
                    } else {
                        0
                    }
                };
                put16(out, i * 2, value);
            }
        } else if lba < DATA {
            if lba == ROOT {
                out[..11].copy_from_slice(b"T3P CAPTURE");
                out[11] = 8;
                for (i, r) in self.files.iter().flatten().enumerate() {
                    let e = &mut out[(i + 1) * 32..(i + 2) * 32];
                    e[..4].copy_from_slice(&r.name);
                    let mut seq = r.sequence;
                    for j in (4..8).rev() {
                        e[j] = b'0' + (seq % 10) as u8;
                        seq /= 10;
                    }
                    e[8..11].copy_from_slice(b"BMP");
                    e[11] = 0x21;
                    put16(e, 16, 0x21);
                    put16(e, 18, 0x21);
                    put16(e, 24, 0x21);
                    put16(e, 26, (2 + i as u32 * FILE_BLOCKS) as u16);
                    put32(e, 28, BMP_BYTES);
                }
            }
        } else {
            let block = lba - DATA;
            let file = (block / FILE_BLOCKS) as usize;
            if file < self.count() {
                self.bmp(file, block % FILE_BLOCKS * BLOCK_BYTES as u32, out)?;
            }
        }
        Ok(())
    }
    fn bmp(&mut self, file: usize, offset: u32, out: &mut [u8; BLOCK_BYTES]) -> Result<(), Error> {
        let r = self.files[file].ok_or(Error::Invalid)?;
        let mut h = [0; 66];
        h[..2].copy_from_slice(b"BM");
        put32(&mut h, 2, BMP_BYTES);
        put32(&mut h, 10, 66);
        put32(&mut h, 14, 40);
        put32(&mut h, 18, WIDTH as u32);
        put32(&mut h, 22, (-(HEIGHT as i32)) as u32); // top-down, matching the LCD
        put16(&mut h, 26, 1);
        put16(&mut h, 28, 16);
        put32(&mut h, 30, 3);
        put32(&mut h, 34, BMP_BYTES - 66);
        put32(&mut h, 54, 0xf800);
        put32(&mut h, 58, 0x07e0);
        put32(&mut h, 62, 0x001f);
        let start = offset.max(66);
        let end = (offset + BLOCK_BYTES as u32).min(BMP_BYTES);
        if offset < 66 {
            out[..66].copy_from_slice(&h);
        }
        if start >= end {
            return Ok(());
        }
        let first_pixel = ((start - 66) / 2) as usize;
        if self
            .decoder
            .as_ref()
            .is_none_or(|(i, d)| *i != file || d.pixels > first_pixel)
        {
            self.decoder = Some((file, Decoder::new(r)));
        }
        let (_, d) = self.decoder.as_mut().ok_or(Error::Invalid)?;
        while d.pixels < first_pixel {
            d.pixel(self.storage)?;
        }
        for at in (start..end).step_by(2) {
            let p = d.pixel(self.storage)?;
            put16(out, (at - offset) as usize, p);
        }
        if end == BMP_BYTES {
            d.end()?;
        }
        Ok(())
    }
}
