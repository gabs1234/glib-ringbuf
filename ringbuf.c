/*
 * ringbuf.c - C ring buffer (FIFO) implementation.
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

struct ringbuf_t {
    guint8 *buf;
    guint8 *head, *tail;
    gsize size;
    GMutex mutex;
};

ringbuf_t ringbuf_new (gsize capacity) {
    ringbuf_t rb = g_new0(struct ringbuf_t, 1);
    if (rb) {
        /* One byte is used for detecting the full condition. */
        rb->size = capacity + 1;
        rb->buf = g_malloc(rb->size);
        if (rb->buf)
            ringbuf_reset(rb);
        else {
            g_free(rb);
            return 0;
        }
        g_mutex_init(&rb->mutex);
    }
    return rb;
}

gsize ringbuf_buffer_size (const struct ringbuf_t *rb) {
    return rb->size;
}

void ringbuf_reset (ringbuf_t rb) {
    guint8 *current_buf = NULL;
    g_mutex_lock(&rb->mutex);
    current_buf = rb->buf;
    rb->head = current_buf;
    rb->tail = current_buf;
    g_mutex_unlock(&rb->mutex);
}

void ringbuf_free (ringbuf_t *rb) {
    g_assert(rb && *rb);
    g_mutex_clear(&(*rb)->mutex);
    g_free((*rb)->buf);
    (*rb)->buf = NULL;
    g_free(*rb);
    *rb = NULL;
}

gsize ringbuf_capacity (const struct ringbuf_t *rb) {
    return ringbuf_buffer_size(rb) - 1;
}

/*
 * Return a pointer to one-past-the-end of the ring buffer's
 * contiguous buffer. You shouldn't normally need to use this function
 * unless you're writing a new ringbuf_* function.
 */
static const guint8 *ringbuf_end (const struct ringbuf_t *rb) {
    return rb->buf + ringbuf_buffer_size(rb);
}

gsize ringbuf_bytes_free (struct ringbuf_t *rb) {
    gsize retval = 0;
    const guint8 *tail = ringbuf_tail(rb);
    const guint8 *head = ringbuf_head(rb);

    if (head >= tail)
        retval = ringbuf_capacity(rb) - (gsize)(head - tail);
    else
        retval =  (gsize)(tail - head) - 1;

    return retval;
}

gsize ringbuf_bytes_used (struct ringbuf_t *rb) {
    return ringbuf_capacity(rb) - ringbuf_bytes_free(rb);
}

gboolean ringbuf_is_full (struct ringbuf_t *rb) {
    return (ringbuf_bytes_free(rb) == 0);
}

gboolean ringbuf_is_empty (struct ringbuf_t *rb) {
    return (ringbuf_bytes_free (rb) == ringbuf_capacity (rb));
}

const guint8 *ringbuf_tail (struct ringbuf_t *rb) {
    const guint8 *retval = 0;
    g_mutex_lock(&rb->mutex);
    retval = rb->tail;
    g_mutex_unlock(&rb->mutex);
    return retval;
}

const guint8 *ringbuf_head (struct ringbuf_t *rb) {
    const guint8 *retval = 0;
    g_mutex_lock(&rb->mutex);
    retval = rb->head;
    g_mutex_unlock(&rb->mutex);
    return retval;
}

/*
 * Given a ring buffer rb and a pointer to a location within its
 * contiguous buffer, return the a pointer to the next logical
 * location in the ring buffer.
 */
static guint8 *ringbuf_nextp (ringbuf_t rb, const guint8 *p) {
    /*
     * The assert guarantees the expression (++p - rb->buf) is
     * non-negative; therefore, the modulus operation is safe and
     * portable.
     */
    g_assert((p >= rb->buf) && (p < ringbuf_end(rb)));
    return rb->buf + ((++p - rb->buf) % ringbuf_buffer_size(rb));
}

// Define a macro to generate the function for a given data type
#define RINGBUF_MEMCPY_INTO_FUNC(type) \
gpointer ringbuf_memcpy_into_##type(ringbuf_t dst, const type *src, gsize count) { \
    const guint8 *u8src = (const guint8 *)src; \
    const guint8 *bufend = ringbuf_end(dst); \
    guint8 *dsthead = (guint8 *)ringbuf_head(dst); \
    gboolean overflow = count > ringbuf_bytes_free(dst); \
    gsize nread = 0, n, diff; \
    while (nread != count) { \
        /* don't copy beyond the end of the buffer */ \
        g_assert(bufend > dsthead); \
        diff = bufend - dsthead; \
        n = MIN(diff, count - nread); \
        memcpy(dsthead, u8src + nread, n * sizeof(type)); \
        dsthead += n * sizeof(type); \
        nread += n; \
        /* wrap? */ \
        if (dsthead == bufend) \
            dsthead = dst->buf; \
    } \
    g_mutex_lock(&dst->mutex); \
    dst->head = dsthead; \
    g_mutex_unlock(&dst->mutex); \
    if (overflow) { \
        /* mark the ring buffer as full */ \
        g_mutex_lock(&dst->mutex); \
        dst->tail = ringbuf_nextp(dst, dsthead); \
        g_mutex_unlock(&dst->mutex); \
        g_assert(ringbuf_is_full(dst)); \
    } \
    return dsthead; \
}

// Generate the functions for guint16 and guint64
RINGBUF_MEMCPY_INTO_FUNC(guint16)
RINGBUF_MEMCPY_INTO_FUNC(guint64)

gpointer ringbuf_memcpy_from (gpointer dst, ringbuf_t src, gsize count) {
    gsize bytes_used = ringbuf_bytes_used(src), n = 0, diff = 0;
    if (count > bytes_used) {
        return NULL;
    }

    guint8 *u8dst = dst;
    guint8 *bufend = (guint8 *)ringbuf_end(src);
    guint8 *tail = (guint8 *)ringbuf_tail(src);

    gsize nwritten = 0;
    while (nwritten != count) {
        g_assert(bufend > tail);
        diff = bufend - tail;
        n = MIN(diff, count - nwritten);
        memcpy(u8dst + nwritten, tail, n);
        tail += n;
        nwritten += n;

        /* wrap ? */
        if (tail == bufend)
            tail = src->buf;
    }

    g_mutex_lock(&src->mutex);
    src->tail = tail;
    g_mutex_unlock(&src->mutex);

    g_assert(count + ringbuf_bytes_used(src) == bytes_used);

    
    
    return tail;
}