/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_BUTTON_H
#define T3P_BUTTON_H
#include <stdint.h>

enum t3p_button_event { T3P_BUTTON_NONE, T3P_BUTTON_SHORT, T3P_BUTTON_LONG };
struct t3p_button {
    uint32_t changed, pressed_at;
    unsigned raw, stable, long_sent;
};
static inline void t3p_button_init(struct t3p_button *b, uint32_t now, unsigned pressed)
{
    b->changed = b->pressed_at = now;
    b->raw = b->stable = pressed;
    b->long_sent = pressed; /* A held boot key must not trigger an action. */
}
static inline enum t3p_button_event t3p_button_poll(struct t3p_button *b, uint32_t now, unsigned pressed)
{
    if (pressed != b->raw) { b->raw = pressed; b->changed = now; }
    if (b->stable != b->raw && now - b->changed >= 30u) {
        b->stable = b->raw;
        if (b->stable) { b->pressed_at = now; b->long_sent = 0; }
        else if (!b->long_sent) {
            b->long_sent = 1;
            return now - b->pressed_at >= 1000u ? T3P_BUTTON_LONG : T3P_BUTTON_SHORT;
        }
    }
    if (b->stable && !b->long_sent && now - b->pressed_at >= 1000u) {
        b->long_sent = 1;
        return T3P_BUTTON_LONG;
    }
    return T3P_BUTTON_NONE;
}
#endif
