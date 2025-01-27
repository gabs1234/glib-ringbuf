#include "../ringbuf.h"
#include "test.h"
#include <glib.h>
#include <locale.h>

// Helper to generate test data
static void fill_buffer(guint8 *buf, gsize size, guint8 seed) {
    for (gsize i = 0; i < size; i++) {
        buf[i] = seed + i;
    }
}

// Basic creation/destruction test
static void test_ringbuf_create(void) {
    ringbuf_t *rb = ringbuf_new(PLATFORM_MAX_BYTES, TRUE);
    g_assert_nonnull(rb);
    g_assert_cmpuint(ringbuf_buffer_size(rb), ==, PLATFORM_MAX_BYTES);
    ringbuf_free(rb);
}

// Test buffer size rounding
static void test_buffer_size_rounding(void) {
    ringbuf_t *rb = ringbuf_new(PLATFORM_MIN_BYTES/2, TRUE);
    g_assert_cmpuint(ringbuf_buffer_size(rb), >=, PLATFORM_MIN_BYTES);
    ringbuf_free(rb);
}

// Basic push/pop workflow
static void test_push_pop(void) {
    ringbuf_t *rb = ringbuf_new(PLATFORM_MAX_BYTES, TRUE);
    guint8 test_data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    
    // Push
    gpointer result = ringbuf_push(rb, test_data, sizeof(test_data));
    g_assert_nonnull(result);
    g_assert_cmpuint(ringbuf_bytes_free(rb), ==, PLATFORM_MAX_BYTES - sizeof(test_data));
    
    // Pop
    guint8 read_buf[4];
    result = ringbuf_pop(read_buf, rb, sizeof(read_buf));
    g_assert_nonnull(result);
    g_assert_true(memcmp(test_data, read_buf, sizeof(test_data)) == 0);
    
    ringbuf_free(rb);
}

// Test reserve/commit workflow
static void test_reserve_commit(void) {
    ringbuf_t *rb = ringbuf_new(PLATFORM_MAX_BYTES, TRUE);
    const gsize test_size = 4;
    
    // Reserve space and write directly
    guint8 *reserved = ringbuf_reserve(rb, test_size);
    g_assert_nonnull(reserved);
    fill_buffer(reserved, test_size, 0x10);
    ringbuf_commit(rb, test_size);
    
    // Verify
    guint8 read_buf[4];
    ringbuf_pop(read_buf, rb, test_size);
    g_assert_true(memcmp(reserved, read_buf, test_size) == 0);
    
    ringbuf_free(rb);
}

// Test full buffer behavior
static void test_buffer_full(void) {
    ringbuf_t *rb = ringbuf_new(PLATFORM_MIN_BYTES, TRUE);
    guint8 *data = g_malloc(PLATFORM_MIN_BYTES);
    
    // Fill buffer completely
    gpointer result = ringbuf_push(rb, data, PLATFORM_MIN_BYTES);
    g_print ("free bytes: %ld\n", ringbuf_bytes_free(rb));
    g_assert_nonnull(result);
    g_assert_true(ringbuf_is_full(rb) == TRUE);
    
    // Next push should block (test with timeout version)
    result = ringbuf_timed_pop(NULL, rb, PLATFORM_MIN_BYTES, 1000); // Wait 1ms
    g_assert_null(result);
    
    g_free(data);
    ringbuf_free(rb);
}

// Test wraparound behavior
static void test_wraparound(void) {
    ringbuf_t *rb = ringbuf_new(PLATFORM_MAX_BYTES, TRUE);
    const gsize chunk = PLATFORM_MAX_BYTES / 2;
    guint8 *data = g_malloc(chunk);
    
    // First write (fills half)
    ringbuf_push(rb, data, chunk);
    
    // Read and write
    ringbuf_pop(data, rb, chunk);

    // Second write (wraps around)
    ringbuf_push(rb, data, chunk);
    
    // Verify head/tail positions
    g_assert_true(ringbuf_head(rb) < ringbuf_tail(rb));

    ringbuf_push(rb, data, chunk);
    
    // Verify its full
    g_assert_true(ringbuf_is_full(rb) == TRUE);
    
    g_free(data);
    ringbuf_free(rb);
}

// Test concurrent producer-consumer
typedef struct {
    ringbuf_t *rb;
    guint8 test_data[4];
} ThreadData;

static gpointer producer_thread(gpointer user_data) {
    ThreadData *td = user_data;
    ringbuf_push(td->rb, td->test_data, sizeof(td->test_data));
    return NULL;
}

static void test_concurrency(void) {
    ringbuf_t *rb = ringbuf_new(PLATFORM_MAX_BYTES, TRUE);
    ThreadData td = {rb, {0xDE, 0xAD, 0xBE, 0xEF}};
    
    GThread *producer = g_thread_new("producer", producer_thread, &td);
    guint8 read_buf[4];
    ringbuf_pop(read_buf, rb, sizeof(read_buf));
    
    g_thread_join(producer);
    g_assert_true(memcmp(td.test_data, read_buf, sizeof(td.test_data)) == 0);
    
    ringbuf_free(rb);
}

// Test timed_pop with timeout
static void test_timed_pop(void) {
    ringbuf_t *rb = ringbuf_new(PLATFORM_MAX_BYTES, TRUE);
    guint8 data;
    
    // Should timeout immediately
    gpointer result = ringbuf_timed_pop(&data, rb, 1, 1000);
    g_assert_null(result);
    
    ringbuf_free(rb);
}

// Test partial commit after reserve
static void test_reserve_commit_partial(void) {
    ringbuf_t *rb = ringbuf_new(PLATFORM_MAX_BYTES, TRUE);
    const gsize reserved_size = PLATFORM_MAX_BYTES;
    const gsize commit_size = PLATFORM_MAX_BYTES / 2;
    
    guint8 *reserved = ringbuf_reserve(rb, reserved_size);
    g_assert_nonnull(reserved);
    ringbuf_commit(rb, commit_size);
    
    g_assert_cmpuint(ringbuf_bytes_free(rb), ==, PLATFORM_MAX_BYTES - commit_size);
    
    ringbuf_free(rb);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    
    // Add test cases
    g_test_add_func("/ringbuf/create", test_ringbuf_create);
    g_test_add_func("/ringbuf/size_rounding", test_buffer_size_rounding);
    g_test_add_func("/ringbuf/push_pop", test_push_pop);
    g_test_add_func("/ringbuf/reserve_commit", test_reserve_commit);
    g_test_add_func("/ringbuf/full", test_buffer_full);
    g_test_add_func("/ringbuf/wraparound", test_wraparound);
    g_test_add_func("/ringbuf/concurrency", test_concurrency);
    g_test_add_func("/ringbuf/timed_pop", test_timed_pop);
    g_test_add_func("/ringbuf/partial_commit", test_reserve_commit_partial);
    
    return g_test_run();
}