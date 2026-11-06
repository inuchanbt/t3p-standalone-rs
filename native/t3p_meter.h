/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_METER_H
#define T3P_METER_H
#include <stdint.h>
#include "t3p_bsp.h"

struct t3p_meter_bus {
    void *context;
    void (*scl)(void *, int);
    void (*sda)(void *, int);
    int (*read_scl)(void *);
    int (*read_sda)(void *);
    int (*delay)(void *);
};

void t3p_meter_gpio_init(struct t3p_bsp *, struct t3p_meter_bus *);
int t3p_meter_read_register(struct t3p_meter_bus *, uint8_t, uint16_t *);
int t3p_pd_read_register(struct t3p_meter_bus *, uint8_t, uint8_t *);
int t3p_meter_identify(struct t3p_meter_bus *);
int t3p_meter_sample(struct t3p_meter_bus *, uint16_t *, int16_t *);
#endif
