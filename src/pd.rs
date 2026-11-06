// SPDX-License-Identifier: GPL-2.0-or-later

pub(crate) const PROFILES_PAGE: u32 = 5;

#[derive(Clone, Copy)]
pub struct SourceCaps {
    words: [u32; 7],
    count: u8,
}

#[derive(Debug, PartialEq, Eq)]
pub struct BadFrame;

impl SourceCaps {
    /// FUSB302 RX FIFO record: SOP token, two header bytes, data objects, CRC.
    /// A well-formed packet of another type is ignored.
    pub fn from_fifo(frame: &[u8]) -> Result<Option<Self>, BadFrame> {
        let &[token, lo, hi, ref rest @ ..] = frame else {
            return Err(BadFrame);
        };
        let count = usize::from((hi >> 4) & 7);
        let payload_len = count * 4;
        if token >> 5 != 7 || rest.len() != payload_len + 4 {
            return Err(BadFrame);
        }
        let wire_len = 2 + payload_len;
        if crc32(&frame[1..][..wire_len]).to_le_bytes() != rest[payload_len..] {
            return Err(BadFrame);
        }
        // Non-extended Source_Capabilities from the source on SOP.
        if hi & 0x81 != 0x01 || lo & 0x1f != 1 || count == 0 {
            return Ok(None);
        }
        let mut words = [0; 7];
        for (word, bytes) in words.iter_mut().zip(rest[..payload_len].chunks_exact(4)) {
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
    Fixed { mv: u32, ma: u32 },
    Pps { min_mv: u32, max_mv: u32, ma: u32 },
    SprAvs { ma_to_15v: u32, ma_above_15v: u32 },
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
}

impl Profiles {
    pub(crate) fn status(self) -> &'static [u8] {
        match self.state {
            0 => b"READY",
            1 => b"LISTENING",
            2 => b"CAPTURED",
            3 => b"NO SOURCE CAPS",
            4 => b"CONFIG MISMATCH",
            5 => b"I2C ERROR",
            6 => b"RX FIFO FULL",
            7 => b"BAD RX FRAME",
            8 => b"LINK RESET",
            _ => b"TIMER UNAVAILABLE",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
