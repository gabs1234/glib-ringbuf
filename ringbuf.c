/*
 * Ring buffer implementation using GLib and virtual memory tricks.
 * More info:
 * - http://en.wikipedia.org/wiki/Circular_buffer 
 * - https://lo.calho.st/posts/black-magic-buffer/
 * 
 * Gabriel Lefloch
 */

#include "ringbuf.h"

#include <linux/memfd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>

struct message_t {
    gsize len;
    guint seq;
};

struct _ringbuf_t {
    gpointer buf;
    gint fd;
    gsize head, tail, element_size, buffer_size;
    GMutex mutex;
    GCond readable, writeable;
    gboolean block_on_full;
    GAsyncQueue message_queue;
};

/** Convenience wrapper around memfd_create syscall, because apparently this is
  * so scary that glibc doesn't provide it...
  */
#if __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 27)
static inline int memfd_create(const char *name, unsigned int flags) {
    return syscall(__NR_memfd_create, name, flags);
}
#endif

ringbuf_t *ringbuf_new (gsize element_size, guint length, gboolean block, GError **error) {
    // Check that the requested size is a multiple of a page. If it isn't, we're in trouble.
    gsize s = length * element_size;
    if (s % getpagesize() != 0) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Requested ring buffer size is not a multiple of a page");
        return NULL;
    }

    gint fd = -1;
    gpointer buffer = NULL;

    // Create an anonymous file backed by memory
    if((fd = memfd_create("queue_region", 0)) == -1){
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Failed to create anonymous file");
        return NULL;
    }

    // Set buffer size
    if(ftruncate(fd, s) != 0){
        queue_error_errno("Could not set size of anonymous file");
    }
    
    // Ask mmap for a good address
    if((buffer = mmap(NULL, 2 * s, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
        queue_error_errno("Could not allocate virtual memory");
    }
    
    // Mmap first region
    if(mmap(buffer, s, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory");
    }
    
    // Mmap second region, with exact address
    if(mmap(buffer + s, s, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED){
        queue_error_errno("Could not map buffer into virtual memory");
    }


    ringbuf_t *rb = g_new0(ringbuf_t, 1);
    if (rb == NULL) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Failed to allocate memory for ring buffer");
        return NULL;
    }

    // Init the condition variables
    g_cond_init(&rb->readable);
    g_cond_init(&rb->writeable);

    // Init the mutex
    g_mutex_init(&rb->mutex);
    
    /* One byte is used for detecting the full condition. */
    rb->element_size = element_size;
    rb->buffer_size = s;
    rb->buf = buffer;
    rb->fd = fd;
    rb->error = NULL;
    rb->head = rb->tail = 0;
    rb->block_on_full = block;
    
    return rb;
}

gsize ringbuf_buffer_size (const ringbuf_t *rb) {
    return rb->buffer_size;
}

void ringbuf_reset (ringbuf_t *rb) {
    g_mutex_lock(&rb->mutex);
    rb->head = rb->tail = 0;
    g_mutex_unlock(&rb->mutex);
}

void ringbuf_free (ringbuf_t *rb) {
    g_assert(rb);

    GString *error_msg = NULL;

    if(munmap(rb->buf + rb->buffer_size, rb->buffer_size) != 0){
        g_string_append(error_msg, "Could not unmap second buffer. ");
    }
    
    if(munmap(rb->buf, rb->buffer_size) != 0){
        g_string_append(error_msg, "Could not unmap buffer. ");
    }
    
    if(close(rb->fd) != 0){
        g_string_append(error_msg, "Could not close file descriptor. ");
    }

    if (error_msg != NULL) {
        rb->error = g_error_new(G_FILE_ERROR, G_FILE_ERROR_FAILED, error_msg->str);
        g_string_free(error_msg, TRUE);
    }

    g_mutex_clear(&rb->mutex);
    g_cond_clear(&rb->readable);
    g_cond_clear(&rb->writeable);
}

gsize ringbuf_capacity (const ringbuf_t *rb) {

}

gsize ringbuf_bytes_free (ringbuf_t *rb) {
    
}

gsize ringbuf_bytes_used (ringbuf_t *rb) {

}

gboolean ringbuf_is_full (ringbuf_t *rb) {
    return (ringbuf_bytes_free(rb) == 0);
}

gboolean ringbuf_is_empty (ringbuf_t *rb) {
    return (ringbuf_bytes_free (rb) == ringbuf_capacity (rb));
}

gconstpointer ringbuf_tail (ringbuf_t *rb) {
    gpointer retval = NULL;
    g_mutex_lock(&rb->mutex);
    retval = rb->tail + rb->buf;
    g_mutex_unlock(&rb->mutex);
    return retval;
}

gconstpointer ringbuf_head (ringbuf_t *rb) {
    gpointer retval = NULL;
    g_mutex_lock(&rb->mutex);
    retval = rb->head + rb->buf;
    g_mutex_unlock(&rb->mutex);
    return retval;
}


gpointer ringbuf_push(ringbuf_t *dst, gconstpointer src, gsize size) {
  
    // Wait for space to become available
    if (dst->block_on_full) {
        g_mutex_lock(&dst->mutex);
        while (ringbuf_bytes_free(dst) < size) {
            g_cond_wait(&dst->writeable, &dst->mutex);
        }
        g_mutex_unlock(&dst->mutex);
    }
    
    // Construct message
    message_t m = g_new0(message_t, 1);
    m->len = size;
    m->seq = dst->head;

    g_async_queue_push(dst->message_queue, m);
    
    g_mutex_lock(&dst->mutex);
    // Write message
    memcpy(&dts->buf[dts->tail], src, size);
    
    // Increment write index
    dts->tail += size;
    g_mutex_unlock(&dst->mutex);
}

gpointer ringbuf_pop (gpointer dst, ringbuf_t *src, gsize count) {
    gsize n = 0, diff = 0;

    gint64 end_time = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (ringbuf_bytes_used(src) < count) {
        g_mutex_lock(&src->mutex);
        if (!g_cond_wait_until(&src->cond, &src->mutex, end_time)) {
            g_mutex_unlock(&src->mutex);
            return NULL;
        }
        g_mutex_unlock(&src->mutex);
    }

    guint8 *u8dst = dst;
    guint8 *bufend = ringbuf_end(src);
    guint8 *tail = ringbuf_tail(src);

    gsize nwritten = 0;
    while (nwritten != count) {
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

    return tail;
}