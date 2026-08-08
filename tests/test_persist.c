/* Tests for the history persistence chunking arithmetic.
 *
 * Regression cover for the bug where the whole history array was written to a
 * single persist key: Pebble rejects values larger than 256 bytes, so from the
 * 6th completed fast on nothing was stored and the next launch wiped the
 * history. History is now split into chunks that each fit one persist value. */

#include "vendor/unity/src/unity.h"

#include "fastforge_logic.h"
#include "fastforge_types.h"

void test_history_chunk_fits_into_one_persist_value(void);
void test_history_chunks_cover_the_whole_history_array(void);
void test_history_chunk_entry_count_splits_history(void);
void test_history_chunk_entry_count_rejects_out_of_range_input(void);

/* The whole point of chunking: a chunk must never exceed the persist limit. */
void test_history_chunk_fits_into_one_persist_value(void) {
  TEST_ASSERT_GREATER_THAN_INT(0, HISTORY_ENTRIES_PER_CHUNK);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(FASTFORGE_PERSIST_MAX_BYTES,
                                 sizeof(FastEntry) * (size_t)HISTORY_ENTRIES_PER_CHUNK);
}

/* Enough chunk keys must exist to hold a completely full history. */
void test_history_chunks_cover_the_whole_history_array(void) {
  TEST_ASSERT_GREATER_OR_EQUAL_INT(MAX_FASTS,
                                   HISTORY_CHUNK_COUNT * HISTORY_ENTRIES_PER_CHUNK);
  TEST_ASSERT_LESS_THAN_INT(MAX_FASTS + HISTORY_ENTRIES_PER_CHUNK,
                            HISTORY_CHUNK_COUNT * HISTORY_ENTRIES_PER_CHUNK);
}

/* Chunks fill front to back; the last one holds the remainder. */
void test_history_chunk_entry_count_splits_history(void) {
  const int per_chunk = HISTORY_ENTRIES_PER_CHUNK;

  /* A history shorter than one chunk lives entirely in chunk 0. */
  TEST_ASSERT_EQUAL_INT(1, history_chunk_entry_count(1, 0));
  TEST_ASSERT_EQUAL_INT(0, history_chunk_entry_count(1, 1));

  /* An exact multiple fills whole chunks and leaves the next one empty. */
  TEST_ASSERT_EQUAL_INT(per_chunk, history_chunk_entry_count(2 * per_chunk, 0));
  TEST_ASSERT_EQUAL_INT(per_chunk, history_chunk_entry_count(2 * per_chunk, 1));
  TEST_ASSERT_EQUAL_INT(0, history_chunk_entry_count(2 * per_chunk, 2));

  /* A remainder ends up alone in the final chunk. */
  TEST_ASSERT_EQUAL_INT(per_chunk, history_chunk_entry_count(per_chunk + 1, 0));
  TEST_ASSERT_EQUAL_INT(1, history_chunk_entry_count(per_chunk + 1, 1));

  /* Summing all chunks reproduces a full history exactly. */
  int total = 0;
  for (int chunk = 0; chunk < HISTORY_CHUNK_COUNT; chunk++) {
    total += history_chunk_entry_count(MAX_FASTS, chunk);
  }
  TEST_ASSERT_EQUAL_INT(MAX_FASTS, total);
}

/* Defensive clamping so a corrupted persisted count cannot index past the array. */
void test_history_chunk_entry_count_rejects_out_of_range_input(void) {
  TEST_ASSERT_EQUAL_INT(0, history_chunk_entry_count(0, 0));
  TEST_ASSERT_EQUAL_INT(0, history_chunk_entry_count(-5, 0));
  TEST_ASSERT_EQUAL_INT(0, history_chunk_entry_count(10, -1));
  TEST_ASSERT_EQUAL_INT(0, history_chunk_entry_count(MAX_FASTS, HISTORY_CHUNK_COUNT));

  int total = 0;
  for (int chunk = 0; chunk < HISTORY_CHUNK_COUNT; chunk++) {
    total += history_chunk_entry_count(MAX_FASTS + 100, chunk);
  }
  TEST_ASSERT_EQUAL_INT(MAX_FASTS, total);
}
