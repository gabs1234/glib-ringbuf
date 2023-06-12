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
#define RINGBUF_SIZE 1000
#define nb_images 100
#define image_size 10 // in bytes

ringbuf_t rb;
gboolean stop = FALSE;
gsize total_data_received = 0, total_data = nb_images * image_size;

GArray *images;

gpointer writer_thread (gpointer data) {
    guint8 buf[image_size];

    for (int i = 0; i < image_size; i++) {
        buf[i] = i;
    }

    total_data_received += image_size;

    while (!stop && total_data_received < total_data) {
        ringbuf_memcpy_into(rb, buf, image_size);
        sleep(.1);
    }

    return NULL;
}

gpointer reader_thread (gpointer data) {
    guint8 *buf = NULL;

    while (!stop && total_data_received < total_data) {
        buf = malloc (image_size * sizeof (buf));
        if (buf == NULL) {
            printf("Error allocating packet buffer\n");
            return NULL;
        }
        ringbuf_memcpy_from(buf, rb, image_size);

        // Add image to array
        g_array_append_val (images, buf);
    }
    
    return NULL;
}

void handle_sigint (int sig) {
    printf("Received SIGINT\n");
    stop = TRUE;
    g_print ("Total data received: %ld\n", total_data_received);
}

int main (int argc, char **argv) {

    g_print ("Creating");
    ringbuf_t rb = ringbuf_new(RINGBUF_SIZE - 1);

    g_assert(ringbuf_is_empty(rb));

    // Setup signal handler
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    // Init array
    images = g_array_new (FALSE, FALSE, sizeof (guint8));

    // Create threads
    g_print ("Starting writer");
    GThread *writer = g_thread_new ("writer", writer_thread, NULL);
    g_print ("Starting reader");
    GThread *reader = g_thread_new ("reader", reader_thread, NULL);

    // Wait for threads to finish
    g_thread_join (writer);
    g_thread_join (reader);

    // free stuff
    g_array_free (images, TRUE);
    ringbuf_free(&rb);

    return 0;
}
