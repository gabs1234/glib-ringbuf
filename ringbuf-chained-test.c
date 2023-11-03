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
#include <unistd.h>
#include <stdio.h>

/* Default sizes for these tests. */
const gsize bit_depth = sizeof (guint16);
const guint nb_pixels = 10;
const gsize image_size = nb_pixels * bit_depth;
const gsize nb_images = 100;
const gsize ring_buf_size = nb_images * image_size;

ringbuf_t *rb1;
ringbuf_t *rb2;

gboolean stop = FALSE;
gsize total_data_received1 = 0, total_data_read1 = 0, total_data = ring_buf_size;
gsize total_data_received2 = 0, total_data_read2 = 0;

GArray *images;

gpointer writer_thread (gpointer data) {
    guint32 *buf = g_malloc (image_size);
    guint32 i = 0;
    gpointer retval = NULL;
    gsize count = 0, packet = 2;

    g_print ("Starting to memcpy into ring %ld\n", image_size);
    while (!stop) {
        for (int j = 0; j < nb_pixels; j+=packet) {
            for (int k = 0; k < packet; k++) {
                buf[j+k] = i;
                i++;
            }
            count = j > nb_pixels ? j - nb_pixels : packet;
            g_print ("memcpy %ld\n", count);
            retval = ringbuf_memcpy_into (rb, buf+j, count * bit_depth);
            if (retval == NULL) {
                printf("Not enough space available\n");
                g_free (buf);
                return NULL;
            }
        }
        total_data_received += image_size;
        g_usleep (G_USEC_PER_SEC/10);
    }

    g_free (buf);

    return NULL;
}

gpointer reader_thread (gpointer data) {
    guint32 *buf = NULL;
    static gboolean first = TRUE;
    g_print ("Starting to read ringbuff\n");

    while (!stop) {
        buf = g_malloc (image_size);
        if (buf == NULL) {
            // printf("Error allocating packet buffer\n");
            return NULL;
        }
        g_print ("waiting\n");
        gpointer res = ringbuf_memcpy_from (buf, rb, image_size);
        if (res == NULL) {
            printf("Timed out...\n");
            g_free (buf);
            continue;
        }

        total_data_read += image_size;

        // Print image
        for (int i = 0; i < nb_pixels; i++) {
            printf("%d ", buf[i]);
        }
        printf("\n");
        g_print ("Read it!\n");
        g_free (buf);
    }
    
    return NULL;
}

void handle_sigint (int sig) {
    printf("Received SIGINT\n");
    stop = TRUE;
    g_print ("Total data received: %ld\n", total_data_received);
    g_print ("Total data read: %ld\n", total_data_read);
}

int main (int argc, char **argv) {

    g_print ("Creating\n");
    rb1 = ringbuf_new (bit_depth, ring_buf_size);
    rb2 = ringbuf_new (bit_depth, ring_buf_size);

    g_assert(ringbuf_is_empty(rb1));
    g_assert(ringbuf_is_empty(rb2));

    // Setup signal handler
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    // Init array
    images = g_array_new (FALSE, FALSE, bit_depth);

    // Create threads
    g_print ("Starting writer\n");
    GThread *receiver = g_thread_new ("writer", writer_thread, NULL);
    g_print ("Starting reader\n");
    GThread *reader = g_thread_new ("reader", reader_thread, NULL);

    // Wait for threads to finish
    g_thread_join (writer);
    g_thread_join (reader);

    // free stuff
    g_array_free (images, TRUE);
    ringbuf_free (rb1);
    ringbuf_free (rb2);

    return 0;
}
