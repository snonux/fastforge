#include "fastforge_internal.h"

#include <stdlib.h>
#include <string.h>

FastEntry history[MAX_FASTS];
int history_count = 0;
FastEntry current_fast = {0};
uint16_t global_target_minutes = DEFAULT_TARGET_MINUTES;
bool developer_mode_enabled = false;
StreakData streak_data = {0};
AppTimer *alarm_timer = NULL;
time_t target_time = 0;

#ifdef DEBUG
bool s_fake_time_enabled = false;
int32_t s_fake_time_offset_seconds = 0;
int32_t s_current_fast_origin_offset_seconds = 0;
#endif

static time_t s_last_streak_refresh_day = 0;
static const uint32_t s_alarm_vibe[] = {200, 100, 200, 100, 400, 100, 200};
static VibePattern s_alarm_pattern = {
  .durations = s_alarm_vibe,
  .num_segments = ARRAY_LENGTH(s_alarm_vibe),
};

static int clamp_history_count(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > MAX_FASTS) {
    return MAX_FASTS;
  }
  return value;
}

static void normalize_loaded_data(void) {
  if (global_target_minutes == 0) {
    global_target_minutes = DEFAULT_TARGET_MINUTES;
  }

  if (current_fast.start_time != 0 && current_fast.target_minutes == 0) {
    current_fast.target_minutes = global_target_minutes;
  }

  if (current_fast.end_time != 0 && current_fast.end_time < current_fast.start_time) {
    current_fast.end_time = 0;
  }
}

time_t local_day_start(time_t timestamp) {
  if (timestamp <= 0) {
    return 0;
  }

  struct tm tm_copy;
  struct tm *tm_info = localtime(&timestamp);
  if (!tm_info) {
    return 0;
  }

  tm_copy = *tm_info;
  tm_copy.tm_hour = 0;
  tm_copy.tm_min = 0;
  tm_copy.tm_sec = 0;
  tm_copy.tm_isdst = -1;
  return mktime(&tm_copy);
}

static int compare_time_t_ascending(const void *a, const void *b) {
  const time_t time_a = *(const time_t *)a;
  const time_t time_b = *(const time_t *)b;
  if (time_a < time_b) {
    return -1;
  }
  if (time_a > time_b) {
    return 1;
  }
  return 0;
}

static bool is_next_local_day(time_t first_day, time_t second_day) {
  if (first_day <= 0 || second_day <= 0 || second_day < first_day) {
    return false;
  }

  struct tm *tm_info = localtime(&first_day);
  if (!tm_info) {
    return false;
  }

  struct tm next_day_tm = *tm_info;
  next_day_tm.tm_mday += 1;
  next_day_tm.tm_hour = 0;
  next_day_tm.tm_min = 0;
  next_day_tm.tm_sec = 0;
  next_day_tm.tm_isdst = -1;
  time_t expected_next_day = mktime(&next_day_tm);
  return expected_next_day == second_day;
}

#ifdef DEBUG
time_t fastforge_now(void) {
  return time(NULL) + (s_fake_time_enabled ? s_fake_time_offset_seconds : 0);
}
#else
time_t fastforge_now(void) {
  return time(NULL);
}
#endif

static int collect_completion_days(time_t *completion_days, int max_days) {
  int completion_day_count = 0;

  for (int i = 0; i < history_count && completion_day_count < max_days; i++) {
    const FastEntry *entry = &history[i];
    time_t duration = entry_duration_seconds(entry);
    if (duration <= 0 || entry->end_time <= 0) {
      continue;
    }

    if (entry->end_time > streak_data.last_completed_fast_end) {
      streak_data.last_completed_fast_end = entry->end_time;
    }

    time_t day_start = local_day_start(entry->end_time);
    if (day_start <= 0) {
      continue;
    }
    completion_days[completion_day_count++] = day_start;
  }

  return completion_day_count;
}

static void apply_completion_days_to_streaks(time_t *completion_days, int completion_day_count) {
  qsort(completion_days, (size_t)completion_day_count, sizeof(time_t), compare_time_t_ascending);

  uint16_t run_length = 0;
  uint16_t longest = 0;
  for (int i = 0; i < completion_day_count; i++) {
    if (i == 0 || completion_days[i] == completion_days[i - 1]) {
      if (i == 0) {
        run_length = 1;
      }
      continue;
    }

    if (is_next_local_day(completion_days[i - 1], completion_days[i])) {
      run_length++;
    } else {
      run_length = 1;
    }

    if (run_length > longest) {
      longest = run_length;
    }
  }

  if (longest == 0) {
    longest = 1;
  }

  time_t today_day_start = local_day_start(fastforge_now());
  time_t last_completion_day = completion_days[completion_day_count - 1];
  if (last_completion_day == today_day_start ||
      is_next_local_day(last_completion_day, today_day_start)) {
    streak_data.current_streak = run_length;
  } else {
    streak_data.current_streak = 0;
  }
  streak_data.longest_streak = longest;
}

static void recompute_streak_data_from_history(void) {
  streak_data.current_streak = 0;
  streak_data.longest_streak = 0;
  streak_data.last_completed_fast_end = 0;

  if (history_count <= 0) {
    return;
  }

  time_t completion_days[MAX_FASTS];
  int completion_day_count = collect_completion_days(completion_days, MAX_FASTS);
  if (completion_day_count == 0) {
    return;
  }

  apply_completion_days_to_streaks(completion_days, completion_day_count);
}

void recompute_streak_data_for_today(void) {
  recompute_streak_data_from_history();
  s_last_streak_refresh_day = local_day_start(fastforge_now());
}

bool refresh_streak_if_day_changed(void) {
  time_t today_day_start = local_day_start(fastforge_now());
  if (today_day_start <= 0 || today_day_start == s_last_streak_refresh_day) {
    return false;
  }

  recompute_streak_data_for_today();
  save_all_data();
  return true;
}

void save_all_data(void) {
  persist_write_int(KEY_HISTORY_COUNT, history_count);
  persist_write_data(KEY_HISTORY_DATA, history, sizeof(FastEntry) * history_count);
  persist_write_data(KEY_CURRENT_FAST, &current_fast, sizeof(FastEntry));
  persist_write_int(KEY_TARGET_MIN, global_target_minutes);
  persist_write_data(KEY_STREAK_DATA, &streak_data, sizeof(StreakData));
  persist_write_bool(KEY_DEV_MODE, developer_mode_enabled);
#ifdef DEBUG
  persist_write_int(KEY_DEBUG_FAKE_OFFSET, s_fake_time_offset_seconds);
  persist_write_int(KEY_DEBUG_FAST_ORIGIN, s_current_fast_origin_offset_seconds);
#endif
}

static void reset_loaded_data(void) {
  history_count = 0;
  memset(history, 0, sizeof(history));
  memset(&current_fast, 0, sizeof(current_fast));
  global_target_minutes = DEFAULT_TARGET_MINUTES;
  developer_mode_enabled = false;
  memset(&streak_data, 0, sizeof(streak_data));
#ifdef DEBUG
  s_fake_time_enabled = false;
  s_fake_time_offset_seconds = 0;
  s_current_fast_origin_offset_seconds = 0;
#endif
}

static void load_persisted_base_data(void) {
  if (persist_exists(KEY_HISTORY_COUNT)) {
    history_count = clamp_history_count(persist_read_int(KEY_HISTORY_COUNT));
  }
  if (history_count > 0 && persist_exists(KEY_HISTORY_DATA)) {
    const int expected_size = sizeof(FastEntry) * history_count;
    const int read_size = persist_read_data(KEY_HISTORY_DATA, history, expected_size);
    if (read_size != expected_size) {
      history_count = 0;
      memset(history, 0, sizeof(history));
    }
  }
  if (persist_exists(KEY_CURRENT_FAST)) {
    const int read_size = persist_read_data(KEY_CURRENT_FAST, &current_fast, sizeof(FastEntry));
    if (read_size != (int)sizeof(FastEntry)) {
      memset(&current_fast, 0, sizeof(current_fast));
    }
  }
  if (persist_exists(KEY_TARGET_MIN)) {
    const int read_target = persist_read_int(KEY_TARGET_MIN);
    if (read_target > 0 && read_target <= UINT16_MAX) {
      global_target_minutes = (uint16_t)read_target;
    }
  }
  if (persist_exists(KEY_STREAK_DATA)) {
    const int read_size = persist_read_data(KEY_STREAK_DATA, &streak_data, sizeof(StreakData));
    if (read_size != (int)sizeof(StreakData)) {
      memset(&streak_data, 0, sizeof(streak_data));
    }
  }
  if (persist_exists(KEY_DEV_MODE)) {
    developer_mode_enabled = persist_read_bool(KEY_DEV_MODE);
  }
}

static void load_persisted_debug_data(void) {
#ifdef DEBUG
  if (persist_exists(KEY_DEBUG_FAKE_OFFSET)) {
    s_fake_time_offset_seconds = persist_read_int(KEY_DEBUG_FAKE_OFFSET);
    s_fake_time_enabled = s_fake_time_offset_seconds != 0;
  }
  if (persist_exists(KEY_DEBUG_FAST_ORIGIN)) {
    s_current_fast_origin_offset_seconds = persist_read_int(KEY_DEBUG_FAST_ORIGIN);
  }
#endif
}

static void finish_loaded_data(void) {
  normalize_loaded_data();
  recompute_streak_data_for_today();
}

void load_all_data(void) {
  reset_loaded_data();
  load_persisted_base_data();
  load_persisted_debug_data();
  finish_loaded_data();
}

bool fast_is_running(void) {
  return current_fast.start_time != 0;
}

void format_hhmmss(time_t seconds, char *buffer, size_t size) {
  if (seconds < 0) {
    seconds = 0;
  }
  snprintf(buffer, size, "%02d:%02d:%02d",
           (int)(seconds / 3600),
           (int)((seconds % 3600) / 60),
           (int)(seconds % 60));
}

void format_duration_hours_minutes(time_t seconds, char *buffer, size_t size) {
  if (seconds < 0) {
    seconds = 0;
  }
  int hours = (int)(seconds / 3600);
  int minutes = (int)((seconds % 3600) / 60);
  snprintf(buffer, size, "%dh %02dm", hours, minutes);
}

time_t entry_duration_seconds(const FastEntry *entry) {
  if (!entry || entry->start_time == 0 || entry->end_time <= entry->start_time) {
    return 0;
  }
  return entry->end_time - entry->start_time;
}

void format_entry_datetime(time_t timestamp, char *buffer, size_t size) {
  if (!buffer || size == 0) {
    return;
  }
  if (timestamp <= 0) {
    snprintf(buffer, size, "--");
    return;
  }

  struct tm *tm_info = localtime(&timestamp);
  if (!tm_info) {
    snprintf(buffer, size, "--");
    return;
  }
  strftime(buffer, size, "%b %d %H:%M", tm_info);
}

uint8_t stage_level_for_elapsed(time_t elapsed_seconds) {
  if (elapsed_seconds >= 24 * 3600) {
    return 3;
  }
  if (elapsed_seconds >= 18 * 3600) {
    return 2;
  }
  if (elapsed_seconds >= 12 * 3600) {
    return 1;
  }
  return 0;
}

const char *stage_text_for_elapsed(time_t elapsed_seconds) {
  if (elapsed_seconds >= 24 * 3600) {
    return "DEEP KETOSIS";
  }
  if (elapsed_seconds >= 18 * 3600) {
    return "EARLY KETOSIS";
  }
  if (elapsed_seconds >= 12 * 3600) {
    return "FAT BURN";
  }
  return "GLYCOGEN";
}

void update_max_stage_if_needed(time_t elapsed_seconds) {
  if (!fast_is_running()) {
    return;
  }

  uint8_t stage_level = stage_level_for_elapsed(elapsed_seconds);
  if (stage_level > current_fast.max_stage_reached) {
    current_fast.max_stage_reached = stage_level;
    save_all_data();
  }
}

bool running_fast_is_at_target(time_t now) {
  if (!fast_is_running() || current_fast.target_minutes == 0) {
    return false;
  }
  return now >= current_fast.start_time + (time_t)current_fast.target_minutes * 60;
}

static void append_history_entry(const FastEntry *entry) {
  if (!entry) {
    return;
  }

  if (history_count < MAX_FASTS) {
    history[history_count++] = *entry;
    return;
  }

  memmove(&history[0], &history[1], sizeof(FastEntry) * (MAX_FASTS - 1));
  history[MAX_FASTS - 1] = *entry;
}

static uint16_t resolve_target_minutes(uint16_t preset_target_minutes) {
  if (preset_target_minutes > 0) {
    return preset_target_minutes;
  }
  if (global_target_minutes == 0) {
    global_target_minutes = DEFAULT_TARGET_MINUTES;
  }
  return global_target_minutes;
}

static void alarm_callback(void *data);

void schedule_alarm_if_needed(void) {
  if (alarm_timer) {
    app_timer_cancel(alarm_timer);
    alarm_timer = NULL;
  }

  if (!fast_is_running() || current_fast.target_minutes == 0) {
    target_time = 0;
    return;
  }

  target_time = current_fast.start_time + (time_t)current_fast.target_minutes * 60;
  time_t now = fastforge_now();
  if (target_time <= now) {
    alarm_callback(NULL);
    return;
  }

  uint32_t ms_until_alarm = (uint32_t)(target_time - now) * 1000;
  alarm_timer = app_timer_register(ms_until_alarm, alarm_callback, NULL);
}

bool fast_start(uint16_t preset_target_minutes) {
  if (fast_is_running()) {
    return false;
  }

  current_fast.start_time = fastforge_now();
  current_fast.end_time = 0;
  current_fast.target_minutes = resolve_target_minutes(preset_target_minutes);
  memset(current_fast.note, 0, sizeof(current_fast.note));
  current_fast.max_stage_reached = 0;
#ifdef DEBUG
  s_current_fast_origin_offset_seconds = s_fake_time_enabled ? s_fake_time_offset_seconds : 0;
#endif
  save_all_data();
  schedule_alarm_if_needed();
  APP_LOG(APP_LOG_LEVEL_INFO, "Started fast at %ld target=%u",
          (long)current_fast.start_time, current_fast.target_minutes);
  return true;
}

bool fast_stop(void) {
  if (!fast_is_running()) {
    return false;
  }

  FastEntry completed = current_fast;
  completed.end_time = fastforge_now();
  if (completed.end_time < completed.start_time) {
    completed.end_time = completed.start_time;
  }
  append_history_entry(&completed);
  sort_history_by_end_time();
  recompute_streak_data_for_today();
  memset(&current_fast, 0, sizeof(current_fast));
#ifdef DEBUG
  s_current_fast_origin_offset_seconds = 0;
#endif
  if (alarm_timer) {
    app_timer_cancel(alarm_timer);
    alarm_timer = NULL;
  }
  target_time = 0;
  save_all_data();
  history_menu_reload();
  APP_LOG(APP_LOG_LEVEL_INFO, "Stopped fast and saved history_count=%d", history_count);
  return true;
}

static void alarm_callback(void *data) {
  (void)data;
  alarm_timer = NULL;
  target_time = 0;
  vibes_enqueue_custom_pattern(s_alarm_pattern);
  light_enable_interaction();
  show_goal_reached_window();
  refresh_all_ui_state();
}

void fastforge_force_goal_alarm(void) {
  alarm_callback(NULL);
}
