#include "vendor/unity/src/unity.h"

void test_entry_duration_seconds_handles_invalid_ranges(void);
void test_stage_thresholds_and_labels(void);
void test_formatters_clamp_negative_and_format_values(void);
void test_running_fast_at_target_logic(void);
void test_local_day_start_returns_midnight_utc(void);
void test_streak_empty_input_resets_all_fields(void);
void test_streak_single_recent_completion_sets_current_and_longest(void);
void test_streak_longest_can_exceed_current_when_sequence_breaks(void);
void test_streak_drops_to_zero_after_missing_more_than_one_day(void);

void test_history_chunk_fits_into_one_persist_value(void);
void test_history_chunks_cover_the_whole_history_array(void);
void test_history_chunk_entry_count_splits_history(void);
void test_history_chunk_entry_count_rejects_out_of_range_input(void);

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_entry_duration_seconds_handles_invalid_ranges);
  RUN_TEST(test_stage_thresholds_and_labels);
  RUN_TEST(test_formatters_clamp_negative_and_format_values);
  RUN_TEST(test_running_fast_at_target_logic);
  RUN_TEST(test_local_day_start_returns_midnight_utc);
  RUN_TEST(test_streak_empty_input_resets_all_fields);
  RUN_TEST(test_streak_single_recent_completion_sets_current_and_longest);
  RUN_TEST(test_streak_longest_can_exceed_current_when_sequence_breaks);
  RUN_TEST(test_streak_drops_to_zero_after_missing_more_than_one_day);

  RUN_TEST(test_history_chunk_fits_into_one_persist_value);
  RUN_TEST(test_history_chunks_cover_the_whole_history_array);
  RUN_TEST(test_history_chunk_entry_count_splits_history);
  RUN_TEST(test_history_chunk_entry_count_rejects_out_of_range_input);

  return UNITY_END();
}
