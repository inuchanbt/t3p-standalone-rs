/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_TEMPERATURE_PROBE_H
#define T3P_TEMPERATURE_PROBE_H
#include "t3p_temperature.h"
#define T3P_TEMP_PROBE_MAGIC 0x32525054u /* TPR2 */
#define T3P_TEMP_PROBE_COUNT 8u
#define T3P_TEMP_PROBE_WORDS 970u
struct t3p_temperature_probe_record {
    /* port, seven-bit address, reg, bytes, signed transport result, value valid,
       observed big-endian value, start ms, finish ms, PA restore (0 N/A,1 OK,2 fail) */
    uint32_t meta[10];
    struct t3p_temperature_diag trace;
};
struct t3p_temperature_probe_snapshot {
    /* magic, version, words, record words, count, completed, generation,
       acquisition enabled, fast timer ready, halted after restore failure */
    uint32_t header[10];
    struct t3p_temperature_probe_record records[T3P_TEMP_PROBE_COUNT];
};
_Static_assert(sizeof(struct t3p_temperature_probe_record)==120*4,"Probe record ABI");
_Static_assert(sizeof(struct t3p_temperature_probe_snapshot)==T3P_TEMP_PROBE_WORDS*4,"Probe snapshot ABI");
int t3p_temperature_probe_target(unsigned, uint32_t [4]);
int t3p_temperature_probe_sample(struct t3p_bsp *,struct t3p_meter_bus *,unsigned,
                                 struct t3p_temperature_probe_record *);
void t3p_temperature_probe_init(unsigned timer_ready);
void t3p_temperature_probe_poll(struct t3p_bsp *,struct t3p_meter_bus *,uint32_t now,unsigned enabled);
int t3p_temperature_probe_page(uint32_t,uint32_t [10]);
#endif
