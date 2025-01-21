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
#include <stdlib.h>
#include <glib.h>

#define MAX_MALLOC_SIZE G_MAXSIZE

typedef struct {
    guint nb_images;
    guint x_res;
    guint y_res;
    guint byte_depth;
} request_t;

typedef struct {
    guint16 *buf;
    guint nb_images;
    gsize packet_size;
    gsize image_size;
} packet_t;

// Profiling variables
GCond cond;
GMutex mutex;
guint64 start = 0, end = 0; // microseconds
guint64 total_time = 0, current_time = 0; // microseconds

ringbuf_t *rb;
gboolean stop = FALSE;
gsize total_data_received = 0, total_data_read = 0;

GAsyncQueue *image_queue = NULL;
GAsyncQueue *request_queue = NULL;

gboolean request_images (gint nb_images, guint x_res, guint y_res, guint byte_depth) {
    request_t *request = g_new0 (request_t, 1);
    if (request == NULL) {
        printf("Error allocating request struct\n");
        return FALSE;
    }
    request->nb_images = nb_images;
    request->x_res = x_res;
    request->y_res = y_res;
    request->byte_depth = byte_depth;

    g_async_queue_push (request_queue, request);
    // g_print ("Requesting %d images\n", nb_images);

    return TRUE;
}

gpointer profiler (gpointer data) {
    g_print ("Starting profiler\n");
    
    g_mutex_lock (&mutex);
    while (!stop) {
        g_print ("time elapsed: %ld\n", current_time);
        g_cond_wait (&cond, &mutex);
    }
    g_mutex_unlock (&mutex);

    g_print ("Total time: %f\n", (gfloat)total_time/G_USEC_PER_SEC);
    g_print ("Total data received: %ld\n", total_data_received);
}

gpointer generate_images (gpointer data) {
    guint16 *image = NULL;
    gsize packet_size = 0, image_size = 0;
    packet_t *image_struct = NULL;
    request_t *request = NULL;
    // Seed random number generator
    srand(time(NULL));

    while (TRUE) {
        request = g_async_queue_pop (request_queue);
        if (request == NULL) {
            printf("Error getting nb_images_requested from queue\n");
            return FALSE;
        }

        // g_print ("Popped! Generating %d images\n", request->nb_images);

        if (request->nb_images == 0) {
            image_struct = g_new0 (packet_t, 1);
            image_struct->buf = NULL;
            image_struct->nb_images = 0;

            g_async_queue_push (image_queue, image_struct);
            g_free (request);
            return FALSE;
        }

        image_struct = g_new0 (packet_t, 1);
        if (image_struct == NULL) {
            printf("Error allocating image struct\n");
            return FALSE;
        }

        image_size = request->x_res * request->y_res * request->byte_depth;
        packet_size = request->nb_images *image_size;

        image = g_malloc (packet_size);
        if (image == NULL) {
            printf("Error allocating image buffer\n");
            return FALSE;
        }

        for (guint x=0; x < request->x_res; x++) {
            for (guint y=0; y < request->y_res; y++) {
                guint pos = x * request->y_res + y;
                *(image + pos) = rand();
            }
        }

        image_struct->buf = image;
        image_struct->nb_images = request->nb_images;
        image_struct->packet_size = packet_size;
        image_struct->image_size = image_size;

        g_async_queue_push (image_queue, image_struct);

        g_free (request);
    }

    return NULL;
}

gpointer receiver_thread (gpointer data) {
    guint16 *buf;
    packet_t *image_struct;
    guint16 i = 0, nb_images = 0;
    gpointer retval = NULL;

    srand(time(NULL));

    while (TRUE) {
        image_struct = g_async_queue_pop (image_queue);
        if (image_struct->buf == NULL || image_struct->nb_images == 0) {
            g_free (image_struct);
            break;
        }

        // g_print ("Received %d images\n", image_struct->nb_images);

        buf = image_struct->buf;
        nb_images = image_struct->nb_images;
        // g_print ("Copying %d images\n", nb_images);
        retval = ringbuf_push (rb, buf, image_struct->packet_size);
        if (retval == NULL) {
            printf("Not enough space available\n");
            g_free (buf);
            return NULL;
        }
        total_data_received += image_struct->packet_size;

        g_free (buf);
        g_free (image_struct);
    }

    g_print ("Stopping\n");

    stop = TRUE;
    return NULL;
}

gpointer reader_thread (gpointer data) {
    guint16 *buf = NULL;
    static gboolean first = TRUE;

    gsize image_size = 1952 * 2048 * 2;
    
    buf = g_malloc (image_size);
    if (buf == NULL) {
        // printf("Error allocating packet buffer\n");
        return NULL;
    }

    while (!stop) {
        start = g_get_monotonic_time ();
        gpointer res = ringbuf_timed_pop (buf, rb, image_size, G_TIME_SPAN_SECOND);
        if (res == NULL) {
            printf("Timed out...\n");
            continue;
        }
        end = g_get_monotonic_time ();
        g_print ("Poped %ld bytes\n", image_size);
        total_data_read += image_size;
        current_time = end - start;
        total_time += current_time;

        g_cond_signal (&cond);
    }
    g_free (buf);
    return NULL;
}

void handle_sigint (int sig) {
    printf("Received SIGINT\n");
    stop = TRUE;
    g_print ("Total data received: %ld\n", total_data_received);
    g_print ("Total data read: %ld\n", total_data_read);
}

int main (int argc, char **argv) {
    // Init queues
    image_queue = g_async_queue_new ();
    request_queue = g_async_queue_new ();

    guint nb_images = 3000, res_x = 2048, res_y = 1952, byte_depth = 2;
    gsize image_size = res_x * res_y * byte_depth;

    g_print ("Creating\n");
    rb = ringbuf_new (image_size*nb_images, TRUE, NULL);

    g_assert(ringbuf_is_empty(rb));

    // Setup signal handler
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    g_mutex_init (&mutex);
    g_cond_init (&cond);

    // Create threads
    // g_print ("Starting profiler\n");
    GThread *profiler_thread = g_thread_new ("profiler", profiler, NULL);
    // g_print ("Starting reader\n");
    GThread *reader = g_thread_new ("reader", reader_thread, NULL);
    // g_print ("Starting writer\n");
    GThread *writer = g_thread_new ("receiver", receiver_thread, NULL);
    // g_print ("Starting generator\n");
    GThread *generator = g_thread_new ("generator", generate_images, NULL);

    // Request images
    guint MaxNumberOfImagesPerCall = 4;
    for (guint i = 0; i < MaxNumberOfImagesPerCall; i ++) {
        request_images (nb_images, res_x, res_y, byte_depth);
    }
    request_images (0, 0, 0, 0);

    // Wait for threads to finish
    g_thread_join (writer);
    g_thread_join (reader);

    // free stuff
    g_async_queue_unref (image_queue);
    g_async_queue_unref (request_queue);
    ringbuf_free (rb);

    return 0;
}
