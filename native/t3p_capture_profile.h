/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_CAPTURE_PROFILE_H
#define T3P_CAPTURE_PROFILE_H
#include "t3p_bsp.h"
#define T3P_CAPTURE_PROFILE_MAGIC 0x31525043u /* CPR1 */
enum {
    CAP_NOTICE=4,CAP_PREPARE,CAP_JOURNAL,CAP_RENDER,CAP_ENCODE,CAP_FINISH,
    CAP_ERASE_MS,CAP_BLANK_MS,CAP_PROGRAM_MS,CAP_READBACK_MS,CAP_READ_MS,
    CAP_ERASE_OPS,CAP_PROGRAM_OPS,CAP_BLANK_BYTES,CAP_READ_BYTES,CAP_PROGRAM_BYTES,
    CAP_READBACK_BYTES,CAP_READ_TRANSACTIONS,CAP_PROGRAM_TRANSACTIONS,
    CAP_BLANK_CLAIMS,CAP_READ_CALLS,CAP_PROGRAM_CALLS,CAP_RENDER_CALLS
};
void t3p_capture_profile_init(struct t3p_bsp *clock);
void t3p_capture_profile_begin(void);
void t3p_capture_profile_phase(uint32_t phase);
void t3p_capture_profile_end(int32_t result);
uint32_t t3p_capture_profile_stamp(void);
void t3p_capture_profile_add(uint32_t metric,uint32_t value);
/* Three cached pages of ten words. Never samples a clock or touches flash. */
int t3p_capture_profile_page(uint32_t page,uint32_t out[10]);
#endif
