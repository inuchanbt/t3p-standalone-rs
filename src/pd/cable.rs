// SPDX-License-Identifier: GPL-2.0-or-later
use super::{BadFrame, SOP_TOKEN, SOP1_TOKEN, checked_fifo};

#[cfg(feature = "pd-cable-read")]
mod active;
#[cfg(feature = "pd-cable-read")]
pub(crate) use active::{
    CableDiscovery, CableSection, ExtendedReply, QueryResult, SVIDS_PER_SCREEN, VdmReply,
};

const IDENTITY_SECTIONS: u8 = 3;

/// Decode SOP' Discover Identity replies from a passive or active read.
/// Ordinary SOP frames can still be delivered while SOP' reception is enabled.
#[derive(Clone, Copy)]
pub struct CableCapture {
    pub(crate) identity: Option<CableIdentity>,
    pub(crate) sop_prime: u8,
    #[cfg(feature = "pd-cable-read")]
    pub(crate) cc_probe: Option<[u8; 2]>,
    #[cfg(feature = "pd-cable-read")]
    pub(crate) discovery: active::CableDiscovery,
}

impl Default for CableCapture {
    fn default() -> Self {
        Self::new()
    }
}

impl CableCapture {
    pub const fn new() -> Self {
        Self {
            identity: None,
            sop_prime: 0,
            #[cfg(feature = "pd-cable-read")]
            cc_probe: None,
            #[cfg(feature = "pd-cable-read")]
            discovery: active::CableDiscovery::new(),
        }
    }

    /// First DAC threshold above each CC pin. Code 64 means above range.
    #[cfg(feature = "pd-cable-read")]
    pub fn set_cc_probe(&mut self, cc1: u32, cc2: u32) {
        self.cc_probe = (cc1 <= 64 && cc2 <= 64).then_some([cc1 as u8, cc2 as u8]);
    }

    pub fn captured(&self) -> bool {
        self.identity.is_some()
    }

    pub(crate) fn sections(&self) -> u8 {
        if !self.captured() {
            return 1;
        }
        #[cfg(feature = "pd-cable-read")]
        if let Some(identity) = self.identity {
            return IDENTITY_SECTIONS + self.discovery.section_count(identity);
        }
        IDENTITY_SECTIONS
    }

    #[cfg(feature = "pd-cable-read")]
    pub(crate) fn extra_section(&self, section: u8) -> Option<CableSection> {
        self.discovery
            .section(self.identity?, section.checked_sub(IDENTITY_SECTIONS)?)
    }

    /// Returns true when the first complete cable Identity ACK is captured.
    pub fn push(&mut self, frame: &[u8]) -> Result<bool, BadFrame> {
        if frame.first().is_some_and(|token| token >> 5 == SOP_TOKEN) {
            checked_fifo(frame, SOP_TOKEN)?;
            return Ok(false);
        }
        let (lo, hi, payload) = checked_fifo(frame, SOP1_TOKEN)?;
        self.sop_prime = self.sop_prime.saturating_add(1);
        if self.captured() || hi & 0x81 != 1 || lo & 0x1f != 15 || payload.len() < 4 {
            return Ok(false);
        }
        self.identity = CableIdentity::from_ack(u16::from_le_bytes([lo, hi]), payload);
        Ok(self.captured())
    }
}

#[derive(Clone, Copy)]
pub(crate) struct CableIdentity {
    pub(crate) header: u16,
    words: [u32; 7],
    count: u8,
}

impl CableIdentity {
    fn from_ack(header: u16, payload: &[u8]) -> Option<Self> {
        if !(20..=28).contains(&payload.len()) {
            return None;
        }
        let mut words = [0; 7];
        for (word, bytes) in words.iter_mut().zip(payload.chunks_exact(4)) {
            *word = u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
        }
        // USB-IF SVID, structured Discover Identity ACK, object position zero.
        if words[0] & 0xffff_87df != 0xff00_8041 {
            return None;
        }
        let count = (payload.len() / 4) as u8;
        match (words[1] >> 27) & 7 {
            3 => {}
            4 if count >= 6 || words[0] & 0x6000 == 0 => {}
            _ => return None,
        }
        Some(Self {
            header,
            words,
            count,
        })
    }

    pub(crate) fn words(&self) -> &[u32] {
        &self.words[..usize::from(self.count)]
    }

    pub(crate) fn active(self) -> bool {
        (self.words[1] >> 27) & 7 == 4
    }

    pub(crate) fn pd2(self) -> bool {
        self.header & 0xc0 == 0x40
    }

    #[cfg(feature = "pd-cable-read")]
    pub(crate) fn modal(self) -> bool {
        self.words[1] & (1 << 26) != 0
    }

    /// Decode PD 2.0 and the known PD 3.x Cable VDO layouts.
    pub(crate) fn vdo(self) -> Option<CableVdo> {
        let word = self.words[4];
        let version = (word >> 21) & 7;
        let svdm = self.words[0] & 0x6000;
        let legacy = svdm == 0 && self.pd2();
        if !legacy {
            let expected_version = if self.active() { 3 } else { 0 };
            if self.header & 0xc0 != 0x80
                || svdm != 0x2000
                || version != expected_version
                || (self.active() && self.count < 6)
            {
                return None;
            }
        }
        Some(CableVdo {
            hardware: (word >> 28) as u8,
            firmware: ((word >> 24) & 15) as u8,
            version: version as u8,
            plug: ((word >> 18) & 3) as u8,
            epr: !legacy && word & (1 << 17) != 0,
            latency: ((word >> 13) & 15) as u8,
            termination: ((word >> 11) & 3) as u8,
            voltage: ((word >> 9) & 3) as u8,
            current: ((word >> 5) & 3) as u8,
            vbus_through: word & (1 << 4) != 0,
            sop2_present: self.active() && word & (1 << 3) != 0,
            speed: (word & 7) as u8,
        })
    }
}

#[derive(Clone, Copy)]
pub(crate) struct CableVdo {
    pub(crate) hardware: u8,
    pub(crate) firmware: u8,
    pub(crate) version: u8,
    pub(crate) plug: u8,
    pub(crate) epr: bool,
    pub(crate) latency: u8,
    pub(crate) termination: u8,
    pub(crate) voltage: u8,
    pub(crate) current: u8,
    pub(crate) vbus_through: bool,
    pub(crate) sop2_present: bool,
    pub(crate) speed: u8,
}
