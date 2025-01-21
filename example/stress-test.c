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

typedef enum {
    RINGBUF_POP,
    GENERATE_DATA,
    NB_MEASURE_TYPES
} measure_enum;

static const gchar *measure_type_str[NB_MEASURE_TYPES] = {
    "data/MEASURE_RINGBUF_POP_%d.csv",
    "data/MEASURE_GENERATE_DATA_%d.csv"
};

typedef struct {
    guint nb_images;
    guint x_res;
    guint y_res;
    gsize byte_depth;
    gboolean kill_pill;
} request_t;

typedef struct {
    measure_enum type;
    guint64 tic, toc;
    gsize size;
    gboolean kill_pill;
} measure_t;

// Profiling variables
GCond cond;
GMutex mutex;
guint64 start = 0, end = 0; // microseconds
guint64 total_time = 0, current_time = 0; // microseconds

ringbuf_t *rb;
gsize total_data_received = 0, total_data_popped = 0, total_generated_data = 0;

GAsyncQueue *image_queue = NULL;
GAsyncQueue *request_queue = NULL;
GAsyncQueue *measure_queue = NULL;

gboolean request_images (gint nb_images, guint x_res, guint y_res, gsize byte_depth, gboolean kill_pill) {
    request_t *request = g_new0 (request_t, 1);
    if (request == NULL) {
        g_debug ("Error allocating request struct\n");
        return FALSE;
    }
    request->nb_images = nb_images;
    request->x_res = x_res;
    request->y_res = y_res;
    request->byte_depth = byte_depth;
    request->kill_pill = kill_pill;

    g_async_queue_push (request_queue, request);

    return TRUE;
}

gpointer profiler (gpointer data) {
    FILE *fp[NB_MEASURE_TYPES];
    g_debug ("Starting measure thread %d\n", getpid());

    for (int i=0; i < NB_MEASURE_TYPES; i++) {
        gchar *file_name = g_strdup_printf (measure_type_str[i], getpid());
        fp[i] = fopen (file_name, "w");
        if (fp[i] == NULL) {
            g_warning ("Could not open file %s", file_name);
        }
        g_free (file_name);
    }
    
    guint64 current_time = 0;
    while (TRUE) {

        measure_t* mdata = g_async_queue_pop (measure_queue);
        if (mdata == NULL) {
            g_debug ("No more data\n");
            break;
        }
        else if (mdata->kill_pill) {
            g_free (mdata);
            break;
        }
        
        fprintf (fp[mdata->type], "%ld,%ld,%ld\n", mdata->tic, mdata->toc, mdata->size);

        g_free (mdata);
    }

    for (int i=0; i < NB_MEASURE_TYPES; i++) {
        fclose (fp[i]);
    }

    g_debug ("Ending measure thread %d\n", getpid());

    return NULL;
}

gpointer general_increasing_data (gsize len, gsize size) {
    guint8 *data = g_malloc0 (size * len);
    if (data == NULL) {
        g_debug ("Error allocating data\n");
        return NULL;
    }

    // use pointer arithmetic to fill the buffer
    guint8 *ptr = data;
    for (gsize i = 0; i < len; i++) {
        *(ptr + i * size) = i;
    }

    return data;
}

gpointer writer_thread (gpointer data) {
    g_debug ("Starting writer thread %d\n", getpid());
    srand(time(NULL));

    while (TRUE) {
        request_t *request = g_async_queue_pop (request_queue);
        if (request == NULL) {
            g_debug ("No more requests\n");
            break;
        }
        else if (request->kill_pill) {
            g_free (request);
            break;
        }

        measure_t *mdata = g_new0 (measure_t, 1);

        gsize image_size = request->x_res * request->y_res * request->byte_depth;

        mdata->type = GENERATE_DATA;
        mdata->size = image_size;
        mdata->tic = g_get_monotonic_time ();
        guint8 *data = general_increasing_data (image_size, request->byte_depth);
        mdata->toc = g_get_monotonic_time ();
        mdata->kill_pill = FALSE;
        g_async_queue_push (measure_queue, mdata);
        
        gpointer retval = ringbuf_push (rb, data, image_size);
        if (retval == NULL) {
            g_debug ("Not enough space available\n");
            g_free (data);
            break;
        }
        total_data_received += image_size;

        g_free (data);
        g_free (request);
    }

    g_debug ("Ending writer thread %d\n", getpid());

    return NULL;
}

gpointer reader_thread (gpointer data) {
    g_debug ("Starting reader thread %d\n", getpid());

    static gboolean first = TRUE;

    gsize image_size = *(gsize *)data;
    guint16 *buf = g_malloc (image_size);
    if (buf == NULL) {
        // g_debug ("Error allocating packet buffer\n");
        return NULL;
    }

    while (TRUE) {
        measure_t *mdata = g_new0 (measure_t, 1);
        mdata->type = RINGBUF_POP;
        mdata->size = image_size;
        mdata->tic = g_get_monotonic_time ();
        gpointer res = ringbuf_timed_pop (buf, rb, image_size, G_TIME_SPAN_SECOND);
        mdata->toc = g_get_monotonic_time ();
        mdata->kill_pill = FALSE;
        if (res == NULL) {
            g_free (mdata);
            break;
        }
        g_async_queue_push (measure_queue, mdata);
    
        total_data_popped += image_size;
    }

    g_debug ("Timed out... Ending reader thread %d\n", getpid());

    g_free (buf);
    return NULL;
}

void handle_sigint (int sig) {
    g_debug ("Received SIGINT\n");
    g_debug ("Total data received: %ld\n", total_data_received);
    g_debug ("Total data read: %ld\n", total_data_popped);
}

int main (int argc, char **argv) {
    // Init queues
    request_queue = g_async_queue_new ();
    measure_queue = g_async_queue_new ();

    guint nb_images = 50, res_x = 1024, res_y = 1024;
    gsize byte_depth = sizeof(guint16);
    gsize image_size = res_x * res_y * byte_depth;

    rb = ringbuf_new (nb_images * image_size, TRUE);

    // Setup signal handler
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    // Create threads
    GThread *profiler_thread = g_thread_new ("profiler", profiler, NULL);
    GThread *reader = g_thread_new ("reader", reader_thread, &image_size);
    GThread *writer = g_thread_new ("writer", writer_thread, NULL);

    // Request images
    guint MaxNumberOfImagesPerCall = 4;
    for (guint i = 0; i < MaxNumberOfImagesPerCall; i ++) {
        request_images (nb_images, res_x, res_y, byte_depth, FALSE);
    }
    request_images (0, 0, 0, 0, TRUE);    

    // Wait for threads to finish
    g_thread_join (reader);
    g_thread_join (writer);

    measure_t *mdata = g_new0 (measure_t, 1);
    mdata->kill_pill = TRUE;
    g_async_queue_push (measure_queue, mdata);

    g_thread_join (profiler_thread);

    // free stuff
    g_async_queue_unref (request_queue);
    g_async_queue_unref (measure_queue);
    ringbuf_free (rb);

    return 0;
}
