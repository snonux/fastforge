#include "fastforge_internal.h"

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

#ifdef DEBUG
time_t fastforge_now(void) {
  return time(NULL) + (s_fake_time_enabled ? s_fake_time_offset_seconds : 0);
}
#else
time_t fastforge_now(void) {
  return time(NULL);
}
#endif

static void recompute_streak_data_from_history(void) {
  fastforge_streak_recompute(history, history_count, fastforge_now(), &streak_data);
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

bool running_current_fast_is_at_target(time_t now) {
  return running_fast_is_at_target(&current_fast, now);
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
