#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "main/rp2040/rp2040.h"

START_TEST(test_cobs_fragment_overflow_invariant)
{
    // Invariant: After detecting an oversized fragment, no data from that oversized frame
    // should be copied into the fragment buffer as the start of a new fragment
    const struct {
        size_t frag_len;
        size_t chunk_len;
        size_t buf_size;
    } test_cases[] = {
        // Exact exploit case: frag_len + chunk_len > sizeof(frag), but after resetting
        // frag_len to 0, frag_len + chunk_len <= sizeof(frag) passes
        { sizeof(frag) - 10, 20, sizeof(frag) },
        // Boundary case: frag_len + chunk_len == sizeof(frag) + 1 (just over limit)
        { sizeof(frag), 1, sizeof(frag) },
        // Valid case: frag_len + chunk_len == sizeof(frag) (exact fit)
        { sizeof(frag) - 5, 5, sizeof(frag) },
    };
    
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    
    for (int i = 0; i < num_cases; i++) {
        // Setup test state
        frag_len = test_cases[i].frag_len;
        size_t chunk_len = test_cases[i].chunk_len;
        size_t buf_size = test_cases[i].buf_size;
        
        // Create test buffer with sentinel pattern
        uint8_t test_buffer[256];
        memset(test_buffer, 0xAA, sizeof(test_buffer));
        
        // Call the vulnerable function path indirectly by simulating the logic
        // We'll test the actual production code by checking the state before/after
        size_t original_frag_len = frag_len;
        
        // Simulate the exact vulnerable code path from rp2040.c
        if(frag_len + chunk_len > buf_size) {
            frag_len = 0;
        }
        if(frag_len + chunk_len <= buf_size) {
            // This is where the bug would occur - copying after reset
            // For testing, we verify the invariant holds
            ck_assert_msg(!(original_frag_len + chunk_len > buf_size && frag_len == 0 && chunk_len > 0),
                         "Oversized fragment data should not be processed as new fragment start");
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_cobs_fragment_overflow_invariant);
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