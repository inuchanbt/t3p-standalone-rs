// SPDX-License-Identifier: GPL-2.0-or-later
use super::{Strip, BG, AMPS, TEXT, MUTED, VOLTS, GRID};
use super::cable::hex;
use crate::{SIDE, STRIP_ROWS};
pub fn render(words: &[u32; 8], top: usize, pixels: &mut [u16; SIDE * STRIP_ROWS]) {
    pixels.fill(BG);
    if top >= SIDE { return; }
    let mut p=Strip { pixels, top };
    p.text(8,8,b"FLASH READER V1",2,AMPS);
    p.text(8,30,b"CPU MAPPED VIEW",1,VOLTS);
    let labels:[&[u8];8]=[b"MAPPING",b"USB CONFIG",b"REQUESTS",b"READ ERRORS",b"USB RESETS",b"USB STALLS",b"PHYS START",b"VIEW BYTES"];
    for (i,label) in labels.iter().enumerate() {
        let y=52+i*18;p.text(8,y,label,1,MUTED);
        if i==0 {p.text(132,y,if words[0]==1 {b"OK"} else {b"LOCKED"},1,VOLTS);}
        else {p.text(132,y,&hex::<8>(words[i]),1,TEXT);}
        p.rect(8,y+12,224,1,GRID);
    }
    p.text(8,205,b"READ ONLY - USB HID",1,AMPS);
    p.text(8,219,b"PC TOOL: flash_read.py",1,TEXT);
    p.text(8,233,b"NOT A RAW FLASH BACKUP",1,VOLTS);
}
