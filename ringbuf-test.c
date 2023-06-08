/*
 * test-ringbuf.c - unit tests for C ring buffer implementation.
 *
 * Written in 2011 by Drew Hess <dhess-src@bothan.net>.
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to
 * the public domain worldwide. This software is distributed without
 * any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication
 * along with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include "ringbuf.h"


/* Default size for these tests. */
#define RINGBUF_SIZE 4096

int main (int argc, char **argv) {

    ringbuf_t rb1 = ringbuf_new(RINGBUF_SIZE - 1);

    g_assert(ringbuf_is_empty(rb1));

    /* Fill the buffer. */
    guint8 buf[RINGBUF_SIZE];
    while (ringbuf_bytes_free(rb1)) {
        *p++ = 0x42;
        ringbuf_memcpy_into(rb1, p, 1);
    }
    
        
    ringbuf_free(&rb1);
    return 0;
}
