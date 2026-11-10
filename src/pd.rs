// SPDX-License-Identifier: GPL-2.0-or-later

pub(crate) mod cable;
pub use cable::CableCapture;
#[cfg(feature = "pd-charger")]
pub mod charger;

pub(crate) const PROFILES_PAGE: u32 = 5;
pub(crate) const EPR_PAGE: u32 = 6;
pub(crate) const SNIFF_PAGE: u32 = 7;
pub(crate) const CABLE_PAGE: u32 = 8;
#[cfg(feature = "pd-charger")]
pub const CHARGER_PAGE: u32 = CABLE_PAGE + 1;
pub(crate) const PD_PAGE_COUNT: u32 = CABLE_PAGE + 1 + cfg!(feature = "pd-charger") as u32;
pub(crate) const CABLE_MODE: u32 = 3;
const SOP_TOKEN: u8 = 7;
const SOP1_TOKEN: u8 = 6;

#[derive(Clone, Copy)]
pub struct SourceCaps {
    words: [u32; 7],
    count: u8,
}

#[derive(Debug, PartialEq, Eq)]
pub struct BadFrame;

fn checked_fifo(frame: &[u8], sop: u8) -> Result<(u8, u8, &[u8]), BadFrame> {
    let &[token, lo, hi, ref rest @ ..] = frame else {
        return Err(BadFrame);
    };
    let payload_len = usize::from((hi >> 4) & 7) * 4;
    if token >> 5 != sop || rest.len() != payload_len + 4 {
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
        let (lo, hi, payload) = checked_fifo(frame, SOP_TOKEN)?;
        Self::from_message(lo, hi, payload)
    }

    fn from_message(lo: u8, hi: u8, payload: &[u8]) -> Result<Option<Self>, BadFrame> {
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
        let (lo, hi, payload) = checked_fifo(frame, SOP_TOKEN)?;
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

const SNIFF_CAPACITY: usize = 192;
pub(crate) const SNIFF_ROWS: usize = 8;

#[derive(Clone, Copy)]
pub(crate) struct SniffEvent {
    lo: u8,
    hi: u8,
    detail: u8,
    extra: u8,
}

impl SniffEvent {
    pub(crate) fn source(self) -> bool {
        self.hi & 1 != 0
    }

    /// EPR Mode has exactly one data object: action in bits 31..24 and
    /// action-specific data in bits 23..16. Unknown actions remain raw.
    pub(crate) fn epr_mode(self) -> Option<(u8, u8, u8)> {
        if self.hi & 0xf0 == 0x10 && self.lo & 31 == 10 {
            Some((self.detail, self.extra, (self.hi >> 1) & 7))
        } else {
            None
        }
    }

    /// The suffix is a letter and an unsigned number: N objects, P PDO,
    /// C chunk, Q chunk request, I message ID or T unknown message type.
    pub(crate) fn label(self) -> (&'static [u8], u8, u8) {
        let kind = self.lo & 31;
        let id = (self.hi >> 1) & 7;
        let count = (self.hi >> 4) & 7;
        if self.hi & 0x80 != 0 {
            let (suffix, number) = if self.detail & 0x40 != 0 {
                (
                    if self.detail & 0x80 != 0 { b'Q' } else { b'C' },
                    self.detail & 15,
                )
            } else {
                (b'I', id)
            };
            return match kind {
                1 => (b"SRC_CAP_EXT", suffix, number),
                2 => (b"STATUS", suffix, number),
                17 => (b"EPR_SRC_CAP", suffix, number),
                _ => (b"EXT", b'T', kind),
            };
        }
        if count != 0 {
            return match kind {
                1 => (b"SRC_CAP", b'N', count),
                2 => (b"REQUEST", b'P', self.detail),
                4 => (b"SNK_CAP", b'N', count),
                6 => (b"ALERT", b'I', id),
                8 => (b"ENTER_USB", b'I', id),
                9 => (b"EPR_REQUEST", b'P', self.detail),
                10 => (b"EPR_MODE", b'N', count),
                15 => (b"VDM", b'I', id),
                _ => (b"DATA", b'T', kind),
            };
        }
        match kind {
            2 => (b"GOTO_MIN", b'I', id),
            3 => (b"ACCEPT", b'I', id),
            4 => (b"REJECT", b'I', id),
            5 => (b"PING", b'I', id),
            6 => (b"PS_RDY", b'I', id),
            7 => (b"GET_SRC_CAP", b'I', id),
            8 => (b"GET_SNK_CAP", b'I', id),
            9 => (b"DR_SWAP", b'I', id),
            10 => (b"PR_SWAP", b'I', id),
            11 => (b"VCONN_SWAP", b'I', id),
            12 => (b"WAIT", b'I', id),
            13 => (b"SOFT_RESET", b'I', id),
            16 => (b"NOT_SUPPORTED", b'I', id),
            17 => (b"GET_SRC_EXT", b'I', id),
            18 => (b"GET_STATUS", b'I', id),
            _ => (b"CTRL", b'T', kind),
        }
    }
}

/// Bounded CRC-checked SOP history with separate counts for GoodCRC and EPR keepalives.
#[derive(Clone, Copy)]
pub struct PdSniff {
    events: [SniffEvent; SNIFF_CAPACITY],
    start: u8,
    count: u8,
    total: u16,
    good_crc: u16,
    keepalive: u16,
    keepalive_ack: u16,
}

impl Default for PdSniff {
    fn default() -> Self {
        Self::new()
    }
}

impl PdSniff {
    pub const fn new() -> Self {
        Self {
            events: [EMPTY_SNIFF_EVENT; SNIFF_CAPACITY],
            start: 0,
            count: 0,
            total: 0,
            good_crc: 0,
            keepalive: 0,
            keepalive_ack: 0,
        }
    }

    pub fn push(&mut self, frame: &[u8]) -> Result<(), BadFrame> {
        let (lo, hi, payload) = checked_fifo(frame, SOP_TOKEN)?;
        let kind = lo & 31;
        if hi & 0x80 == 0 && payload.is_empty() && kind == 1 {
            self.good_crc = self.good_crc.saturating_add(1);
            return Ok(());
        }
        let (detail, extra) = if hi & 0x80 != 0 {
            if payload.len() < 2 {
                return Err(BadFrame);
            }
            let extended = u16::from_le_bytes([payload[0], payload[1]]);
            (
                ((extended >> 11) & 15) as u8
                    | if extended & 0x400 != 0 { 0x80 } else { 0 }
                    | if extended & 0x8000 != 0 { 0x40 } else { 0 },
                0,
            )
        } else if (kind == 2 || kind == 9) && !payload.is_empty() {
            (payload[3] >> 4, 0)
        } else if kind == 10 && payload.len() == 4 {
            (payload[3], payload[2])
        } else {
            (0, 0)
        };
        self.total = self.total.saturating_add(1);
        if hi & 0x80 != 0 && kind == 16 && lo >> 6 == 2 {
            // Extended Control, chunk zero, with a two-byte data block.
            let counter = match (hi & 1, payload) {
                (0, [2, 0x80, 3, 0]) => Some(&mut self.keepalive),
                (1, [2, 0x80, 4, 0]) => Some(&mut self.keepalive_ack),
                _ => None,
            };
            if let Some(counter) = counter {
                *counter = counter.saturating_add(1);
                return Ok(());
            }
        }
        let index = (usize::from(self.start) + usize::from(self.count)) % SNIFF_CAPACITY;
        self.events[index] = SniffEvent {
            lo,
            hi,
            detail,
            extra,
        };
        if usize::from(self.count) == SNIFF_CAPACITY {
            self.start = (self.start + 1) % SNIFF_CAPACITY as u8;
        } else {
            self.count += 1;
        }
        Ok(())
    }

    pub(crate) fn event(&self, index: usize) -> Option<SniffEvent> {
        if index >= usize::from(self.count) {
            None
        } else {
            Some(self.events[(usize::from(self.start) + index) % SNIFF_CAPACITY])
        }
    }

    #[cfg(test)]
    pub(crate) fn counts(&self) -> (u16, u16) {
        (self.total, self.good_crc)
    }

    /// Offset is measured backwards from the newest retained message.
    #[cfg_attr(not(any(test, feature = "pd-profiles")), allow(dead_code))]
    pub(crate) fn older_offset(&self, offset: u8) -> Option<u8> {
        let next = usize::from(offset) + SNIFF_ROWS;
        (next < usize::from(self.count)).then_some(next as u8)
    }

    /// Copy only the visible rows, keeping the screen stable during redraw.
    pub(crate) fn window(&self, offset: u8) -> SniffWindow {
        let count = usize::from(self.count);
        let end = count.saturating_sub(usize::from(offset).min(count));
        let start = end.saturating_sub(SNIFF_ROWS);
        let mut rows = [EMPTY_SNIFF_EVENT; SNIFF_ROWS];
        for (index, row) in rows.iter_mut().take(end - start).enumerate() {
            if let Some(event) = self.event(start + index) {
                *row = event;
            }
        }
        SniffWindow {
            rows,
            start: start as u8,
            end: end as u8,
            retained: self.count,
            total: self.total,
            good_crc: self.good_crc,
            keepalive: self.keepalive,
            keepalive_ack: self.keepalive_ack,
        }
    }
}

const EMPTY_SNIFF_EVENT: SniffEvent = SniffEvent {
    lo: 0,
    hi: 0,
    detail: 0,
    extra: 0,
};

#[derive(Clone, Copy)]
pub(crate) struct SniffWindow {
    pub(crate) rows: [SniffEvent; SNIFF_ROWS],
    pub(crate) start: u8,
    pub(crate) end: u8,
    pub(crate) retained: u8,
    pub(crate) total: u16,
    pub(crate) good_crc: u16,
    pub(crate) keepalive: u16,
    pub(crate) keepalive_ack: u16,
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
    pub(crate) sniff: Option<SniffWindow>,
    pub(crate) cable: CableCapture,
    pub(crate) cable_section: u8,
    // One retained warning bit per capture mode.
    pub(crate) fifo_warnings: u32,
}

impl Profiles {
    pub(crate) fn status(&self, mode: u32) -> &'static [u8] {
        if matches!(self.state, 4 | 5 | 9 | 16) {
            return match self.state {
                4 => b"CONFIG MISMATCH",
                5 => b"I2C ERROR",
                16 => b"RESTORE FAILED",
                _ => b"TIMER UNAVAILABLE",
            };
        }
        let fifo_warning = mode <= CABLE_MODE && self.fifo_warnings & (1u32 << mode) != 0;
        if self.mode != mode {
            if mode == 2 && self.sniff.is_some() {
                return if fifo_warning {
                    b"STOP WARN"
                } else {
                    b"STOPPED"
                };
            }
            return if (mode == 0 && self.caps.is_some())
                || (mode == 1 && self.epr.is_some())
                || (mode == CABLE_MODE && self.cable.captured())
            {
                if mode == 1 && self.epr.is_some_and(|caps| !caps.complete) {
                    if fifo_warning {
                        b"PARTIAL WARN"
                    } else {
                        b"PARTIAL"
                    }
                } else if fifo_warning {
                    b"CAPTURE WARN"
                } else {
                    b"CAPTURED"
                }
            } else if fifo_warning {
                b"WARN"
            } else {
                b"READY"
            };
        }
        if fifo_warning {
            match self.state {
                1 => return b"LISTEN WARN",
                2 => return b"CAPTURE WARN",
                3 if mode == 2 => return b"DONE WARN",
                3 if mode == 1 => return b"NO EPR WARN",
                3 if mode == CABLE_MODE => return b"NO ID WARN",
                3 => return b"NO CAP WARN",
                10 => return b"PARTIAL WARN",
                _ => {}
            }
        }
        match self.state {
            0 => b"READY",
            1 => b"LISTENING",
            2 => b"CAPTURED",
            3 if mode == 1 => b"NO EPR CAPS",
            3 if mode == 2 => b"DONE",
            3 if mode == CABLE_MODE && cfg!(feature = "pd-cable-read") => b"NO REPLY",
            3 if mode == CABLE_MODE => b"NO CABLE ID",
            3 => b"NO SOURCE CAPS",
            4 => b"CONFIG MISMATCH",
            5 => b"I2C ERROR",
            6 => b"FIFO FULL",
            7 => b"BAD RX FRAME",
            8 => b"LINK RESET",
            9 => b"TIMER UNAVAILABLE",
            11 => b"VBUS PRESENT",
            12 => b"CC BUSY",
            13 => b"NO RA ON CC2",
            14 => b"VCONN FAULT",
            15 => b"ACK WITHOUT ID",
            _ => b"PARTIAL",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fifo_warning_keeps_the_capture_state_visible() {
        let mut profiles = Profiles {
            state: 1,
            cc: 1,
            caps: None,
            epr: None,
            mode: 2,
            epr_counters: 0,
            sniff: None,
            cable: CableCapture::new(),
            cable_section: 0,
            fifo_warnings: 1 << 2,
        };
        assert_eq!(profiles.status(2), b"LISTEN WARN");
        profiles.state = 3;
        assert_eq!(profiles.status(2), b"DONE WARN");
        profiles.state = 6;
        assert_eq!(profiles.status(2), b"FIFO FULL");
        profiles.state = 2;
        profiles.mode = 1;
        profiles.fifo_warnings |= 1 << 1;
        assert_eq!(profiles.status(1), b"CAPTURE WARN");
        profiles.sniff = Some(PdSniff::new().window(0));
        assert_eq!(profiles.status(2), b"STOP WARN");
        profiles.fifo_warnings &= !(1 << 1);
        assert_eq!(profiles.status(1), b"CAPTURED");
        assert_eq!(profiles.status(2), b"STOP WARN");
    }

    fn fixture<const N: usize>(hex: &str) -> [u8; N] {
        let mut frame = [0; N];
        let mut bytes = hex.split_whitespace();
        for out in &mut frame {
            *out = u8::from_str_radix(bytes.next().unwrap(), 16).unwrap();
        }
        assert!(bytes.next().is_none());
        frame
    }

    #[test]
    fn cable_identity_requires_a_crc_valid_sop_prime_cable_ack() {
        let mut packets = include_str!("../tests/cable_identity.hex").lines();
        let request = fixture::<11>(packets.next().unwrap());
        let good_crc = fixture::<7>(packets.next().unwrap());
        let reply = fixture::<27>(packets.next().unwrap());
        let mut cable = CableCapture::new();
        let mut sop = reply;
        sop[0] = SOP_TOKEN << 5;
        assert_eq!(cable.push(&sop), Ok(false));
        assert_eq!(cable.sop_prime, 0);
        assert_eq!(cable.push(&request), Ok(false));
        assert_eq!(cable.push(&good_crc), Ok(false));
        let mut corrupt = reply;
        corrupt[26] ^= 1;
        assert_eq!(cable.push(&corrupt), Err(BadFrame));
        assert!(!cable.captured());

        // Same bytes with the actual SOP' token and a valid CRC.
        assert_eq!(cable.push(&reply), Ok(true));
        assert_eq!(cable.sop_prime, 3);
        let id = cable.identity.unwrap();
        assert_eq!(id.header, 0x518f);
        assert_eq!(
            id.words(),
            [0xff00_a841, 0x1c60_20c2, 0, 0x0a14_0310, 0x460a_2643]
        );
        assert!(!id.active());
        let vdo = id.vdo().unwrap();
        assert_eq!(
            (vdo.speed, vdo.voltage, vdo.current, vdo.latency),
            (3, 3, 2, 1)
        );
        assert!(vdo.epr);
        assert_eq!((vdo.hardware, vdo.firmware, vdo.version), (4, 6, 0));
        assert_eq!(cable.push(&reply), Ok(false));
    }

    #[test]
    fn cable_identity_preserves_legacy_words_and_requires_active_vdo2() {
        let mut reply = fixture::<27>(
            include_str!("../tests/cable_identity.hex")
                .lines()
                .last()
                .unwrap(),
        );
        reply[1] = 0x4f; // PD 2.0
        reply[4] = 0x80; // SVDM 1.0
        let crc = crc32(&reply[1..23]).to_le_bytes();
        reply[23..].copy_from_slice(&crc);
        let mut cable = CableCapture::new();
        assert_eq!(cable.push(&reply), Ok(true));
        assert_eq!(cable.identity.unwrap().words()[0], 0xff00_8041);
        let id = cable.identity.unwrap();
        assert!(id.pd2());
        let legacy = id.vdo().unwrap();
        assert_eq!((legacy.current, legacy.latency), (2, 1));
        assert!(!legacy.epr);

        reply[1] = 0x8f;
        reply[4] = 0xa8;
        reply[10] = 0x20; // Active Cable product type
        reply[19..23].copy_from_slice(&0x466a_365bu32.to_le_bytes());
        let crc = crc32(&reply[1..23]).to_le_bytes();
        reply[23..].copy_from_slice(&crc);
        let mut cable = CableCapture::new();
        assert_eq!(cable.push(&reply), Ok(false));

        let mut active = [0; 31];
        active[..23].copy_from_slice(&reply[..23]);
        active[2] = 0x61;
        active[23..27].copy_from_slice(&0x0040_0009u32.to_le_bytes());
        let crc = crc32(&active[1..27]).to_le_bytes();
        active[27..].copy_from_slice(&crc);
        assert_eq!(cable.push(&active), Ok(true));
        let id = cable.identity.unwrap();
        assert!(id.active());
        assert_eq!(id.words()[5], 0x0040_0009);
        let vdo = id.vdo().unwrap();
        assert!(vdo.vbus_through && vdo.sop2_present);
    }

    fn epr_frames() -> ([u8; 35], [u8; 27]) {
        (
            fixture(include_str!("../tests/epr_chunk0.hex")),
            fixture(include_str!("../tests/epr_chunk1.hex")),
        )
    }

    #[test]
    fn sniff_decodes_a_crc_checked_exchange_and_filters_goodcrc() {
        let mut lines = include_str!("../tests/sniff_exchange.hex").lines();
        let request = fixture::<11>(lines.next().unwrap());
        let accept = fixture::<7>(lines.next().unwrap());
        let ready = fixture::<7>(lines.next().unwrap());
        let good_crc = fixture::<7>(lines.next().unwrap());
        let source = fixture::<35>(include_str!("../tests/pd_offer.hex"));
        let mut sniff = PdSniff::new();
        for frame in [&source[..], &request, &accept, &ready, &good_crc] {
            sniff.push(frame).unwrap();
        }
        assert_eq!(sniff.counts(), (4, 1));
        assert!(sniff.event(0).unwrap().source());
        assert_eq!(sniff.event(0).unwrap().label(), (&b"SRC_CAP"[..], b'N', 7));
        assert!(!sniff.event(1).unwrap().source());
        assert_eq!(sniff.event(1).unwrap().label(), (&b"REQUEST"[..], b'P', 1));
        assert_eq!(sniff.event(2).unwrap().label(), (&b"ACCEPT"[..], b'I', 0));
        assert_eq!(sniff.event(3).unwrap().label(), (&b"PS_RDY"[..], b'I', 1));
        assert!(sniff.event(4).is_none());
        let mut bad = ready;
        bad[3] ^= 1;
        assert!(sniff.push(&bad).is_err());
        assert_eq!(sniff.counts(), (4, 1));
    }

    #[test]
    fn sniff_counts_epr_keepalives_without_evicting_negotiation_rows() {
        let mut lines = include_str!("../tests/sniff_keepalive.hex").lines();
        let keepalive = fixture::<11>(lines.next().unwrap());
        let ack = fixture::<11>(lines.next().unwrap());
        let (first, second) = epr_frames();
        let mut sniff = PdSniff::new();
        sniff.push(&first).unwrap();
        sniff.push(&second).unwrap();
        for _ in 0..SNIFF_CAPACITY {
            sniff.push(&keepalive).unwrap();
            sniff.push(&ack).unwrap();
        }
        let mut bad = keepalive;
        bad[10] ^= 1;
        assert_eq!(sniff.push(&bad), Err(BadFrame));

        let window = sniff.window(0);
        assert_eq!(window.total as usize, 2 + 2 * SNIFF_CAPACITY);
        assert_eq!(window.retained, 2);
        assert_eq!(window.keepalive as usize, SNIFF_CAPACITY);
        assert_eq!(window.keepalive_ack as usize, SNIFF_CAPACITY);
        assert_eq!(window.rows[0].label(), (&b"EPR_SRC_CAP"[..], b'C', 0));
        assert_eq!(window.rows[1].label(), (&b"EPR_SRC_CAP"[..], b'C', 1));

        // A different Extended Control subtype and an unexpected sender stay visible.
        let mut get_caps = keepalive;
        get_caps[5] = 1;
        let mut wrong_role = ack;
        wrong_role[2] &= !1;
        for mut frame in [get_caps, wrong_role] {
            let crc = crc32(&frame[1..7]).to_le_bytes();
            frame[7..].copy_from_slice(&crc);
            sniff.push(&frame).unwrap();
        }
        let window = sniff.window(0);
        assert_eq!(window.retained, 4);
        assert_eq!(window.keepalive as usize, SNIFF_CAPACITY);
        assert_eq!(window.keepalive_ack as usize, SNIFF_CAPACITY);
        assert_eq!(window.rows[2].label(), (&b"EXT"[..], b'T', 16));
        assert_eq!(window.rows[3].label(), (&b"EXT"[..], b'T', 16));
    }

    #[test]
    fn sniff_keeps_and_pages_the_latest_messages_in_wire_order() {
        let mut sniff = PdSniff::new();
        for index in 0..SNIFF_CAPACITY + 10 {
            let mut frame = [0xe0, 0x8a, 0x11, 0, 0, index as u8, 1, 0, 0, 0, 0];
            let crc = crc32(&frame[1..7]).to_le_bytes();
            frame[7..].copy_from_slice(&crc);
            sniff.push(&frame).unwrap();
        }
        assert_eq!(sniff.counts(), ((SNIFF_CAPACITY + 10) as u16, 0));
        assert_eq!(sniff.event(0).unwrap().epr_mode(), Some((1, 10, 0)));
        assert_eq!(
            sniff.event(SNIFF_CAPACITY - 1).unwrap().epr_mode(),
            Some((1, 201, 0))
        );
        assert!(sniff.event(SNIFF_CAPACITY).is_none());
        let mut offset = 0;
        for page in 0..SNIFF_CAPACITY / SNIFF_ROWS {
            let window = sniff.window(offset);
            let end = SNIFF_CAPACITY - page * SNIFF_ROWS;
            assert_eq!((window.start as usize, window.end as usize), (end - 8, end));
            assert_eq!(window.retained as usize, SNIFF_CAPACITY);
            for (row, event) in window.rows.iter().enumerate() {
                assert_eq!(event.epr_mode(), Some((1, (10 + end - 8 + row) as u8, 0)));
            }
            match sniff.older_offset(offset) {
                Some(next) => offset = next,
                None => assert_eq!(page, SNIFF_CAPACITY / SNIFF_ROWS - 1),
            }
        }
    }

    #[test]
    fn sniff_pages_partial_history_without_losing_the_first_rows() {
        let ready = fixture::<7>(
            include_str!("../tests/sniff_exchange.hex")
                .lines()
                .nth(2)
                .unwrap(),
        );
        for count in [0, 1, 8, 9, 10, 16, 17] {
            let mut sniff = PdSniff::new();
            for _ in 0..count {
                sniff.push(&ready).unwrap();
            }
            let mut offset = 0;
            let mut end = count;
            loop {
                let window = sniff.window(offset);
                assert_eq!(window.end as usize, end);
                assert_eq!(window.start as usize, end.saturating_sub(8));
                end = end.saturating_sub(8);
                match sniff.older_offset(offset) {
                    Some(next) => offset = next,
                    None => {
                        assert_eq!(end, 0);
                        break;
                    }
                }
            }
        }
    }

    #[test]
    fn sniff_decodes_epr_mode_action_and_data_without_guessing_unknown_values() {
        let mut sniff = PdSniff::new();
        for (index, (action, data)) in [(1, 240), (2, 0), (3, 0), (4, 1), (5, 0), (0xfe, 0xab)]
            .into_iter()
            .enumerate()
        {
            // Synthetic, CRC-valid SOP EPR_Mode packet with one data object.
            let mut frame = [
                0xe0,
                0x8a,
                0x10 | ((index as u8) << 1),
                0,
                0,
                data,
                action,
                0,
                0,
                0,
                0,
            ];
            let crc = crc32(&frame[1..7]).to_le_bytes();
            frame[7..].copy_from_slice(&crc);
            sniff.push(&frame).unwrap();
            assert_eq!(
                sniff.event(index).unwrap().epr_mode(),
                Some((action, data, index as u8))
            );
        }
        // EPR_Mode with two data objects is not a valid EPRMDO and is not decoded.
        let mut frame = [0xe0, 0x8a, 0x20, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0];
        let crc = crc32(&frame[1..11]).to_le_bytes();
        frame[11..].copy_from_slice(&crc);
        sniff.push(&frame).unwrap();
        assert_eq!(sniff.event(6).unwrap().epr_mode(), None);
        assert_eq!(sniff.event(6).unwrap().label(), (&b"EPR_MODE"[..], b'N', 2));
    }

    #[test]
    fn sniff_identifies_epr_chunks_without_claiming_a_contract() {
        let (first, second) = epr_frames();
        let mut sniff = PdSniff::new();
        sniff.push(&first).unwrap();
        sniff.push(&second).unwrap();
        assert_eq!(
            sniff.event(0).unwrap().label(),
            (&b"EPR_SRC_CAP"[..], b'C', 0)
        );
        assert_eq!(
            sniff.event(1).unwrap().label(),
            (&b"EPR_SRC_CAP"[..], b'C', 1)
        );

        let mut chunk_request = first;
        chunk_request[2] &= !1; // Message header claims sink power role.
        chunk_request[4] |= 4; // Extended header requests a chunk.
        let crc = crc32(&chunk_request[1..31]).to_le_bytes();
        chunk_request[31..].copy_from_slice(&crc);
        sniff.push(&chunk_request).unwrap();
        assert!(!sniff.event(2).unwrap().source());
        assert_eq!(
            sniff.event(2).unwrap().label(),
            (&b"EPR_SRC_CAP"[..], b'Q', 0)
        );

        let mut unchunked = first;
        unchunked[4] &= !0x80;
        let crc = crc32(&unchunked[1..31]).to_le_bytes();
        unchunked[31..].copy_from_slice(&crc);
        sniff.push(&unchunked).unwrap();
        let (label, suffix, _) = sniff.event(3).unwrap().label();
        assert_eq!((label, suffix), (&b"EPR_SRC_CAP"[..], b'I'));
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
