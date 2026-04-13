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

void test_csv_append_text_plain_and_escaped_values(void);
void test_csv_append_int_appends_number_text(void);
void test_format_history_csv_header_and_row_with_escaping(void);
void test_format_history_csv_row_null_entry_returns_empty(void);

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

  RUN_TEST(test_csv_append_text_plain_and_escaped_values);
  RUN_TEST(test_csv_append_int_appends_number_text);
  RUN_TEST(test_format_history_csv_header_and_row_with_escaping);
  RUN_TEST(test_format_history_csv_row_null_entry_returns_empty);

  return UNITY_END();
}
