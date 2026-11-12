/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_CAPTURE_STORE_H
#define T3P_CAPTURE_STORE_H
#include <stdint.h>
#include "t3p_bsp.h"
#define T3P_CAPTURE_START 0x90000u
#define T3P_CAPTURE_BYTES 0xc0000u
#define T3P_CAPTURE_SLOT 0x20000u
/* 0 ready; nonzero is a real lock/fault, never an invented success. */
extern uint32_t t3p_capture_status, t3p_capture_jedec, t3p_capture_sfdp;
int t3p_capture_store_init(struct t3p_bsp *clock);
int t3p_capture_store_prepare(void);
int t3p_capture_store_read(uint32_t offset, uint8_t *out, uint32_t count);
int t3p_capture_store_export_read(uint32_t offset, uint8_t *out, uint32_t count);
int t3p_capture_store_program(uint32_t offset, const uint8_t *bytes, uint32_t count);
int t3p_capture_store_flush(void);
#endif
