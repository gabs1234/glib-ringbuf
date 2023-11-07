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

gchar *filename = "../test.bin";
const guint res_x = 512, res_y = 768;
gsize image_size = res_x * res_y * sizeof(guint16);
guint nb_images = 5;

ringbuf_t *rb;

gpointer push (gpointer data) {
    g_print ("push\n");

    // Open file to read
    FILE *fp = fopen("../test.bin", "rb");
    if (fp == NULL) {
        printf("Error opening file\n");
        return NULL;
    }

    // Store image in memory
    guint16 *images = g_malloc0 (image_size * nb_images);
    if (images == NULL) {
        printf("Error allocating image\n");
        return NULL;
    }
    if (fread(images, image_size*nb_images, 1, fp) == 1) {
        printf("Read image\n");
    }
    else {
        printf("Error reading image\n");
        return NULL;
    }

    // Write into ring buffer
    for (guint i = 0; i < nb_images; i++) {
        // print 10 first values
        for (guint j = 0; j < 10; j++) {
            printf("w%d ", images[i* res_x * res_y + j + 200]);
        }
        printf("\n");
        ringbuf_push (rb, images + i * res_x * res_y, image_size);
    }

    g_free(images);
    fclose(fp);

    return NULL;
}

gpointer pop (gpointer data) {
    g_print ("pop\n");
    
    
    guint16 *read_buffer = g_malloc0 (image_size);
    if (read_buffer == NULL) {
        printf("Error allocating image\n");
        return NULL;
    }

    FILE *fp = NULL;
    gchar *filename = NULL;

    for (guint i = 0; i < nb_images; i++) {
        // open new file
        gchar *filename = g_strdup_printf("test%d_out.bin", i);
        g_print ("Writing %s\n", filename);
        fp = fopen(filename, "wb");
        if (fp == NULL) {
            printf("Error opening file\n");
            return NULL;
        }
        ringbuf_pop (read_buffer, rb, image_size);

        // print 10 first values
        for (guint j = 0; j < 10; j++) {
            printf("r%d ", read_buffer[j + 200]);
        }
        printf("\n");


        if (fwrite(read_buffer, image_size, 1, fp) == 1) {
            printf("Wrote image\n");
        }
        else {
            printf("Error writing image\n");
            return NULL;
        }
        fclose(fp);
        g_free(filename);
    }
    g_free(read_buffer);

    return NULL;
}

int main (int argc, char **argv) {
    g_print ("main\n");

    gsize buffer_size = 5 * image_size;

    rb = ringbuf_new (buffer_size, TRUE, NULL);
    g_assert(ringbuf_is_empty(rb));

    GThread *pop_thread = g_thread_new("pop", pop, NULL);
    GThread *push_thread = g_thread_new("push", push, NULL);

    g_thread_join(pop_thread);
    g_thread_join(push_thread);
    
    ringbuf_free (rb);

    return 0;
}
