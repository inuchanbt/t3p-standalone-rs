/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_FLASH_READER_H
#define T3P_FLASH_READER_H
#include "t3p_boot.h"
#include "t3p_bsp.h"
/* CPU mapped view only; no raw SPI, SFC writes, erase or program interface. */
struct t3p_flash_reader {
    uint32_t cpu_base, physical_base, length, con0, offset, ndec_start, ndec_end;
    uint32_t requests, errors;
    void *context;
    int (*read)(void *, uint32_t, uint8_t *, uint32_t);
    uint8_t ready;
};
int t3p_flash_reader_init(struct t3p_flash_reader *,const struct t3p_boot_snapshot *,
                        struct t3p_bsp *,void *,int (*)(void *,uint32_t,uint8_t *,uint32_t));
void t3p_flash_reader_request(struct t3p_flash_reader *,const uint8_t[64],uint8_t[64]);
void t3p_flash_usb_run(struct t3p_bsp *);
#endif
