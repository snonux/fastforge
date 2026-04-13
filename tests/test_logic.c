#include "vendor/unity/src/unity.h"

#include "../src/c/fastforge_logic.h"

#include "test_helpers.h"

#include <string.h>

void test_entry_duration_seconds_handles_invalid_ranges(void) {
  FastEntry entry = {0};

  TEST_ASSERT_EQUAL_INT64(0, entry_duration_seconds(NULL));

  entry.start_time = 0;
  entry.end_time = 100;
  TEST_ASSERT_EQUAL_INT64(0, entry_duration_seconds(&entry));

  entry.start_time = 200;
  entry.end_time = 200;
  TEST_ASSERT_EQUAL_INT64(0, entry_duration_seconds(&entry));

  entry.start_time = 200;
  entry.end_time = 150;
  TEST_ASSERT_EQUAL_INT64(0, entry_duration_seconds(&entry));

  entry.start_time = 200;
  entry.end_time = 280;
  TEST_ASSERT_EQUAL_INT64(80, entry_duration_seconds(&entry));
}

void test_stage_thresholds_and_labels(void) {
  TEST_ASSERT_EQUAL_UINT8(0, stage_level_for_elapsed(0));
  TEST_ASSERT_EQUAL_UINT8(0, stage_level_for_elapsed((12 * 3600) - 1));
  TEST_ASSERT_EQUAL_UINT8(1, stage_level_for_elapsed(12 * 3600));
  TEST_ASSERT_EQUAL_UINT8(2, stage_level_for_elapsed(18 * 3600));
  TEST_ASSERT_EQUAL_UINT8(3, stage_level_for_elapsed(24 * 3600));

  TEST_ASSERT_EQUAL_STRING("GLYCOGEN", stage_text_for_elapsed(11 * 3600));
  TEST_ASSERT_EQUAL_STRING("FAT BURN", stage_text_for_elapsed(12 * 3600));
  TEST_ASSERT_EQUAL_STRING("EARLY KETOSIS", stage_text_for_elapsed(18 * 3600));
  TEST_ASSERT_EQUAL_STRING("DEEP KETOSIS", stage_text_for_elapsed(24 * 3600));
}

void test_formatters_clamp_negative_and_format_values(void) {
  char buffer[32];

  format_hhmmss(-1, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("00:00:00", buffer);

  format_hhmmss(3661, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("01:01:01", buffer);

  format_duration_hours_minutes(-120, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("0h 00m", buffer);

  format_duration_hours_minutes(7260, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("2h 01m", buffer);
}

void test_running_fast_at_target_logic(void) {
  FastEntry entry = {0};

  TEST_ASSERT_FALSE(running_fast_is_at_target(NULL, 0));

  entry.start_time = 0;
  entry.target_minutes = 16 * 60;
  TEST_ASSERT_FALSE(running_fast_is_at_target(&entry, 100));

  entry.start_time = 100;
  entry.end_time = 200;
  TEST_ASSERT_FALSE(running_fast_is_at_target(&entry, 200));

  entry.end_time = 0;
  entry.target_minutes = 0;
  TEST_ASSERT_FALSE(running_fast_is_at_target(&entry, 1000));

  entry.target_minutes = 10;
  TEST_ASSERT_FALSE(running_fast_is_at_target(&entry, 699));
  TEST_ASSERT_TRUE(running_fast_is_at_target(&entry, 700));
}

void test_local_day_start_returns_midnight_utc(void) {
  const time_t ts = make_utc_time(2026, 4, 5, 17, 20, 10);
  const time_t expected_day_start = make_utc_time(2026, 4, 5, 0, 0, 0);

  TEST_ASSERT_EQUAL_INT64(0, local_day_start(0));
  TEST_ASSERT_EQUAL_INT64(0, local_day_start(-1));
  TEST_ASSERT_EQUAL_INT64(expected_day_start, local_day_start(ts));
}

void test_streak_empty_input_resets_all_fields(void) {
  StreakData streak;
  const time_t now = make_utc_time(2026, 4, 10, 9, 0, 0);

  streak.current_streak = 77;
  streak.longest_streak = 88;
  streak.last_completed_fast_end = 99;

  fastforge_streak_recompute(NULL, 0, now, &streak);

  TEST_ASSERT_EQUAL_UINT16(0, streak.current_streak);
  TEST_ASSERT_EQUAL_UINT16(0, streak.longest_streak);
  TEST_ASSERT_EQUAL_INT64(0, streak.last_completed_fast_end);
}

void test_streak_single_recent_completion_sets_current_and_longest(void) {
  FastEntry entries[1];
  StreakData streak;
  const time_t now = make_utc_time(2026, 4, 10, 12, 0, 0);

  memset(entries, 0, sizeof(entries));
  entries[0].start_time = make_utc_time(2026, 4, 9, 8, 0, 0);
  entries[0].end_time = make_utc_time(2026, 4, 9, 20, 0, 0);

  fastforge_streak_recompute(entries, 1, now, &streak);

  TEST_ASSERT_EQUAL_UINT16(1, streak.current_streak);
  TEST_ASSERT_EQUAL_UINT16(1, streak.longest_streak);
  TEST_ASSERT_EQUAL_INT64(entries[0].end_time, streak.last_completed_fast_end);
}

void test_streak_longest_can_exceed_current_when_sequence_breaks(void) {
  FastEntry entries[4];
  StreakData streak;
  const time_t now = make_utc_time(2026, 4, 4, 23, 0, 0);

  memset(entries, 0, sizeof(entries));
  entries[0].start_time = make_utc_time(2026, 4, 1, 8, 0, 0);
  entries[0].end_time = make_utc_time(2026, 4, 1, 20, 0, 0);
  entries[1].start_time = make_utc_time(2026, 4, 2, 8, 0, 0);
  entries[1].end_time = make_utc_time(2026, 4, 2, 20, 0, 0);
  entries[2].start_time = make_utc_time(2026, 4, 4, 8, 0, 0);
  entries[2].end_time = make_utc_time(2026, 4, 4, 20, 0, 0);
  entries[3].start_time = make_utc_time(2026, 4, 4, 6, 0, 0);
  entries[3].end_time = make_utc_time(2026, 4, 4, 7, 0, 0);

  fastforge_streak_recompute(entries, 4, now, &streak);

  TEST_ASSERT_EQUAL_UINT16(1, streak.current_streak);
  TEST_ASSERT_EQUAL_UINT16(2, streak.longest_streak);
  TEST_ASSERT_EQUAL_INT64(entries[2].end_time, streak.last_completed_fast_end);
}

void test_streak_drops_to_zero_after_missing_more_than_one_day(void) {
  FastEntry entries[2];
  StreakData streak;
  const time_t now = make_utc_time(2026, 4, 10, 12, 0, 0);

  memset(entries, 0, sizeof(entries));
  entries[0].start_time = make_utc_time(2026, 4, 6, 8, 0, 0);
  entries[0].end_time = make_utc_time(2026, 4, 6, 20, 0, 0);
  entries[1].start_time = make_utc_time(2026, 4, 7, 8, 0, 0);
  entries[1].end_time = make_utc_time(2026, 4, 7, 20, 0, 0);

  fastforge_streak_recompute(entries, 2, now, &streak);

  TEST_ASSERT_EQUAL_UINT16(0, streak.current_streak);
  TEST_ASSERT_EQUAL_UINT16(2, streak.longest_streak);
  TEST_ASSERT_EQUAL_INT64(entries[1].end_time, streak.last_completed_fast_end);
}
