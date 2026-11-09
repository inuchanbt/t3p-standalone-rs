// SPDX-License-Identifier: GPL-2.0-or-later
use super::{BadFrame, CableCapture, CableIdentity, SOP1_TOKEN, checked_fifo};

const SOP2_TOKEN: u8 = 5;
const SVID_LIMIT: usize = 32;
pub(crate) const SVIDS_PER_SCREEN: usize = 8;

// Shared with t3p_cable_query in native/t3p_pd.h.
#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
enum Query {
    Identity,
    Svids,
    Modes,
    Manufacturer,
    Status,
    Sop2Reset,
    Sop2Status,
}

impl Query {
    fn from_raw(value: u32) -> Option<Self> {
        [
            Self::Identity,
            Self::Svids,
            Self::Modes,
            Self::Manufacturer,
            Self::Status,
            Self::Sop2Reset,
            Self::Sop2Status,
        ]
        .get(value as usize)
        .copied()
    }

    fn sop(self) -> u8 {
        match self {
            Self::Sop2Reset | Self::Sop2Status => SOP2_TOKEN,
            _ => SOP1_TOKEN,
        }
    }
}

// Shared with t3p_cable_result in native/t3p_pd.h. Ignore also represents an unqueried item.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub(crate) enum QueryResult {
    Ignore,
    Ack,
    More,
    Nak,
    Busy,
    Unsupported,
    Reject,
    Invalid,
    NoCrc,
    NoReply,
    Limit,
}

impl QueryResult {
    fn from_raw(value: u32) -> Option<Self> {
        [
            Self::Ignore,
            Self::Ack,
            Self::More,
            Self::Nak,
            Self::Busy,
            Self::Unsupported,
            Self::Reject,
            Self::Invalid,
            Self::NoCrc,
            Self::NoReply,
            Self::Limit,
        ]
        .get(value as usize)
        .copied()
    }
}

#[derive(Clone, Copy)]
pub(crate) struct VdmReply {
    pub(crate) header: u16,
    pub(crate) result: QueryResult,
    count: u8,
    words: [u32; 7],
}

impl VdmReply {
    const EMPTY: Self = Self {
        header: 0,
        result: QueryResult::Ignore,
        count: 0,
        words: [0; 7],
    };

    fn new(header: u16, payload: &[u8]) -> Self {
        let mut reply = Self::EMPTY;
        reply.header = header;
        reply.count = (payload.len() / 4) as u8;
        for (word, bytes) in reply.words.iter_mut().zip(payload.chunks_exact(4)) {
            *word = u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
        }
        reply
    }

    pub(crate) fn words(&self) -> &[u32] {
        &self.words[..usize::from(self.count)]
    }
}

#[derive(Clone, Copy)]
pub(crate) struct ExtendedReply {
    pub(crate) result: QueryResult,
    pub(crate) header: u16,
    pub(crate) ext_header: u16,
    len: u8,
    bytes: [u8; 26],
}

impl ExtendedReply {
    const EMPTY: Self = Self {
        result: QueryResult::Ignore,
        header: 0,
        ext_header: 0,
        len: 0,
        bytes: [0; 26],
    };

    fn store(&mut self, header: u16, payload: &[u8], min: usize, max: usize) -> QueryResult {
        let &[lo, hi, ref data @ ..] = payload else {
            return QueryResult::Invalid;
        };
        self.header = header;
        self.ext_header = u16::from_le_bytes([lo, hi]);
        let len = usize::from(self.ext_header & 0x1ff);
        // Cable-plug extended messages are chunked, including single-chunk replies.
        if self.ext_header & 0xfc00 != 0x8000
            || !(min..=max).contains(&len)
            || (2 + len).next_multiple_of(4) != payload.len()
        {
            return QueryResult::Invalid;
        }
        self.bytes[..len].copy_from_slice(&data[..len]);
        self.len = len as u8;
        QueryResult::Ack
    }

    pub(crate) fn bytes(&self) -> &[u8] {
        &self.bytes[..usize::from(self.len)]
    }

    pub(crate) fn maker(&self) -> &[u8] {
        let name = self.bytes().get(4..).unwrap_or_default();
        let end = name
            .iter()
            .position(|&byte| byte == 0)
            .unwrap_or(name.len());
        name[..end].trim_ascii_end()
    }
}

#[derive(Clone, Copy)]
pub(crate) struct CableDiscovery {
    scanned: bool,
    pub(crate) identity_result: QueryResult,
    pub(crate) svid_reply: VdmReply,
    svid_count: u8,
    svids: [u16; SVID_LIMIT],
    modes: [VdmReply; SVID_LIMIT],
    pub(crate) manufacturer: ExtendedReply,
    pub(crate) status: ExtendedReply,
    pub(crate) sop2_sync: QueryResult,
    pub(crate) sop2_status: ExtendedReply,
    pub(crate) sop2_rx: u8,
}

#[derive(Clone, Copy)]
pub(crate) enum CableSection {
    IdFields,
    CableFields,
    ActiveUsb,
    SvidList(usize),
    SvidRaw,
    Modes(usize),
    Manufacturer,
    Status,
}

impl CableDiscovery {
    pub(super) const fn new() -> Self {
        Self {
            scanned: false,
            identity_result: QueryResult::Ignore,
            svid_reply: VdmReply::EMPTY,
            svid_count: 0,
            svids: [0; SVID_LIMIT],
            modes: [VdmReply::EMPTY; SVID_LIMIT],
            manufacturer: ExtendedReply::EMPTY,
            status: ExtendedReply::EMPTY,
            sop2_sync: QueryResult::Ignore,
            sop2_status: ExtendedReply::EMPTY,
            sop2_rx: 0,
        }
    }

    pub(crate) fn svids(&self) -> &[u16] {
        &self.svids[..usize::from(self.svid_count)]
    }

    pub(crate) fn modes(&self, index: usize) -> &VdmReply {
        &self.modes[index]
    }

    pub(crate) fn section_count(&self, id: CableIdentity) -> u8 {
        if !self.scanned {
            return 0;
        }
        4 + self.extra_layout(id) + self.svid_pages() + self.svid_count
    }

    fn extra_layout(&self, id: CableIdentity) -> u8 {
        if id.vdo().is_none() {
            0
        } else if id.pd2() {
            1
        } else if id.active() {
            2
        } else {
            0
        }
    }

    fn svid_pages(&self) -> u8 {
        self.svids().len().div_ceil(SVIDS_PER_SCREEN).max(1) as u8
    }

    pub(crate) fn section(&self, id: CableIdentity, index: u8) -> Option<CableSection> {
        if !self.scanned {
            return None;
        }
        let layout = self.extra_layout(id);
        let pages = self.svid_pages();
        let svid_raw = 1 + layout + pages;
        let manufacturer = svid_raw + 1 + self.svid_count;
        Some(match index {
            0 => CableSection::IdFields,
            1 if layout >= 1 => CableSection::CableFields,
            2 if layout == 2 => CableSection::ActiveUsb,
            n if n < svid_raw => CableSection::SvidList(usize::from(n - 1 - layout)),
            n if n == svid_raw => CableSection::SvidRaw,
            n if n < manufacturer => CableSection::Modes(usize::from(n - svid_raw - 1)),
            n if n == manufacturer => CableSection::Manufacturer,
            n if n == manufacturer + 1 => CableSection::Status,
            _ => return None,
        })
    }

    fn add_svids(&mut self, reply: VdmReply) -> QueryResult {
        let mut result = QueryResult::Invalid;
        if reply.words().len() > 1 {
            for &word in &reply.words()[1..] {
                for svid in [(word >> 16) as u16, word as u16] {
                    if svid == 0 {
                        return QueryResult::Ack;
                    }
                    if let Err(index) = self.svids().binary_search(&svid) {
                        let count = usize::from(self.svid_count);
                        if count == SVID_LIMIT {
                            return QueryResult::Limit;
                        }
                        let mut next = svid;
                        for slot in &mut self.svids[index..=count] {
                            core::mem::swap(slot, &mut next);
                        }
                        self.svid_count += 1;
                    }
                }
            }
            if reply.words().len() == 7 {
                result = QueryResult::More;
            }
        }
        result
    }
}

impl CableCapture {
    // A read outcome and its validated bytes are stored independently. This keeps
    // earlier pages inspectable if a later query times out or the bus is stopped.
    pub fn record_query_result(&mut self, query: u32, index: u32, result: u32) {
        let (Some(query), Some(result)) = (Query::from_raw(query), QueryResult::from_raw(result))
        else {
            return;
        };
        let d = &mut self.discovery;
        d.scanned = true;
        match query {
            Query::Identity => d.identity_result = result,
            Query::Svids => d.svid_reply.result = result,
            Query::Modes => {
                if let Some(reply) = d.modes.get_mut(index as usize) {
                    reply.result = result;
                }
            }
            Query::Manufacturer => d.manufacturer.result = result,
            Query::Status => d.status.result = result,
            Query::Sop2Reset => d.sop2_sync = result,
            Query::Sop2Status => d.sop2_status.result = result,
        }
    }

    /// Flags and negotiated revision/version for the native cable reader.
    #[cfg(any(feature = "board", test))]
    pub(crate) fn query_parameters(&self) -> u32 {
        let Some(id) = self.identity else {
            return 0;
        };
        let revision = u32::from(id.header & 0xc0);
        let svdm = if revision == 0x80 && id.words[0] & 0x6000 == 0x2000 {
            id.words[0] & 0x7800
        } else {
            0
        };
        let sop2 = id.vdo().is_some_and(|vdo| vdo.sop2_present);
        revision | svdm | u32::from(id.modal()) | u32::from(id.active()) << 1 | u32::from(sop2) << 2
    }

    #[cfg(any(feature = "board", test))]
    pub(crate) fn query_svid(&self, index: usize) -> Option<u16> {
        self.discovery.svids().get(index).copied()
    }

    /// Parse one complete FUSB302 FIFO record for an outstanding cable query.
    /// Result values follow `t3p_cable_result` in the native reader.
    pub fn record_query_reply(
        &mut self,
        query: u32,
        index: u32,
        frame: &[u8],
    ) -> Result<u32, BadFrame> {
        let query = Query::from_raw(query).ok_or(BadFrame)?;
        self.parse_reply(query, index as usize, frame)
            .map(|result| result as u32)
    }

    fn parse_reply(
        &mut self,
        query: Query,
        index: usize,
        frame: &[u8],
    ) -> Result<QueryResult, BadFrame> {
        let (lo, hi, payload) = checked_fifo(frame, query.sop())?;
        let d = &mut self.discovery;
        if query.sop() == SOP1_TOKEN {
            self.sop_prime = self.sop_prime.saturating_add(1);
        } else {
            d.sop2_rx = d.sop2_rx.saturating_add(1);
        }
        let header = u16::from_le_bytes([lo, hi]);
        let message = lo & 0x1f;
        if hi & 1 == 0 || !matches!(lo & 0xc0, 0x40 | 0x80) {
            return Ok(QueryResult::Ignore);
        }
        if payload.is_empty() && hi & 0x80 == 0 {
            return Ok(match message {
                3 if query == Query::Sop2Reset && hi & 0x0e == 0 => QueryResult::Ack,
                4 => QueryResult::Reject,
                16 => QueryResult::Unsupported,
                _ => QueryResult::Ignore,
            });
        }
        let extended = match query {
            Query::Manufacturer => Some((7, &mut d.manufacturer, 4, 26)),
            Query::Status => Some((2, &mut d.status, 2, 2)),
            Query::Sop2Status => Some((2, &mut d.sop2_status, 2, 2)),
            _ => None,
        };
        if let Some((expected, reply, min, max)) = extended {
            return Ok(if hi & 0x80 != 0 && message == expected {
                let result = reply.store(header, payload, min, max);
                if result == QueryResult::Ack
                    && query == Query::Manufacturer
                    && reply.maker().eq_ignore_ascii_case(b"Not Supported")
                {
                    QueryResult::Unsupported
                } else {
                    result
                }
            } else {
                QueryResult::Ignore
            });
        }
        if query == Query::Sop2Reset || hi & 0x80 != 0 || message != 15 || payload.len() < 4 {
            return Ok(QueryResult::Ignore);
        }
        let reply = VdmReply::new(header, payload);
        let vdm = reply.words[0];
        let svid = match query {
            Query::Modes => *d.svids().get(index).ok_or(BadFrame)?,
            _ => 0xff00,
        };
        let expected = u32::from(svid) << 16 | 0x8000 | (query as u32 + 1);
        if vdm & 0xffff_871f != expected {
            return Ok(QueryResult::Ignore);
        }
        let result = match (vdm >> 6) & 3 {
            1 => QueryResult::Ack,
            2 => QueryResult::Nak,
            3 => QueryResult::Busy,
            _ => return Ok(QueryResult::Ignore),
        };
        if result != QueryResult::Ack && reply.count != 1 {
            return Ok(QueryResult::Invalid);
        }
        Ok(match query {
            Query::Identity if result == QueryResult::Ack => {
                self.identity = CableIdentity::from_ack(header, payload);
                if self.captured() {
                    QueryResult::Ack
                } else {
                    QueryResult::Invalid
                }
            }
            Query::Svids => {
                d.svid_reply = reply;
                if result == QueryResult::Ack {
                    d.add_svids(reply)
                } else {
                    result
                }
            }
            Query::Modes => {
                d.modes[index] = reply;
                if result == QueryResult::Ack && reply.count < 2 {
                    QueryResult::Invalid
                } else {
                    result
                }
            }
            _ => result,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pd::crc32;

    fn fifo(token: u8, header: u16, data: &[u32]) -> [u8; 35] {
        let mut frame = [0; 35];
        frame[0] = token << 5;
        let header = header | ((data.len() as u16) << 12);
        frame[1..3].copy_from_slice(&header.to_le_bytes());
        let end = 3 + data.len() * 4;
        for (slot, word) in frame[3..end].chunks_exact_mut(4).zip(data) {
            slot.copy_from_slice(&word.to_le_bytes());
        }
        let crc = crc32(&frame[1..end]);
        frame[end..end + 4].copy_from_slice(&crc.to_le_bytes());
        frame
    }

    fn reply(
        cable: &mut CableCapture,
        query: Query,
        index: usize,
        header: u16,
        data: &[u32],
    ) -> QueryResult {
        let frame = fifo(query.sop(), header, data);
        let result = cable
            .parse_reply(query, index, &frame[..7 + data.len() * 4])
            .unwrap();
        if result != QueryResult::Ignore {
            cable.record_query_result(query as u32, index as u32, result as u32);
        }
        result
    }

    #[test]
    fn svid_continuations_are_sorted_deduplicated_and_bounded() {
        let mut cable = CableCapture::new();
        let svid_reply = |cable: &mut CableCapture, start: u32| {
            let mut words = [0xff00_a042; 7];
            for (i, word) in words[1..].iter_mut().enumerate() {
                let first = start - i as u32 * 2;
                *word = first << 16 | (first - 1);
            }
            reply(cable, Query::Svids, 0, 0x018f, &words)
        };
        assert_eq!(svid_reply(&mut cable, 12), QueryResult::More);
        assert_eq!(svid_reply(&mut cable, 23), QueryResult::More);
        assert_eq!(cable.query_svid(0), Some(1));
        assert_eq!(cable.query_svid(11), Some(12));
        assert_eq!(cable.query_svid(22), Some(23));
        assert_eq!(cable.query_svid(23), None);
        assert_eq!(svid_reply(&mut cable, 35), QueryResult::Limit);
        assert_eq!(cable.discovery.svids().len(), SVID_LIMIT);
        assert!(cable.discovery.svids().is_sorted());
        assert_eq!(
            reply(&mut cable, Query::Modes, 0, 0x018f, &[0x0002_a043, 1]),
            QueryResult::Ignore
        );
        assert_eq!(
            reply(&mut cable, Query::Modes, 0, 0x018f, &[0x0001_a043, 2, 1]),
            QueryResult::Ack
        );
        assert_eq!(cable.discovery.modes(0).words(), &[0x0001_a043, 2, 1]);
    }

    #[test]
    fn active_status_requires_the_cable_plug_and_a_complete_reply() {
        let mut cable = CableCapture::new();
        let vdo1 = 0x466a_3e5b;
        let vdo2 = 0x501e_0000;
        assert_eq!(
            reply(
                &mut cable,
                Query::Identity,
                0,
                0x018f,
                &[0xff00_a041, 0x2060_20c2, 0, 0x0a14_0310, vdo1, vdo2]
            ),
            QueryResult::Ack
        );
        assert_eq!(cable.query_parameters(), 0x2086);

        // A reply with the wrong SOP token must not become second-plug status.
        let frame = fifo(SOP1_TOKEN, 0x8182, &[0x012a_8002]);
        assert_eq!(
            cable.parse_reply(Query::Sop2Status, 0, &frame[..11]),
            Err(BadFrame)
        );
        assert_eq!(
            reply(&mut cable, Query::Sop2Reset, 0, 0x0383, &[]),
            QueryResult::Ignore
        );
        assert_eq!(
            reply(&mut cable, Query::Sop2Reset, 0, 0x0183, &[]),
            QueryResult::Ack
        );
        assert_eq!(
            reply(&mut cable, Query::Sop2Status, 0, 0x8382, &[0x012a_8802]),
            QueryResult::Invalid
        );
        assert!(cable.discovery.sop2_status.bytes().is_empty());
        assert_eq!(
            reply(&mut cable, Query::Sop2Status, 0, 0x8382, &[0x012a_8002]),
            QueryResult::Ack
        );
        assert_eq!(cable.discovery.sop2_status.bytes(), &[42, 1]);
        assert_eq!(
            reply(&mut cable, Query::Manufacturer, 0, 0x0384, &[]),
            QueryResult::Reject
        );
        assert_eq!(cable.identity.unwrap().words()[4..], [vdo1, vdo2]);
    }
}
