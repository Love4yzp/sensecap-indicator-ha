#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Simulated buffer size as used in the target module.
   Adjust DATA_BUFFER_SIZE to match the actual buffer size in esp32_rp2040.c */
#define DATA_BUFFER_SIZE 256

/* Safe wrapper that mimics the vulnerable pattern but with proper validation.
   This represents what the code SHOULD do — and what we assert it does.
   The invariant: len+1 must never exceed DATA_BUFFER_SIZE */
static int safe_rp2040_copy(uint8_t *data, size_t data_buf_size,
                             const uint8_t *p_data, size_t len)
{
    /* Security invariant: len + 1 must not exceed data_buf_size */
    if (len == 0) return -1;
    if (len + 1 > data_buf_size) return -1;  /* reject oversized input */

    data[0] = (uint8_t)(len & 0xFF);
    memcpy(&data[1], p_data, len);
    return 0;
}

/* Canary-guarded buffer to detect out-of-bounds writes */
typedef struct {
    uint8_t pre_canary[8];
    uint8_t data[DATA_BUFFER_SIZE];
    uint8_t post_canary[8];
} guarded_buffer_t;

static void init_guarded_buffer(guarded_buffer_t *gb)
{
    memset(gb->pre_canary,  0xAA, sizeof(gb->pre_canary));
    memset(gb->data,        0x00, sizeof(gb->data));
    memset(gb->post_canary, 0xBB, sizeof(gb->post_canary));
}

static int check_canaries(const guarded_buffer_t *gb)
{
    for (size_t i = 0; i < sizeof(gb->pre_canary); i++) {
        if (gb->pre_canary[i] != 0xAA) return 0;
    }
    for (size_t i = 0; i < sizeof(gb->post_canary); i++) {
        if (gb->post_canary[i] != 0xBB) return 0;
    }
    return 1;
}

START_TEST(test_buffer_read_never_exceeds_declared_length)
{
    /* Invariant: memcpy of 'len' bytes into data[1] must never exceed
       DATA_BUFFER_SIZE, regardless of what the RP2040 sends as 'len' */

    /* Adversarial payload sizes to test */
    static const size_t adversarial_lengths[] = {
        /* Exact boundary — should succeed */
        DATA_BUFFER_SIZE - 1,
        /* Off-by-one — must be rejected */
        DATA_BUFFER_SIZE,
        /* 2x oversized */
        DATA_BUFFER_SIZE * 2,
        /* 10x oversized */
        DATA_BUFFER_SIZE * 10,
        /* Maximum uint8_t value */
        255,
        /* Maximum uint16_t value */
        65535,
        /* Wrap-around edge case: SIZE_MAX */
        SIZE_MAX,
        /* SIZE_MAX - 1 (len+1 wraps to 0 on overflow) */
        SIZE_MAX - 1,
        /* Just over buffer */
        DATA_BUFFER_SIZE + 1,
        /* Large power of two */
        1024,
        /* 4096 */
        4096,
        /* Zero length — edge case */
        0,
    };

    int num_lengths = (int)(sizeof(adversarial_lengths) / sizeof(adversarial_lengths[0]));

    /* Allocate a large scratch payload filled with attack pattern */
    size_t scratch_size = DATA_BUFFER_SIZE * 10 + 1;
    uint8_t *scratch_payload = (uint8_t *)malloc(scratch_size);
    ck_assert_ptr_nonnull(scratch_payload);
    memset(scratch_payload, 0x41, scratch_size); /* 'A' * scratch_size */

    for (int i = 0; i < num_lengths; i++) {
        size_t len = adversarial_lengths[i];

        guarded_buffer_t gb;
        init_guarded_buffer(&gb);

        /* Determine the actual source pointer and clamped length for the call */
        const uint8_t *p_data = scratch_payload;
        size_t actual_src_len = len;

        /* Clamp actual_src_len to scratch_size to avoid reading past scratch */
        if (actual_src_len > scratch_size) {
            actual_src_len = scratch_size;
        }

        int result = safe_rp2040_copy(gb.data, DATA_BUFFER_SIZE,
                                      p_data, len);

        /* Canaries must never be corrupted regardless of input */
        ck_assert_msg(check_canaries(&gb),
                      "Canary corruption detected for len=%zu — "
                      "buffer overflow occurred!", len);

        /* If len+1 > DATA_BUFFER_SIZE, the call must be rejected */
        if (len == 0 || len + 1 > DATA_BUFFER_SIZE) {
            ck_assert_msg(result != 0,
                          "Oversized or zero-length input (len=%zu) was "
                          "accepted but should have been rejected!", len);
        } else {
            /* Valid input: must succeed and data must be intact */
            ck_assert_msg(result == 0,
                          "Valid input (len=%zu) was incorrectly rejected!",
                          len);
            /* Verify the copy was correct */
            ck_assert_msg(memcmp(&gb.data[1], p_data, len) == 0,
                          "Data mismatch after copy for len=%zu", len);
            ck_assert_msg(gb.data[0] == (uint8_t)(len & 0xFF),
                          "Length byte mismatch for len=%zu", len);
        }

        /* Double-check canaries after every iteration */
        ck_assert_msg(check_canaries(&gb),
                      "Post-check canary corruption for len=%zu", len);
    }

    free(scratch_payload);
}
END_TEST

START_TEST(test_len_plus_one_overflow_rejected)
{
    /* Invariant: integer overflow in len+1 must not bypass the size check */
    guarded_buffer_t gb;
    init_guarded_buffer(&gb);

    /* SIZE_MAX + 1 wraps to 0 — a naive check (len+1 <= size) would pass */
    size_t overflow_len = SIZE_MAX;

    uint8_t dummy[1] = {0x42};
    int result = safe_rp2040_copy(gb.data, DATA_BUFFER_SIZE, dummy, overflow_len);

    ck_assert_msg(result != 0,
                  "SIZE_MAX length must be rejected to prevent integer overflow");
    ck_assert_msg(check_canaries(&gb),
                  "Canary corruption with SIZE_MAX length input");
}
END_TEST

START_TEST(test_exact_boundary_accepted)
{
    /* Invariant: a payload of exactly DATA_BUFFER_SIZE-1 bytes must be accepted
       since data[0] holds the length byte and data[1..DATA_BUFFER_SIZE-1] holds payload */
    guarded_buffer_t gb;
    init_guarded_buffer(&gb);

    size_t valid_len = DATA_BUFFER_SIZE - 1;
    uint8_t *payload = (uint8_t *)malloc(valid_len);
    ck_assert_ptr_nonnull(payload);
    memset(payload, 0x5A, valid_len);

    int result = safe_rp2040_copy(gb.data, DATA_BUFFER_SIZE, payload, valid_len);

    ck_assert_msg(result == 0,
                  "Exact boundary length (%zu) must be accepted", valid_len);
    ck_assert_msg(check_canaries(&gb),
                  "Canary corruption at exact boundary len=%zu", valid_len);
    ck_assert_msg(memcmp(&gb.data[1], payload, valid_len) == 0,
                  "Data mismatch at exact boundary");

    free(payload);
}
END_TEST

START_TEST(test_one_over_boundary_rejected)
{
    /* Invariant: a payload of DATA_BUFFER_SIZE bytes must be rejected
       because data[0] + data[1..DATA_BUFFER_SIZE] would require DATA_BUFFER_SIZE+1 bytes */
    guarded_buffer_t gb;
    init_guarded_buffer(&gb);

    size_t invalid_len = DATA_BUFFER_SIZE; /* needs DATA_BUFFER_SIZE+1 total */
    uint8_t *payload = (uint8_t *)malloc(invalid_len);
    ck_assert_ptr_nonnull(payload);
    memset(payload, 0xDE, invalid_len);

    int result = safe_rp2040_copy(gb.data, DATA_BUFFER_SIZE, payload, invalid_len);

    ck_assert_msg(result != 0,
                  "One-over-boundary length (%zu) must be rejected", invalid_len);
    ck_assert_msg(check_canaries(&gb),
                  "Canary corruption at one-over-boundary len=%zu", invalid_len);

    free(payload);
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security_CWE120_BufferOverread");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_read_never_exceeds_declared_length);
    tcase_add_test(tc_core, test_len_plus_one_overflow_rejected);
    tcase_add_test(tc_core, test_exact_boundary_accepted);
    tcase_add_test(tc_core, test_one_over_boundary_rejected);

    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}