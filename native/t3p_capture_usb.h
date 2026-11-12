/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_CAPTURE_USB_H
#define T3P_CAPTURE_USB_H
#include "t3p_bsp.h"
int t3p_capture_usb_start(struct t3p_bsp *);
void t3p_capture_usb_poll(void);
void t3p_capture_usb_request(const uint8_t q[64],uint8_t a[64]);
extern int32_t t3p_capture_result;
#endif
