#include <glib.h>
#include <glib/gstdio.h>
#include "test.h"
#include "../ringbuf.h"

#define NUM_CONSUMERS 2
#define NUM_BLOCKS 1000  // Total blocks to produce
#define BLOCK_SIZE 64    // Size of each block

typedef struct {
    ringbuf_t *rb;
    GMutex mutex;
    guint consumed_count;  // Shared counter for consumed blocks
    gboolean producer_done;
} TestContext;

// Producer thread: reserves, fills, and commits blocks
static gpointer producer_thread(gpointer data) {
    TestContext *ctx = data;
    
    for (guint i = 0; i < NUM_BLOCKS; i++) {
        // Reserve space
        guint8 *reserved = ringbuf_reserve(ctx->rb, BLOCK_SIZE);
        g_assert_nonnull(reserved);

        // Fill with unique data (i + j pattern to detect corruption)
        for (gsize j = 0; j < BLOCK_SIZE; j++) {
            reserved[j] = (guint8)(i + j);
        }

        // Commit to make data visible to consumers
        ringbuf_commit(ctx->rb, BLOCK_SIZE);
    }

    // Signal end of production
    g_mutex_lock(&ctx->mutex);
    ctx->producer_done = TRUE;
    g_mutex_unlock(&ctx->mutex);

    return NULL;
}

// Consumer thread: pops blocks and validates data
static gpointer consumer_thread(gpointer data) {
    TestContext *ctx = data;
    guint8 buffer[BLOCK_SIZE];

    while (TRUE) {
        // Pop data (blocking)
        gpointer result = ringbuf_timed_pop(buffer, ctx->rb, BLOCK_SIZE, G_USEC_PER_SEC);
        g_mutex_lock(&ctx->mutex);
        if (ctx->producer_done && result == NULL) {
            g_mutex_unlock(&ctx->mutex);
            break;
        }
        g_mutex_unlock(&ctx->mutex);
        g_assert_nonnull(result);

        // Validate data integrity
        guint base = buffer[0];  // First byte = block index
        gboolean valid = TRUE;
        for (gsize j = 0; j < BLOCK_SIZE; j++) {
            if (buffer[j] != (guint8)(base + j)) {
                valid = FALSE;
                break;
            }
        }
        g_assert_true(valid);

        // Update shared counter
        g_mutex_lock(&ctx->mutex);
        ctx->consumed_count++;
        g_mutex_unlock(&ctx->mutex);

        // Exit if producer is done and all blocks consumed
        g_mutex_lock(&ctx->mutex);
        if (ctx->producer_done && ctx->consumed_count >= NUM_BLOCKS) {
            g_mutex_unlock(&ctx->mutex);
            break;
        }
        g_mutex_unlock(&ctx->mutex);
    }

    return NULL;
}

// Main test function
static void test_multi_consumer_reserve_commit(void) {
    TestContext ctx = {
        .rb = ringbuf_new(PLATFORM_MAX_BYTES, TRUE),
        .consumed_count = 0,
        .producer_done = FALSE
    };
    g_mutex_init(&ctx.mutex);

    // Create threads
    GThread *producer = g_thread_new("producer", producer_thread, &ctx);
    GThread *consumers[NUM_CONSUMERS];
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        consumers[i] = g_thread_new(g_strdup_printf("consumer-%d", i), consumer_thread, &ctx);
    }

    // Wait for producer to finish
    g_thread_join(producer);

    // Wait for consumers to finish processing
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        g_thread_join(consumers[i]);
    }

    // Verify all blocks were consumed
    g_assert_cmpuint(ctx.consumed_count, ==, NUM_BLOCKS);

    // Cleanup
    ringbuf_free(ctx.rb);
    g_mutex_clear(&ctx.mutex);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ringbuf/multi_consumer_reserve_commit", test_multi_consumer_reserve_commit);
    return g_test_run();
}