/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_BUTTON_H
#define T3P_BUTTON_H
#include <stdint.h>

enum t3p_button_event { T3P_BUTTON_NONE, T3P_BUTTON_SHORT, T3P_BUTTON_DOUBLE, T3P_BUTTON_LONG };
struct t3p_button {
    uint32_t changed, pressed_at, released_at;
    unsigned raw, stable, long_sent, pending_short, second_press;
};
static inline void t3p_button_init(struct t3p_button *b, uint32_t now, unsigned pressed)
{
    b->changed = b->pressed_at = now;
    b->raw = b->stable = pressed;
    b->long_sent = pressed; /* A held boot key must not trigger an action. */
    b->released_at = now;
    b->pending_short = b->second_press = 0;
}
static inline enum t3p_button_event t3p_button_poll(struct t3p_button *b, uint32_t now, unsigned pressed)
{
    if (pressed != b->raw) { b->raw = pressed; b->changed = now; }
    if (b->stable != b->raw && now - b->changed >= 30u) {
        b->stable = b->raw;
        if (b->stable) {
            b->pressed_at = now;
            b->long_sent = 0;
            b->second_press = 0;
            if (b->pending_short) {
                b->pending_short = 0;
                if (now - b->released_at < 350u) b->second_press = 1;
                else return T3P_BUTTON_SHORT;
            }
        } else if (!b->long_sent) {
            if (b->changed - b->pressed_at >= 1000u) {
                b->second_press = b->pending_short = 0;
                return T3P_BUTTON_LONG;
            }
            if (b->second_press) {
                b->second_press = 0;
                return T3P_BUTTON_DOUBLE;
            }
            b->pending_short = 1;
            b->released_at = now;
        }
    }
    if (b->stable && b->raw && !b->long_sent && now - b->pressed_at >= 1000u) {
        b->long_sent = 1;
        b->pending_short = b->second_press = 0;
        return T3P_BUTTON_LONG;
    }
    if (b->pending_short && !b->stable && !b->raw && now - b->released_at >= 350u) {
        b->pending_short = 0;
        return T3P_BUTTON_SHORT;
    }
    return T3P_BUTTON_NONE;
}
#endif
