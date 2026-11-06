/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stddef.h>

/* Freestanding implementations of the memory operations emitted by LLVM. */
void *memset(void *to, int byte, size_t len)
{
    unsigned char *out = to;
    while (len--) *out++ = (unsigned char)byte;
    return to;
}

void *memcpy(void *restrict to, const void *restrict from, size_t len)
{
    unsigned char *out = to;
    const unsigned char *in = from;
    while (len--) *out++ = *in++;
    return to;
}
