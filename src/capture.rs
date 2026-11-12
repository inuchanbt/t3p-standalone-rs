// SPDX-License-Identifier: GPL-2.0-or-later
//! Hardware-independent capture journal. No device address or erase operation.
//! A board backend must establish ownership and physical bounds before use.
//! Feed strips from ONE frozen render snapshot, in top-to-bottom order.

pub mod disk;

pub const WIDTH: usize = 240;
pub const HEIGHT: usize = 240;
pub const STRIP_PIXELS: usize = WIDTH * 8;
pub const SLOT_BYTES: u32 = 131_072;
pub const MAX_FILES: usize = 8;
const HEADER: u32 = 64;
const MAGIC: &[u8; 8] = b"T3PSCR01";
const COMMIT: &[u8; 4] = b"DONE";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Io,
    Bounds,
    Full,
    Invalid,
    Corrupt,
    Sequence,
}

/// Offsets are relative to an independently verified capture partition.
/// program must be synchronous, preserve bytes outside the requested range,
/// and reject any 0-to-1 bit transition. Erasing is intentionally not exposed.
pub trait Storage {
    fn len(&self) -> u32;
    fn is_empty(&self) -> bool {
        self.len() == 0
    }
    fn read(&self, offset: u32, bytes: &mut [u8]) -> Result<(), Error>;
    fn program(&mut self, offset: u32, bytes: &[u8]) -> Result<(), Error>;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Record {
    pub(crate) slot: u32,
    pub sequence: u16,
    pub name: [u8; 4],
    pub(crate) size: u32,
    pub(crate) crc: u32,
}

fn crc_byte(mut crc: u32, value: u8) -> u32 {
    crc ^= value as u32;
    for _ in 0..8 {
        crc = (crc >> 1) ^ (0xedb8_8320 & 0u32.wrapping_sub(crc & 1));
    }
    crc
}
fn crc(bytes: &[u8]) -> u32 {
    !bytes.iter().fold(!0, |c, b| crc_byte(c, *b))
}
fn u32le(bytes: &[u8]) -> u32 {
    u32::from_le_bytes(bytes[..4].try_into().unwrap())
}
fn slots<S: Storage>(s: &S) -> Result<usize, Error> {
    let n = s.len() / SLOT_BYTES;
    if s.len() % SLOT_BYTES != 0 || n == 0 || n > MAX_FILES as u32 {
        return Err(Error::Bounds);
    }
    Ok(n as usize)
}
fn blank<S: Storage>(s: &S, slot: u32) -> Result<bool, Error> {
    let mut bytes = [0; 256];
    for offset in (0..SLOT_BYTES).step_by(bytes.len()) {
        s.read(slot * SLOT_BYTES + offset, &mut bytes)?;
        if bytes.iter().any(|b| *b != 0xff) {
            return Ok(false);
        }
    }
    Ok(true)
}

/// Invalid/unfinished slots consume capacity but are never overwritten.
/// I/O errors are not mistaken for an empty partition.
pub fn scan<S: Storage>(s: &S) -> Result<[Option<Record>; MAX_FILES], Error> {
    let mut records = [None; MAX_FILES];
    for slot in 0..slots(s)? {
        let mut h = [0; HEADER as usize];
        s.read(slot as u32 * SLOT_BYTES, &mut h)?;
        if &h[..8] != MAGIC || &h[60..] != COMMIT {
            continue;
        }
        if crc(&h[..40]) != u32le(&h[40..]) || h[14..32] != [0; 18] || h[44..60] != [0xff; 16] {
            continue;
        }
        let sequence = u16::from_le_bytes([h[8], h[9]]);
        let name: [u8; 4] = h[10..14].try_into().unwrap();
        let size = u32le(&h[32..]);
        if sequence == 0
            || sequence > 9999
            || !name.iter().all(u8::is_ascii_uppercase)
            || size == 0
            || size > SLOT_BYTES - HEADER
        {
            continue;
        }
        let r = Record {
            slot: slot as u32,
            sequence,
            name,
            size,
            crc: u32le(&h[36..]),
        };
        match verify(s, r) {
            Ok(()) => {}
            Err(Error::Io) => return Err(Error::Io),
            Err(_) => continue,
        }
        if records
            .iter()
            .flatten()
            .any(|r: &Record| r.sequence == sequence)
        {
            return Err(Error::Sequence);
        }
        records[slot] = Some(r);
    }
    Ok(records)
}

pub struct Writer<'a, S> {
    storage: &'a mut S,
    record: Record,
    pixels: usize,
    crc: u32,
    poisoned: bool,
}
impl<'a, S: Storage> Writer<'a, S> {
    pub fn begin(s: &'a mut S, name: [u8; 4]) -> Result<Self, Error> {
        if !name.iter().all(u8::is_ascii_uppercase) {
            return Err(Error::Invalid);
        }
        let records = scan(s)?;
        let sequence = records
            .iter()
            .flatten()
            .map(|r| r.sequence)
            .max()
            .unwrap_or(0)
            + 1;
        if sequence > 9999 {
            return Err(Error::Sequence);
        }
        let mut free = None;
        for slot in 0..slots(s)? {
            if blank(s, slot as u32)? {
                free = Some(slot as u32);
                break;
            }
        }
        let slot = free.ok_or(Error::Full)?;
        let mut h = [0; 32];
        h[..8].copy_from_slice(MAGIC);
        h[8..10].copy_from_slice(&sequence.to_le_bytes());
        h[10..14].copy_from_slice(&name);
        s.program(slot * SLOT_BYTES, &h)?;
        Ok(Self {
            storage: s,
            record: Record {
                slot,
                sequence,
                name,
                size: 0,
                crc: 0,
            },
            pixels: 0,
            crc: !0,
            poisoned: false,
        })
    }

    fn append(&mut self, data: &[u8]) -> Result<(), Error> {
        if self.record.size + data.len() as u32 > SLOT_BYTES - HEADER {
            return Err(Error::Bounds);
        }
        self.storage.program(
            self.record.slot * SLOT_BYTES + HEADER + self.record.size,
            data,
        )?;
        for b in data {
            self.crc = crc_byte(self.crc, *b);
        }
        self.record.size += data.len() as u32;
        Ok(())
    }

    /// Bounded work per strip; caller may service measurement/PD between strips.
    /// A failed/misordered strip poisons this record until it is abandoned.
    pub fn strip(&mut self, top: usize, pixels: &[u16]) -> Result<(), Error> {
        if self.poisoned {
            return Err(Error::Invalid);
        }
        self.poisoned = true;
        if pixels.len() != STRIP_PIXELS || top >= HEIGHT || top * WIDTH != self.pixels {
            return Err(Error::Invalid);
        }
        let mut pos = 0;
        let mut buf = [0; 258];
        while pos < pixels.len() {
            let mut run = 1;
            while pos + run < pixels.len() && pixels[pos + run] == pixels[pos] {
                run += 1;
            }
            if run >= 3 {
                buf[..2].copy_from_slice(&(0x8000 | run as u16).to_le_bytes());
                buf[2..4].copy_from_slice(&pixels[pos].to_le_bytes());
                self.append(&buf[..4])?;
                pos += run;
            } else {
                let start = pos;
                pos += run;
                while pos < pixels.len() && pos - start < 128 {
                    if pos + 2 < pixels.len()
                        && pixels[pos] == pixels[pos + 1]
                        && pixels[pos] == pixels[pos + 2]
                    {
                        break;
                    }
                    pos += 1;
                }
                let count = pos - start;
                buf[..2].copy_from_slice(&(count as u16).to_le_bytes());
                for (i, p) in pixels[start..pos].iter().enumerate() {
                    buf[2 + i * 2..4 + i * 2].copy_from_slice(&p.to_le_bytes());
                }
                self.append(&buf[..2 + count * 2])?;
            }
        }
        self.pixels += pixels.len();
        self.poisoned = false;
        Ok(())
    }

    pub fn finish(self) -> Result<Record, Error> {
        let s = self.storage;
        if self.poisoned || self.pixels != WIDTH * HEIGHT {
            return Err(Error::Invalid);
        }
        let mut r = self.record;
        r.crc = !self.crc;
        verify(s, r)?; // Read back before making the record visible.
        let mut h = [0; 44];
        s.read(r.slot * SLOT_BYTES, &mut h[..32])?;
        if &h[..8] != MAGIC
            || h[8..10] != r.sequence.to_le_bytes()
            || h[10..14] != r.name
            || h[14..32] != [0; 18]
        {
            return Err(Error::Corrupt);
        }
        h[32..36].copy_from_slice(&r.size.to_le_bytes());
        h[36..40].copy_from_slice(&r.crc.to_le_bytes());
        let sum = crc(&h[..40]);
        h[40..44].copy_from_slice(&sum.to_le_bytes());
        s.program(r.slot * SLOT_BYTES + 32, &h[32..])?;
        let mut check = [0; 44];
        s.read(r.slot * SLOT_BYTES, &mut check)?;
        if check != h {
            return Err(Error::Corrupt);
        }
        s.program(r.slot * SLOT_BYTES + 60, COMMIT)?;
        let mut committed = [0; 4];
        s.read(r.slot * SLOT_BYTES + 60, &mut committed)?;
        if &committed != COMMIT {
            return Err(Error::Corrupt);
        }
        Ok(r)
    }
}

/// Stateful streaming decoder uses a 256-byte read cache, no full frame.
pub struct Decoder {
    r: Record,
    offset: u32,
    crc: u32,
    pixels: usize,
    remaining: usize,
    repeated: bool,
    value: u16,
    cache: [u8; 256],
    cached: usize,
    cursor: usize,
}
impl Decoder {
    pub fn new(r: Record) -> Self {
        Self {
            r,
            offset: 0,
            crc: !0,
            pixels: 0,
            remaining: 0,
            repeated: false,
            value: 0,
            cache: [0; 256],
            cached: 0,
            cursor: 0,
        }
    }
    fn byte<S: Storage>(&mut self, s: &S) -> Result<u8, Error> {
        if self.offset == self.r.size {
            return Err(Error::Corrupt);
        }
        if self.cursor == self.cached {
            self.cached = core::cmp::min(256, (self.r.size - self.offset) as usize);
            s.read(
                self.r.slot * SLOT_BYTES + HEADER + self.offset,
                &mut self.cache[..self.cached],
            )?;
            self.cursor = 0;
        }
        let b = self.cache[self.cursor];
        self.cursor += 1;
        self.offset += 1;
        self.crc = crc_byte(self.crc, b);
        Ok(b)
    }
    fn word<S: Storage>(&mut self, s: &S) -> Result<u16, Error> {
        Ok(u16::from_le_bytes([self.byte(s)?, self.byte(s)?]))
    }
    pub fn pixel<S: Storage>(&mut self, s: &S) -> Result<u16, Error> {
        if self.pixels >= WIDTH * HEIGHT {
            return Err(Error::Bounds);
        }
        if self.remaining == 0 {
            let token = self.word(s)?;
            self.remaining = (token & 0x7fff) as usize;
            self.repeated = token & 0x8000 != 0;
            if self.remaining == 0 || self.remaining > WIDTH * HEIGHT - self.pixels {
                return Err(Error::Corrupt);
            }
            if self.repeated {
                self.value = self.word(s)?;
            }
        }
        let value = if self.repeated {
            self.value
        } else {
            self.word(s)?
        };
        self.remaining -= 1;
        self.pixels += 1;
        Ok(value)
    }
    pub fn end(&self) -> Result<(), Error> {
        if self.pixels != WIDTH * HEIGHT
            || self.remaining != 0
            || self.offset != self.r.size
            || !self.crc != self.r.crc
        {
            Err(Error::Corrupt)
        } else {
            Ok(())
        }
    }
}
fn verify<S: Storage>(s: &S, r: Record) -> Result<(), Error> {
    let mut d = Decoder::new(r);
    for _ in 0..WIDTH * HEIGHT {
        d.pixel(s)?;
    }
    d.end()
}
