// SPDX-License-Identifier: GPL-2.0-or-later
use super::{BadFrame, SOP_TOKEN, SourceCaps, checked_fifo};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct SourceExtended {
    pub vid: u16,
    pub pid: u16,
    pub xid: u32,
    pub fw: u8,
    pub hw: u8,
    pub spr_pdp: u8,
    pub epr_pdp: Option<u8>,
    pub regulation: u8,
    pub hold_ms: u8,
    pub compliance: u8,
    pub touch: u8,
    pub peaks: [u16; 3],
    pub touch_temp: u8,
    pub inputs: u8,
    pub batteries: u8,
}

impl SourceExtended {
    fn decode(payload: &[u8]) -> Result<Self, BadFrame> {
        // With no unchunked support advertised, expect the complete SCEDB
        // in data chunk zero, with padding to the data-object boundary.
        let data = extended_data(payload, 24)?;
        Ok(Self {
            vid: u16::from_le_bytes([data[0], data[1]]),
            pid: u16::from_le_bytes([data[2], data[3]]),
            xid: u32::from_le_bytes([data[4], data[5], data[6], data[7]]),
            fw: data[8],
            hw: data[9],
            spr_pdp: data[23],
            epr_pdp: data.get(24).copied(),
            regulation: data[10],
            hold_ms: data[11],
            compliance: data[12],
            touch: data[13],
            peaks: core::array::from_fn(|i| {
                u16::from_le_bytes([data[14 + i * 2], data[15 + i * 2]])
            }),
            touch_temp: data[20],
            inputs: data[21],
            batteries: data[22],
        })
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum QueryResult {
    #[default]
    NotQueried,
    Ok,
    NotSupported,
    Rejected,
    Wait,
    NoReply,
    NoGoodCrc,
    Blocked,
    Interrupted,
    BadReply,
    Pd2,
}

impl QueryResult {
    pub(crate) const fn label(self) -> &'static [u8] {
        match self {
            Self::NotQueried => b"NOT QUERIED",
            Self::Ok => b"OK",
            Self::NotSupported => b"NOT SUPPORTED",
            Self::Rejected => b"REJECT",
            Self::Wait => b"WAIT",
            Self::NoReply => b"NO REPLY",
            Self::NoGoodCrc => b"NO GOODCRC",
            Self::Blocked => b"BLOCKED",
            Self::Interrupted => b"INTERRUPTED",
            Self::BadReply => b"BAD REPLY",
            Self::Pd2 => b"PD 2.0 N/A",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct SourceInfo {
    pub first: u32,
    pub second: Option<u32>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct SourceStatus {
    pub bytes: [u8; 7],
    pub len: u8,
}

// Extended replies fit in chunk zero. Ignore reserved bit 9 and any future
// fields that fit in the same chunk; never interpret padding as data.
fn extended_data(payload: &[u8], minimum: usize) -> Result<&[u8], BadFrame> {
    let [lo, hi, body @ ..] = payload else {
        return Err(BadFrame);
    };
    let header = u16::from_le_bytes([*lo, *hi]);
    let size = usize::from(header & 0x1ff);
    if header & 0xfc00 != 0x8000
        || size < minimum
        || size > 26
        || (size + 2).next_multiple_of(4) != payload.len()
    {
        return Err(BadFrame);
    }
    Ok(&body[..size])
}

pub const CHARGER_SECTIONS: u8 = 9;

#[derive(Clone, Copy, Default)]
pub struct ChargerCapture {
    pub(crate) state: u32,
    pub(crate) caps: Option<SourceCaps>,
    pub(crate) extended: Option<SourceExtended>,
    pub(crate) measured_mv: Option<u16>,
    pub(crate) revision: u8,
    pub(crate) request_current: u8,
    pub(crate) accepted: bool,
    pub(crate) ps_rdy: bool,
    pub(crate) status: Option<SourceStatus>,
    pub(crate) info: Option<SourceInfo>,
    pub(crate) revision_data: Option<u32>,
    pub(crate) queries: [QueryResult; 4],
    last_source_id: Option<u8>,
}

impl ChargerCapture {
    pub const fn new() -> Self {
        Self {
            state: 0,
            caps: None,
            extended: None,
            measured_mv: None,
            revision: 0,
            request_current: 0,
            accepted: false,
            ps_rdy: false,
            status: None,
            info: None,
            revision_data: None,
            queries: [QueryResult::NotQueried; 4],
            last_source_id: None,
        }
    }

    pub fn set_state(&mut self, state: u32) {
        self.state = state;
        if state == 3 {
            self.queries = [QueryResult::Pd2; 4];
        }
    }

    /// Native transaction result for phases 3..6, independently of other queries.
    pub fn set_query_state(&mut self, phase: u32, state: u32) {
        let Some(query) = phase
            .checked_sub(3)
            .and_then(|i| self.queries.get_mut(i as usize))
        else {
            return;
        };
        // A validated reply is stronger evidence than a late PHY status bit.
        if *query == QueryResult::Ok {
            return;
        }
        *query = match state {
            0 => QueryResult::Ok,
            4 | 15 => QueryResult::Blocked,
            5 => QueryResult::NoReply,
            6 => QueryResult::Rejected,
            7 => QueryResult::Wait,
            8 => QueryResult::NotSupported,
            12 => QueryResult::NoGoodCrc,
            18 => QueryResult::BadReply,
            _ => QueryResult::Interrupted,
        };
    }

    pub fn set_vbus(&mut self, mv: u16) {
        self.measured_mv = Some(mv);
    }

    pub fn has_offer(&self) -> bool {
        self.caps.is_some()
    }

    // Phase and return values match the transaction in native/t3p_charger.c.
    pub fn record(&mut self, frame: &[u8], phase: u32) -> u32 {
        match self.receive(frame, phase) {
            Ok(result) => result,
            Err(_) => 6,
        }
    }

    fn receive(&mut self, frame: &[u8], phase: u32) -> Result<u32, BadFrame> {
        let (lo, hi, payload) = checked_fifo(frame, SOP_TOKEN)?;
        let kind = lo & 0x1f;
        let control = hi & 0x80 == 0 && payload.is_empty();
        let id = (hi >> 1) & 7;
        if hi & 1 == 0 {
            return Ok(0);
        }
        if control && kind == 1 {
            // GoodCRC echoes the query's MessageID and leaves the source sequence alone.
            return Ok(
                if (3..=6).contains(&phase) && self.ps_rdy && u32::from(id) == phase - 2 {
                    7
                } else {
                    0
                },
            );
        }
        if control && kind == 13 {
            return Ok(5); // Soft Reset invalidates the short transaction.
        }
        if self.last_source_id == Some(id) {
            return Ok(0);
        }
        self.last_source_id = Some(id);

        if phase == 0 {
            let Some(caps) = SourceCaps::from_message(lo, hi, payload)? else {
                return Ok(0);
            };
            let revision = lo & 0xc0;
            let current = (caps.rows()[0] & 0x3ff).min(50) as u8;
            if !matches!(revision, 0x40 | 0x80) || current == 0 {
                return Err(BadFrame);
            }
            self.caps = Some(caps);
            self.revision = revision;
            self.request_current = current;
            return Ok(1);
        }

        if self.caps.is_none() || lo & 0xc0 != self.revision {
            return Ok(0);
        }
        if control {
            return Ok(match (phase, kind) {
                (1, 3) => {
                    self.accepted = true;
                    1
                }
                (2, 6) if self.accepted => {
                    self.ps_rdy = true;
                    1
                }
                (1 | 3..=6, 4) => 2,
                (1 | 3..=6, 12) => 3,
                (1 | 3..=6, 16) => 4,
                _ => 0,
            });
        }
        if self.ps_rdy {
            match (phase, hi & 0x80 != 0, kind) {
                (3, true, 1) => self.extended = Some(SourceExtended::decode(payload)?),
                (4, true, 2) => {
                    let data = extended_data(payload, 6)?;
                    let mut bytes = [0; 7];
                    let len = data.len().min(bytes.len());
                    bytes[..len].copy_from_slice(&data[..len]);
                    self.status = Some(SourceStatus {
                        bytes,
                        len: len as u8,
                    });
                }
                (5, false, 11) => {
                    if !matches!(payload.len(), 4 | 8) {
                        return Err(BadFrame);
                    }
                    let first = u32::from_le_bytes(payload[..4].try_into().map_err(|_| BadFrame)?);
                    let second = payload
                        .get(4..8)
                        .map(|p| u32::from_le_bytes([p[0], p[1], p[2], p[3]]));
                    self.info = Some(SourceInfo { first, second });
                }
                (6, false, 12) => {
                    let bytes: [u8; 4] = payload.try_into().map_err(|_| BadFrame)?;
                    self.revision_data = Some(u32::from_le_bytes(bytes));
                }
                _ => return Ok(0),
            }
            self.queries[(phase - 3) as usize] = QueryResult::Ok;
            return Ok(1);
        }
        Ok(0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pd::crc32;

    fn replay(scan: &mut ChargerCapture, line: &str, phase: u32) -> u32 {
        let mut frame = [0; 35];
        let mut len = 0;
        for byte in line.split_whitespace() {
            frame[len] = u8::from_str_radix(byte, 16).unwrap();
            len += 1;
        }
        scan.record(&frame[..len], phase)
    }

    fn contracted() -> ChargerCapture {
        let mut scan = ChargerCapture::new();
        assert_eq!(
            replay(&mut scan, include_str!("../../tests/pd_offer.hex"), 0),
            1
        );
        for (i, line) in include_str!("../../tests/charger_scan.hex")
            .lines()
            .take(2)
            .enumerate()
        {
            assert_eq!(replay(&mut scan, line, i as u32 + 1), 1);
        }
        scan
    }

    fn frame(header: u16, data: &[u8]) -> ([u8; 35], usize) {
        assert!(data.len().is_multiple_of(4));
        let mut frame = [0; 35];
        frame[0] = 0xe0;
        frame[1..3].copy_from_slice(&(header | ((data.len() / 4) as u16) << 12).to_le_bytes());
        frame[3..3 + data.len()].copy_from_slice(data);
        let crc = crc32(&frame[1..3 + data.len()]);
        frame[3 + data.len()..7 + data.len()].copy_from_slice(&crc.to_le_bytes());
        (frame, 7 + data.len())
    }

    #[test]
    fn records_each_query_without_requiring_other_optional_replies() {
        let mut scan = contracted();
        for (i, line) in include_str!("../../tests/charger_scan.hex")
            .lines()
            .skip(2)
            .enumerate()
        {
            let phase = i as u32 + 3;
            assert_eq!(replay(&mut scan, line, phase), 1);
            assert_eq!(scan.queries[i], QueryResult::Ok);
            scan.set_query_state(phase, 12); // A late retry latch cannot erase a reply.
            assert_eq!(scan.queries[i], QueryResult::Ok);
            assert_eq!(replay(&mut scan, line, phase), 0); // Source retransmit.
        }
        let ext = scan.extended.unwrap();
        assert_eq!(
            (ext.vid, ext.pid, ext.spr_pdp, ext.epr_pdp),
            (0x32ac, 0x22, 100, Some(240))
        );
        assert_eq!(scan.status.unwrap().bytes, [0, 6, 0, 0, 2, 0, 1]);
        assert_eq!(
            scan.info.unwrap(),
            SourceInfo {
                first: 0x80f0_f001,
                second: Some(0xc003_c1e0)
            }
        );
        assert_eq!(scan.revision_data, Some(0x3212_0000));

        let mut scan = contracted();
        scan.set_query_state(3, 8);
        scan.set_query_state(4, 5);
        let line = include_str!("../../tests/charger_scan.hex")
            .lines()
            .nth(4)
            .unwrap();
        assert_eq!(replay(&mut scan, line, 5), 1);
        assert_eq!(
            scan.queries,
            [
                QueryResult::NotSupported,
                QueryResult::NoReply,
                QueryResult::Ok,
                QueryResult::NotQueried
            ]
        );
    }

    #[test]
    fn query_goodcrc_requires_matching_message_id_source_and_crc() {
        for phase in 3..=6 {
            let mut scan = contracted();
            let id = phase - 2;
            let (good, len) = frame(0x0181 | (id as u16) << 9, &[]);
            assert_eq!(scan.record(&good[..len], phase), 7);
            let (wrong, len) = frame(0x0181 | (((id + 1) % 8) as u16) << 9, &[]);
            assert_eq!(scan.record(&wrong[..len], phase), 0);
            let (sink, len) = frame(0x0081 | (id as u16) << 9, &[]);
            assert_eq!(scan.record(&sink[..len], phase), 0);
            let mut bad = good;
            bad[len - 1] ^= 1;
            assert_eq!(scan.record(&bad[..len], phase), 6);
            let mut cable = good;
            cable[0] = 0xc0;
            assert_eq!(scan.record(&cable[..len], phase), 6);
        }
    }

    #[test]
    fn status_accepts_six_or_seven_bytes_and_rejects_incomplete_chunks() {
        for size in [6, 7, 26] {
            let mut scan = contracted();
            let mut payload = [0; 28];
            payload[0] = size;
            payload[1] = 0x82; // Reserved bit 9 is ignored.
            payload[2..8].copy_from_slice(&[42, 6, 0, 2, 2, 4]);
            payload[8] = 0x11; // Power state / LED, absent in the six-byte variant.
            let (reply, len) = frame(0x8982, &payload[..(size as usize + 2).next_multiple_of(4)]);
            assert_eq!(scan.record(&reply[..len], 4), 1);
            let status = scan.status.unwrap();
            assert_eq!(status.len, size.min(7));
            assert_eq!(status.bytes[0], 42);
            assert_eq!(status.bytes[6], if size == 6 { 0 } else { 0x11 });
        }
        for (size, flags) in [(5, 0x80), (7, 0x88), (7, 0x84), (7, 0x00), (27, 0x80)] {
            let mut scan = contracted();
            let mut payload = [0; 28];
            payload[0] = size;
            payload[1] = flags;
            let (reply, len) = frame(0x8982, &payload);
            assert_eq!(scan.record(&reply[..len], 4), 6);
            assert_eq!(scan.status, None);
            assert_eq!(scan.queries[1], QueryResult::NotQueried);
        }
    }

    #[test]
    fn reply_type_phase_and_length_must_match() {
        let payload = [1, 240, 240, 128];
        let (reply, len) = frame(0x098b, &payload);
        for phase in [0, 1, 2, 3, 4, 6] {
            let mut scan = contracted();
            assert_eq!(scan.record(&reply[..len], phase), 0);
            assert_eq!(scan.info, None);
        }
        let mut scan = contracted();
        assert_eq!(scan.record(&reply[..len], 5), 1);
        assert_eq!(scan.info.unwrap().second, None); // Legacy single-SIDO reply.
        let (too_many, len) = frame(0x098b, &[0; 12]);
        let mut scan = contracted();
        assert_eq!(scan.record(&too_many[..len], 5), 6);
        assert_eq!(scan.info, None);
        let (too_many, len) = frame(0x098c, &[0; 8]);
        assert_eq!(contracted().record(&too_many[..len], 6), 6);
        let (sink, len) = frame(0x088b, &payload);
        assert_eq!(contracted().record(&sink[..len], 5), 0);
    }

    #[test]
    fn negative_replies_and_reset_have_separate_outcomes() {
        for phase in 3..=6 {
            for (kind, result) in [(4, 2), (12, 3), (16, 4), (13, 5)] {
                let (reply, len) = frame(0x0980 | kind, &[]);
                let mut scan = contracted();
                assert_eq!(scan.record(&reply[..len], phase), result);
                assert!(scan.queries.iter().all(|q| *q == QueryResult::NotQueried));
            }
        }
        let mut scan = contracted();
        scan.set_state(3);
        assert_eq!(scan.queries, [QueryResult::Pd2; 4]);
        scan.set_query_state(99, 0); // FFI input outside the query phases is ignored.
        assert_eq!(scan.queries, [QueryResult::Pd2; 4]);
    }
}
