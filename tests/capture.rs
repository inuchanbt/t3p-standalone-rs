use t3p_standalone::capture::disk::{BLOCKS, BMP_BYTES, Disk};
use t3p_standalone::capture::{self, Decoder, Error, SLOT_BYTES, STRIP_PIXELS, Storage, Writer};

#[derive(Clone)]
struct Nor {
    bytes: Vec<u8>,
    budget: usize,
    read_error: bool,
}
impl Nor {
    fn new(slots: usize) -> Self {
        Self {
            bytes: vec![255; SLOT_BYTES as usize * slots],
            budget: usize::MAX,
            read_error: false,
        }
    }
}
impl Storage for Nor {
    fn len(&self) -> u32 {
        self.bytes.len() as u32
    }
    fn read(&self, offset: u32, bytes: &mut [u8]) -> Result<(), Error> {
        if self.read_error {
            return Err(Error::Io);
        }
        bytes.copy_from_slice(
            self.bytes
                .get(offset as usize..offset as usize + bytes.len())
                .ok_or(Error::Bounds)?,
        );
        Ok(())
    }
    fn program(&mut self, offset: u32, bytes: &[u8]) -> Result<(), Error> {
        let dst = self
            .bytes
            .get_mut(offset as usize..offset as usize + bytes.len())
            .ok_or(Error::Bounds)?;
        for (to, from) in dst.iter_mut().zip(bytes) {
            if self.budget == 0 {
                return Err(Error::Io);
            }
            self.budget -= 1;
            if *to & from != *from {
                return Err(Error::Invalid);
            }
            *to = *from;
        }
        Ok(())
    }
}
fn save(s: &mut Nor, seed: u16) -> Result<capture::Record, Error> {
    let mut w = Writer::begin(s, *b"SYST")?;
    for top in (0..240).step_by(8) {
        w.strip(top, &[seed; STRIP_PIXELS])?;
    }
    w.finish()
}

#[test]
fn lossless_high_entropy_and_boundaries() {
    let mut s = Nor::new(2);
    let mut w = Writer::begin(&mut s, *b"TEST").unwrap();
    let mut all = Vec::new();
    let mut random = 1u32;
    for top in (0..240).step_by(8) {
        let mut strip = [0; STRIP_PIXELS];
        for p in &mut strip {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            *p = random as u16;
        }
        strip[0..3].fill(0);
        strip[127..130].fill(65535);
        strip[1918..1920].fill(123);
        all.extend_from_slice(&strip);
        w.strip(top, &strip).unwrap();
    }
    let r = w.finish().unwrap();
    let mut d = Decoder::new(r);
    for p in all {
        assert_eq!(d.pixel(&s).unwrap(), p);
    }
    d.end().unwrap();
    assert_eq!(capture::scan(&s).unwrap().iter().flatten().count(), 1);
}

#[test]
fn every_byte_power_cut_preserves_earlier_capture() {
    let mut baseline = Nor::new(3);
    save(&mut baseline, 0x07e0).unwrap();
    let old = baseline.bytes[..SLOT_BYTES as usize].to_vec();
    // Solid image: header32 + thirty 4-byte runs + final metadata12 + commit4.
    for cut in 0..168 {
        let mut s = baseline.clone();
        s.budget = cut;
        assert!(save(&mut s, 0xf800).is_err());
        assert_eq!(&s.bytes[..SLOT_BYTES as usize], &old);
        assert_eq!(capture::scan(&s).unwrap().iter().flatten().count(), 1);
        s.budget = usize::MAX;
        save(&mut s, 0x001f).unwrap();
        assert_eq!(capture::scan(&s).unwrap().iter().flatten().count(), 2);
    }
}

#[test]
fn invalid_storage_and_unknown_bytes_are_never_reclaimed() {
    assert_eq!(capture::scan(&Nor::new(0)), Err(Error::Bounds));
    assert_eq!(capture::scan(&Nor::new(9)), Err(Error::Bounds));
    let mut s = Nor::new(1);
    s.bytes[8000] = 7;
    let before = s.bytes.clone();
    assert!(matches!(Writer::begin(&mut s, *b"SYST"), Err(Error::Full)));
    assert_eq!(s.bytes, before);
    s.read_error = true;
    assert_eq!(capture::scan(&s), Err(Error::Io));
}

#[test]
fn corrupt_and_incomplete_images_are_not_exported() {
    let mut s = Nor::new(2);
    save(&mut s, 0).unwrap();
    for at in [8, 10, 14, 32, 36, 40, 60, 64, 66, 183] {
        let mut damaged = s.clone();
        damaged.bytes[at] ^= 1;
        assert_eq!(Disk::mount(&damaged).unwrap().count(), 0, "offset {at}");
    }
    let mut w = Writer::begin(&mut s, *b"PART").unwrap();
    w.strip(0, &[0; STRIP_PIXELS]).unwrap();
    assert_eq!(w.finish(), Err(Error::Invalid));
    assert_eq!(Disk::mount(&s).unwrap().count(), 1);
}

#[test]
fn bad_strip_order_poisoning_and_full() {
    let mut s = Nor::new(1);
    let mut w = Writer::begin(&mut s, *b"TEST").unwrap();
    assert_eq!(w.strip(usize::MAX, &[0; STRIP_PIXELS]), Err(Error::Invalid));
    assert_eq!(w.strip(0, &[0; STRIP_PIXELS]), Err(Error::Invalid));
    assert_eq!(w.finish(), Err(Error::Invalid));
    assert_eq!(save(&mut s, 0), Err(Error::Full));
}

#[test]
fn readback_detects_silent_program_corruption_before_commit() {
    struct BadNor(Nor);
    impl Storage for BadNor {
        fn len(&self) -> u32 {
            self.0.len()
        }
        fn read(&self, offset: u32, bytes: &mut [u8]) -> Result<(), Error> {
            self.0.read(offset, bytes)
        }
        fn program(&mut self, offset: u32, bytes: &[u8]) -> Result<(), Error> {
            self.0.program(offset, bytes)?;
            if offset == 64 {
                self.0.bytes[66] ^= 1;
            }
            Ok(())
        }
    }
    let mut s = BadNor(Nor::new(1));
    let mut w = Writer::begin(&mut s, *b"TEST").unwrap();
    for top in (0..240).step_by(8) {
        w.strip(top, &[0; STRIP_PIXELS]).unwrap();
    }
    assert_eq!(w.finish(), Err(Error::Corrupt));
    assert_eq!(&s.0.bytes[60..64], &[255; 4]);
    assert_eq!(Disk::mount(&s).unwrap().count(), 0);
}

#[test]
fn fat_mirrors_filenames_chains_random_sectors_and_bmp() {
    let mut s = Nor::new(2);
    save(&mut s, 0xf800).unwrap();
    save(&mut s, 0x07e0).unwrap();
    let mut disk = Disk::mount(&s).unwrap();
    let mut a = [0; 512];
    let mut b = [0; 512];
    disk.read(0, &mut a).unwrap();
    assert_eq!(&a[510..], &[85, 170]);
    for sector in 1..=32 {
        disk.read(sector, &mut a).unwrap();
        disk.read(sector + 32, &mut b).unwrap();
        assert_eq!(a, b);
    }
    disk.read(65, &mut a).unwrap();
    assert_eq!(&a[32..43], b"SYST0001BMP");
    assert_eq!(&a[64..75], b"SYST0002BMP");
    assert_eq!(u32::from_le_bytes(a[60..64].try_into().unwrap()), BMP_BYTES);
    for (file, value) in [(0, 0xf800u16), (1, 0x07e0)] {
        let mut image = vec![0; 226 * 512];
        // Reverse order forces decoder restarts, not only its fast sequential path.
        for block in (0..226).rev() {
            disk.read(67 + file * 226 + block, &mut a).unwrap();
            image[block as usize * 512..(block as usize + 1) * 512].copy_from_slice(&a);
        }
        assert_eq!(&image[..2], b"BM");
        assert_eq!(i32::from_le_bytes(image[22..26].try_into().unwrap()), -240);
        for p in image[66..BMP_BYTES as usize].chunks_exact(2) {
            assert_eq!(p, value.to_le_bytes());
        }
        assert!(image[BMP_BYTES as usize..].iter().all(|b| *b == 0));
    }
    disk.read(BLOCKS - 1, &mut a).unwrap();
    assert_eq!(a, [0; 512]);
    assert_eq!(disk.read(BLOCKS, &mut a), Err(Error::Bounds));
    disk.read(1, &mut a).unwrap();
    assert_eq!(
        u16::from_le_bytes(a[227 * 2..228 * 2].try_into().unwrap()),
        0xffff
    );
}
