// SPDX-License-Identifier: GPL-2.0-or-later

pub(crate) const PROFILES_PAGE: u32 = 5;
pub(crate) const EPR_PAGE: u32 = 6;

#[derive(Clone, Copy)]
pub struct SourceCaps {
    words: [u32; 7],
    count: u8,
}

#[derive(Debug, PartialEq, Eq)]
pub struct BadFrame;

fn checked_fifo(frame: &[u8]) -> Result<(u8, u8, &[u8]), BadFrame> {
    let &[token, lo, hi, ref rest @ ..] = frame else {
        return Err(BadFrame);
    };
    let payload_len = usize::from((hi >> 4) & 7) * 4;
    if token >> 5 != 7 || rest.len() != payload_len + 4 {
        return Err(BadFrame);
    }
    if crc32(&frame[1..][..2 + payload_len]).to_le_bytes() != rest[payload_len..] {
        return Err(BadFrame);
    }
    Ok((lo, hi, &rest[..payload_len]))
}

impl SourceCaps {
    /// FUSB302 RX FIFO record: SOP token, two header bytes, data objects, CRC.
    /// A well-formed packet of another type is ignored.
    pub fn from_fifo(frame: &[u8]) -> Result<Option<Self>, BadFrame> {
        let (lo, hi, payload) = checked_fifo(frame)?;
        let count = payload.len() / 4;
        // Non-extended Source_Capabilities from the source on SOP.
        if hi & 0x81 != 0x01 || lo & 0x1f != 1 || count == 0 {
            return Ok(None);
        }
        let mut words = [0; 7];
        for (word, bytes) in words.iter_mut().zip(payload.chunks_exact(4)) {
            *word = u32::from(bytes[0])
                | u32::from(bytes[1]) << 8
                | u32::from(bytes[2]) << 16
                | u32::from(bytes[3]) << 24;
        }
        // The first source PDO must be fixed 5 V, including on an EPR-capable source.
        if words[0] >> 30 != 0 || (words[0] >> 10) & 0x3ff != 100 {
            return Err(BadFrame);
        }
        Ok(Some(Self {
            words,
            count: count as u8,
        }))
    }

    pub(crate) fn rows(&self) -> &[u32] {
        &self.words[..usize::from(self.count)]
    }
}

/// The EPR part starts at object position 8, two bytes into chunk 1.
#[derive(Clone, Copy)]
pub struct EprCaps {
    words: [u32; 4],
    count: u8,
    pub(crate) complete: bool,
}

impl EprCaps {
    pub(crate) fn rows(&self) -> &[u32] {
        &self.words[..usize::from(self.count)]
    }
}

pub enum EprEvent {
    Ignored,
    Started,
    Partial(EprCaps),
    Complete(EprCaps),
}

pub struct EprCapture {
    first_size: u8,
    first_id: u8,
    first_sender: u8,
    first_at: u32,
    rx_count: u8,
    spr_count: u8,
    chunk0_count: u8,
    chunk1_count: u8,
}

impl Default for EprCapture {
    fn default() -> Self {
        Self::new()
    }
}

impl EprCapture {
    pub const fn new() -> Self {
        Self {
            first_size: 0,
            first_id: 0,
            first_sender: 0,
            first_at: 0,
            rx_count: 0,
            spr_count: 0,
            chunk0_count: 0,
            chunk1_count: 0,
        }
    }

    /// CRC-valid SOP records, SPR source offers, and decoded EPR chunks.
    pub fn counters(&self) -> u32 {
        u32::from_le_bytes([
            self.rx_count,
            self.spr_count,
            self.chunk0_count,
            self.chunk1_count,
        ])
    }

    /// Accept two independently CRC-checked EPR_Source_Capabilities chunks.
    pub fn push(&mut self, frame: &[u8], now: u32) -> Result<EprEvent, BadFrame> {
        let (lo, hi, payload) = checked_fifo(frame)?;
        self.rx_count = self.rx_count.saturating_add(1);
        if hi & 0x81 == 0x01 && lo & 0x1f == 1 && !payload.is_empty() {
            self.spr_count = self.spr_count.saturating_add(1);
        }
        if hi & 0x81 != 0x81 || lo & 0x1f != 17 {
            let soft_reset = hi & 0x80 == 0 && payload.is_empty() && lo & 0x1f == 13;
            let source_message =
                hi & 1 != 0 && !(hi & 0x80 == 0 && payload.is_empty() && lo & 0x1f == 1);
            if soft_reset || source_message {
                self.first_size = 0;
            }
            return Ok(EprEvent::Ignored);
        }
        if payload.len() < 4 {
            return Err(BadFrame);
        }
        let extended = u16::from_le_bytes([payload[0], payload[1]]);
        if extended & 0x8600 != 0x8000 {
            return Ok(EprEvent::Ignored);
        }
        let size = usize::from(extended & 0x1ff);
        if !(28..=44).contains(&size) || size % 4 != 0 {
            return Err(BadFrame);
        }
        let chunk = (extended >> 11) & 15;
        let id = (hi >> 1) & 7;
        if chunk == 0 {
            if payload.len() != 28 {
                return Err(BadFrame);
            }
            let first = u32::from_le_bytes([payload[2], payload[3], payload[4], payload[5]]);
            if first >> 30 != 0 || (first >> 10) & 0x3ff != 100 {
                return Err(BadFrame);
            }
            self.first_size = size as u8;
            self.first_id = id;
            self.first_sender = lo & 0xe0;
            self.first_at = now;
            self.chunk0_count = self.chunk0_count.saturating_add(1);
            return Ok(EprEvent::Started);
        }
        if chunk != 1 {
            return Ok(EprEvent::Ignored);
        }
        let data_len = size - 26;
        if payload.len() != ((2 + data_len + 3) & !3) {
            return Err(BadFrame);
        }
        self.chunk1_count = self.chunk1_count.saturating_add(1);
        let complete = usize::from(self.first_size) == size
            && id == ((self.first_id + 1) & 7)
            && self.first_sender == (lo & 0xe0)
            && now.wrapping_sub(self.first_at) <= 500;
        self.first_size = 0;
        let mut words = [0; 4];
        let count = (size - 28) / 4;
        for (word, bytes) in words
            .iter_mut()
            .zip(payload[4..4 + count * 4].chunks_exact(4))
        {
            *word = u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
        }
        let caps = EprCaps {
            words,
            count: count as u8,
            complete,
        };
        Ok(if complete {
            EprEvent::Complete(caps)
        } else {
            EprEvent::Partial(caps)
        })
    }
}

fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for &byte in bytes {
        crc ^= u32::from(byte);
        for _ in 0..8 {
            let polynomial = if crc & 1 != 0 { 0xedb8_8320 } else { 0 };
            crc = (crc >> 1) ^ polynomial;
        }
    }
    !crc
}

#[derive(Debug, PartialEq, Eq)]
pub(crate) enum Pdo {
    Fixed {
        mv: u32,
        ma: u32,
    },
    Pps {
        min_mv: u32,
        max_mv: u32,
        ma: u32,
    },
    SprAvs {
        ma_to_15v: u32,
        ma_above_15v: u32,
    },
    EprAvs {
        min_mv: u32,
        max_mv: u32,
        pdp_w: u32,
    },
    Raw(u32),
}

impl Pdo {
    pub(crate) fn decode(word: u32) -> Self {
        match word >> 28 {
            0..=3 => Self::Fixed {
                mv: ((word >> 10) & 0x3ff) * 50,
                ma: (word & 0x3ff) * 10,
            },
            0xc => Self::Pps {
                min_mv: ((word >> 8) & 0xff) * 100,
                max_mv: ((word >> 17) & 0xff) * 100,
                ma: (word & 0x7f) * 50,
            },
            0xd => Self::EprAvs {
                min_mv: ((word >> 8) & 0xff) * 100,
                max_mv: ((word >> 17) & 0x1ff) * 100,
                pdp_w: word & 0xff,
            },
            0xe => Self::SprAvs {
                ma_to_15v: ((word >> 10) & 0x3ff) * 10,
                ma_above_15v: (word & 0x3ff) * 10,
            },
            _ => Self::Raw(word),
        }
    }
}

#[derive(Clone, Copy)]
pub(crate) struct Profiles {
    // Status numbers are shared with enum t3p_pd_state in native/t3p_pd.h.
    pub(crate) state: u32,
    pub(crate) cc: u32,
    pub(crate) caps: Option<SourceCaps>,
    pub(crate) epr: Option<EprCaps>,
    pub(crate) mode: u32,
    pub(crate) epr_counters: u32,
}

impl Profiles {
    pub(crate) fn status(self, mode: u32) -> &'static [u8] {
        if matches!(self.state, 4 | 5 | 9) {
            return match self.state {
                4 => b"CONFIG MISMATCH",
                5 => b"I2C ERROR",
                _ => b"TIMER UNAVAILABLE",
            };
        }
        if self.mode != mode {
            return if mode == 0 && self.caps.is_some() || mode == 1 && self.epr.is_some() {
                if mode == 1 && self.epr.is_some_and(|caps| !caps.complete) {
                    b"PARTIAL"
                } else {
                    b"CAPTURED"
                }
            } else {
                b"READY"
            };
        }
        match self.state {
            0 => b"READY",
            1 => b"LISTENING",
            2 => b"CAPTURED",
            3 if mode == 1 => b"NO EPR CAPS",
            3 => b"NO SOURCE CAPS",
            4 => b"CONFIG MISMATCH",
            5 => b"I2C ERROR",
            6 => b"RX FIFO FULL",
            7 => b"BAD RX FRAME",
            8 => b"LINK RESET",
            9 => b"TIMER UNAVAILABLE",
            _ => b"PARTIAL",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture<const N: usize>(hex: &str) -> [u8; N] {
        let mut frame = [0; N];
        let mut bytes = hex.split_whitespace();
        for out in &mut frame {
            *out = u8::from_str_radix(bytes.next().unwrap(), 16).unwrap();
        }
        assert!(bytes.next().is_none());
        frame
    }

    fn epr_frames() -> ([u8; 35], [u8; 27]) {
        (
            fixture(include_str!("../tests/epr_chunk0.hex")),
            fixture(include_str!("../tests/epr_chunk1.hex")),
        )
    }

    #[test]
    fn reconstructs_epr_offer_from_two_chunks() {
        let (first, second) = epr_frames();
        let mut capture = EprCapture::new();
        assert!(matches!(
            capture.push(&first, u32::MAX - 10),
            Ok(EprEvent::Started)
        ));
        let EprEvent::Complete(caps) = capture.push(&second, 20).unwrap() else {
            panic!("expected complete EPR offer");
        };
        assert!(caps.complete);
        assert_eq!(
            caps.rows(),
            [0x0018_c1f4, 0x001b_41f4, 0x001f_01f4, 0xd7c0_96f0]
        );
        assert_eq!(
            Pdo::decode(caps.rows()[0]),
            Pdo::Fixed {
                mv: 28000,
                ma: 5000
            }
        );
        assert_eq!(
            Pdo::decode(caps.rows()[2]),
            Pdo::Fixed {
                mv: 48000,
                ma: 5000
            }
        );
        assert_eq!(
            Pdo::decode(caps.rows()[3]),
            Pdo::EprAvs {
                min_mv: 15000,
                max_mv: 48000,
                pdp_w: 240
            }
        );
    }

    #[test]
    fn counts_crc_valid_records_and_recognized_epr_chunks() {
        let (first, second) = epr_frames();
        let spr = fixture::<35>(include_str!("../tests/pd_offer.hex"));
        let mut capture = EprCapture::new();
        assert_eq!(capture.counters().to_le_bytes(), [0, 0, 0, 0]);
        assert!(matches!(capture.push(&spr, 0), Ok(EprEvent::Ignored)));
        assert!(matches!(capture.push(&first, 1), Ok(EprEvent::Started)));
        let mut bad_crc = second;
        bad_crc[10] ^= 1;
        assert!(capture.push(&bad_crc, 2).is_err());
        assert_eq!(capture.counters().to_le_bytes(), [2, 1, 1, 0]);
        assert!(matches!(
            capture.push(&second, 3),
            Ok(EprEvent::Complete(_))
        ));
        assert_eq!(capture.counters().to_le_bytes(), [3, 1, 1, 1]);
        for _ in 0..260 {
            assert!(matches!(capture.push(&spr, 4), Ok(EprEvent::Ignored)));
        }
        assert_eq!(capture.counters().to_le_bytes(), [255, 255, 1, 1]);
    }

    #[test]
    fn missing_or_stale_first_chunk_stays_partial() {
        let (first, second) = epr_frames();
        let mut capture = EprCapture::new();
        let EprEvent::Partial(partial) = capture.push(&second, 100).unwrap() else {
            panic!("a lone chunk cannot be complete");
        };
        assert!(!partial.complete);
        assert_eq!(partial.rows().len(), 4);
        assert!(matches!(capture.push(&first, 100), Ok(EprEvent::Started)));
        assert!(matches!(
            capture.push(&second, 601),
            Ok(EprEvent::Partial(_))
        ));
        assert!(matches!(capture.push(&first, 700), Ok(EprEvent::Started)));
        let mut corrupted = second;
        corrupted[10] ^= 1;
        assert!(capture.push(&corrupted, 701).is_err());
        assert!(capture.push(&second[..26], 701).is_err());
    }

    #[test]
    fn ignores_chunk_requests_from_sink() {
        let (first, mut request) = epr_frames();
        let mut capture = EprCapture::new();
        assert!(matches!(capture.push(&first, 1), Ok(EprEvent::Started)));
        request[2] &= !1;
        let checksum = crc32(&request[1..23]).to_le_bytes();
        request[23..].copy_from_slice(&checksum);
        assert!(matches!(capture.push(&request, 2), Ok(EprEvent::Ignored)));
        let (_, second) = epr_frames();
        assert!(matches!(
            capture.push(&second, 3),
            Ok(EprEvent::Complete(_))
        ));
    }

    #[test]
    fn soft_reset_between_chunks_makes_the_offer_partial() {
        let (first, second) = epr_frames();
        let mut capture = EprCapture::new();
        assert!(matches!(capture.push(&first, 1), Ok(EprEvent::Started)));
        let mut reset = [0xe0, 0xad, 0x01, 0, 0, 0, 0];
        let checksum = crc32(&reset[1..3]).to_le_bytes();
        reset[3..].copy_from_slice(&checksum);
        assert!(matches!(capture.push(&reset, 2), Ok(EprEvent::Ignored)));
        assert!(matches!(capture.push(&second, 3), Ok(EprEvent::Partial(_))));
    }

    #[test]
    fn decodes_seven_objects_and_checks_the_wire_crc() {
        let mut frame = [0; 35];
        for (out, hex) in frame
            .iter_mut()
            .zip(include_str!("../tests/pd_offer.hex").split_whitespace())
        {
            *out = u8::from_str_radix(hex, 16).unwrap();
        }
        let caps = SourceCaps::from_fifo(&frame).unwrap().unwrap();
        assert_eq!(caps.rows().len(), 7);
        assert_eq!(
            Pdo::decode(caps.rows()[0]),
            Pdo::Fixed { mv: 5000, ma: 3000 }
        );
        assert_eq!(
            Pdo::decode(caps.rows()[3]),
            Pdo::Fixed {
                mv: 20000,
                ma: 5000
            }
        );
        assert_eq!(
            Pdo::decode(caps.rows()[4]),
            Pdo::Pps {
                min_mv: 3300,
                max_mv: 21000,
                ma: 5000
            }
        );
        assert_eq!(
            Pdo::decode(caps.rows()[5]),
            Pdo::SprAvs {
                ma_to_15v: 3000,
                ma_above_15v: 5000
            }
        );
        assert_eq!(Pdo::decode(caps.rows()[6]), Pdo::Raw(0xf012_3456));

        assert!(SourceCaps::from_fifo(&frame[..34]).is_err());
        frame[3] ^= 1;
        assert!(SourceCaps::from_fifo(&frame).is_err());
    }

    #[test]
    fn rejects_a_cable_packet_as_sop_and_skips_goodcrc() {
        let mut frame = [0xe0, 0x81, 0, 0, 0, 0, 0];
        let crc = crc32(&frame[1..3]).to_le_bytes();
        frame[3..].copy_from_slice(&crc);
        assert!(SourceCaps::from_fifo(&frame).unwrap().is_none());
        frame[0] = 0xc0;
        assert!(SourceCaps::from_fifo(&frame).is_err());
    }
}
