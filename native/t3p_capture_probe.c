/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_capture_probe.h"
void t3p_capture_probe_read(struct t3p_bsp *b, const struct t3p_boot_snapshot *boot,
                           const uint32_t parameters[4], uint32_t out[36])
{
    for (unsigned i=0;i<36;i++) out[i]=0;
    for (unsigned i=0;i<4;i++) out[i]=parameters[i];
    if (boot->valid == T3P_BOOT_MAGIC) {
        out[4]=1;
        for (unsigned i=0;i<6;i++) out[5+i]=boot->context[i];
        if (boot->auxiliary_valid == 1) {
            out[4]|=2;
            for (unsigned i=0;i<8;i++) {
                const uint8_t *p=boot->auxiliary+i*4;
                out[11+i]=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
            }
        }
        if (boot->header_valid == 1) {
            out[4]|=4;
            for (unsigned i=0;i<8;i++) {
                const uint8_t *p=boot->header+i*4;
                out[27+i]=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
            }
        }
    }
    /* BR35 SFC RW registers, matching the pinned SDK br35.h.
     * Deliberately excludes write-only DEC_KEY at +0x14 and user commands.
     * This function has no writes, cache changes, ROM calls or SPI commands. */
    static const uint8_t offsets[]={0,4,16,24,28,32,36};
    for (unsigned i=0;i<7;i++) out[19+i]=b->io.read32(b->io.context,0xfe0200u+offsets[i]);
}
