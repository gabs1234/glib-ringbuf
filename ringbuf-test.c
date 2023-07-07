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

/* Default size for these tests. */
#define nb_images 100
#define image_size 10 // in bytes
#define N 1000
#define RINGBUF_SIZE N*image_size // Save n images without overwriting

ringbuf_t rb;
gboolean stop = FALSE;
gsize total_data_received = 0, total_data_read = 0, total_data = nb_images * image_size;

GArray *images;

gpointer writer_thread (gpointer data) {
    guint8 *buf = g_malloc (image_size * sizeof (guint8));

    guint8 i = 0;

    g_print ("Starting to memcpy into ring\n", image_size);
    while (!stop) {
        for (int j = 0; j < image_size; j++) {
            buf[j] = i;
            i++;
        }
        ringbuf_memcpy_into(rb, buf, image_size);
        total_data_received += image_size;
        g_usleep(10000);
    }

    g_free (buf);

    return NULL;
}

gpointer reader_thread (gpointer data) {
    guint8 *buf = NULL;
    static gboolean first = TRUE;
    g_print ("Starting to read ringbuff\n");

        g_usleep(100000);

    while (!stop) {
        // if (ringbuf_tail(rb) > ringbuf_head(rb)) {
        //     // g_print ("Not enough data to read\n");
        //     // sleep(.1);
        //     continue;
        // }
        // else {
        //     g_print ("Reading %d bytes\n", image_size);
        // }

        buf = g_malloc (image_size * sizeof (guint8));
        if (buf == NULL) {
            // printf("Error allocating packet buffer\n");
            return NULL;
        }

        gpointer res = ringbuf_memcpy_from (buf, rb, image_size);

        if (res == NULL) {
            // printf("Not enough data available\n");
            g_free (buf);
            continue;
        }

        total_data_read += image_size;

        // Print image
        for (int i = 0; i < image_size; i++) {
            printf("%d ", buf[i]);
        }
        printf("\n");
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
    rb = ringbuf_new(RINGBUF_SIZE);

    g_assert(ringbuf_is_empty(rb));

    // Setup signal handler
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    // Init array
    images = g_array_new (FALSE, FALSE, sizeof (guint8));

    // Create threads
    g_print ("Starting writer\n");
    GThread *writer = g_thread_new ("writer", writer_thread, NULL);
    g_print ("Starting reader\n");
    GThread *reader = g_thread_new ("reader", reader_thread, NULL);

    // Wait for threads to finish
    g_thread_join (writer);
    g_thread_join (reader);

    g_print ("Total data received: %ld\n", total_data_received);
    g_print ("Total data read: %ld\n", total_data_read);

    // free stuff
    g_array_free (images, TRUE);
    ringbuf_free(&rb);

    return 0;
}
