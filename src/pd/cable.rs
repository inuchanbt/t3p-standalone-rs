// SPDX-License-Identifier: GPL-2.0-or-later
use super::{BadFrame, SOP_TOKEN, SOP1_TOKEN, checked_fifo};

/// Decode SOP' Discover Identity replies from a passive or active read.
/// Ordinary SOP frames can still be delivered while SOP' reception is enabled.
#[derive(Clone, Copy, Default)]
pub struct CableCapture {
    pub(crate) identity: Option<CableIdentity>,
    pub(crate) sop_prime: u8,
}

impl CableCapture {
    pub const fn new() -> Self {
        Self {
            identity: None,
            sop_prime: 0,
        }
    }

    pub fn captured(&self) -> bool {
        self.identity.is_some()
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
        let vdm = u32::from_le_bytes([payload[0], payload[1], payload[2], payload[3]]);
        // USB-IF SVID, structured Discover Identity ACK, object position zero.
        if vdm & 0xffff_87df != 0xff00_8041 || payload.len() < 20 {
            return Ok(false);
        }
        let mut words = [0; 7];
        for (word, bytes) in words.iter_mut().zip(payload.chunks_exact(4)) {
            *word = u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
        }
        let count = (payload.len() / 4) as u8;
        match (words[1] >> 27) & 7 {
            3 => {}
            // SVDM 2.x active cables return two Cable VDOs.
            4 if count >= 6 || vdm & 0x6000 == 0 => {}
            _ => return Ok(false),
        }
        self.identity = Some(CableIdentity {
            header: u16::from_le_bytes([lo, hi]),
            words,
            count,
        });
        Ok(true)
    }
}

#[derive(Clone, Copy)]
pub(crate) struct CableIdentity {
    pub(crate) header: u16,
    words: [u32; 7],
    count: u8,
}

impl CableIdentity {
    pub(crate) fn words(&self) -> &[u32] {
        &self.words[..usize::from(self.count)]
    }

    pub(crate) fn active(self) -> bool {
        (self.words[1] >> 27) & 7 == 4
    }

    /// Decode the known PD 3.x SVDM 2.x Cable VDO1 layouts. Older layouts
    /// and other VDO versions remain available as raw words.
    pub(crate) fn vdo(self) -> Option<CableVdo> {
        let word = self.words[4];
        let version = (word >> 21) & 7;
        if self.header & 0xc0 != 0x80
            || self.words[0] & 0x6000 != 0x2000
            || version != if self.active() { 3 } else { 0 }
        {
            return None;
        }
        Some(CableVdo {
            hardware: (word >> 28) as u8,
            firmware: ((word >> 24) & 15) as u8,
            version: version as u8,
            plug: ((word >> 18) & 3) as u8,
            epr: word & (1 << 17) != 0,
            latency: ((word >> 13) & 15) as u8,
            termination: ((word >> 11) & 3) as u8,
            voltage: ((word >> 9) & 3) as u8,
            current: ((word >> 5) & 3) as u8,
            vbus_through: word & (1 << 4) != 0,
            sop2_present: word & (1 << 3) != 0,
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
