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
}

impl SourceExtended {
    fn decode(payload: &[u8]) -> Result<Self, BadFrame> {
        let [lo, hi, body @ ..] = payload else {
            return Err(BadFrame);
        };
        let header = u16::from_le_bytes([*lo, *hi]);
        let size = usize::from(header & 0x01ff);
        // With no unchunked support advertised, expect the complete SCEDB
        // in data chunk zero, with padding to the data-object boundary.
        if header & 0xfe00 != 0x8000
            || !(24..=26).contains(&size)
            || (size + 2).next_multiple_of(4) != payload.len()
        {
            return Err(BadFrame);
        }
        let data = &body[..size];
        Ok(Self {
            vid: u16::from_le_bytes([data[0], data[1]]),
            pid: u16::from_le_bytes([data[2], data[3]]),
            xid: u32::from_le_bytes([data[4], data[5], data[6], data[7]]),
            fw: data[8],
            hw: data[9],
            spr_pdp: data[23],
            epr_pdp: data.get(24).copied(),
        })
    }
}

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
            last_source_id: None,
        }
    }

    pub fn set_state(&mut self, state: u32) {
        self.state = state;
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
        if hi & 1 == 0 || (control && kind == 1) {
            return Ok(0); // Sink records and GoodCRC do not advance the source MessageID.
        }
        if control && kind == 13 {
            return Ok(5); // Soft Reset invalidates the short transaction.
        }
        let id = (hi >> 1) & 7;
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
                (1 | 3, 4) => 2,
                (1 | 3, 12) => 3,
                (1 | 3, 16) => 4,
                _ => 0,
            });
        }
        if phase == 3 && self.ps_rdy && hi & 0x80 != 0 && kind == 1 {
            self.extended = Some(SourceExtended::decode(payload)?);
            return Ok(1);
        }
        Ok(0)
    }
}
