/* Device acquisition, NOT the manufacturer transcript.
 * SFDP Probe V1, 2026-11-15 23:19:27 JST, JEDEC 856015.
 * Two independent SPI passes and two matching USB acquisitions, all advertised
 * tables covered. Private 4096-byte snapshot SHA-256:
 * 0ea357f52b1e02511d347995cec9226e056e5b4b65172471f00f7896faac7549.
 * Header equals puya_header; BFPT at 0x30, 9 DWORDs. Only the capability table
 * is transcribed here. Uninterpreted vendor data and address gaps stay private.
 * BFPT SHA-256:
 * 2e71d6dbc5696d1ec4fb214ee353f4329d31f8dd1827c10b625bc6b40d57b079.
 */
static const uint8_t t3p_856015_bfpt[]={
    0xe5,0x20,0xf9,0xff,0xff,0xff,0xff,0x00,
    0x44,0xeb,0x08,0x6b,0x08,0x3b,0x80,0xbb,
    0xfe,0xff,0xff,0xff,0xff,0xff,0x00,0xff,
    0xff,0xff,0x44,0xeb,0x0c,0x20,0x0f,0x52,
    0x10,0xd8,0x08,0x81
};
