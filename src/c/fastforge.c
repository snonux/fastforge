#include <pebble.h>
#include <stdlib.h>
#include <string.h>
#include "fastforge.h"

enum {
  MAIN_MENU_INDEX_START_NEW = 0,
  MAIN_MENU_INDEX_CURRENT_TIMER = 1,
  MAIN_MENU_INDEX_STOP_CURRENT = 2,
  MAIN_MENU_INDEX_HISTORY = 3,
  MAIN_MENU_INDEX_STATS = 4,
  MAIN_MENU_INDEX_SCIENCE = 5,
  MAIN_MENU_INDEX_SETTINGS = 6,
  MAIN_MENU_INDEX_BACKUP = 7,
  MAIN_MENU_ITEM_COUNT = 8
};

enum {
  PRESET_MENU_INDEX_DEFAULT = 0,
  PRESET_MENU_INDEX_16H = 1,
  PRESET_MENU_INDEX_18H = 2,
  PRESET_MENU_INDEX_20H = 3,
  PRESET_MENU_INDEX_24H = 4,
  PRESET_MENU_INDEX_36H = 5,
  PRESET_MENU_ITEM_COUNT = 6
};

typedef enum {
  EDIT_FIELD_START = 0,
  EDIT_FIELD_END = 1
} EditField;

FastEntry history[MAX_FASTS];
int history_count = 0;
FastEntry current_fast = {0};
uint16_t global_target_minutes = DEFAULT_TARGET_MINUTES;
StreakData streak_data = {0};
AppTimer *alarm_timer = NULL;
time_t target_time = 0;

static Window *s_menu_window;
static Window *s_timer_window;
static Window *s_goal_window;
static Window *s_presets_window;
static Window *s_stats_window;
static Window *s_detail_window;
static Window *s_history_window;
static Window *s_history_edit_window;
static Window *s_running_edit_window;

static SimpleMenuLayer *s_main_menu_layer;
static SimpleMenuLayer *s_presets_menu_layer;
static MenuLayer *s_history_menu_layer;
static SimpleMenuSection s_main_menu_sections[1];
static SimpleMenuSection s_presets_menu_sections[1];
static SimpleMenuItem s_main_menu_items[MAIN_MENU_ITEM_COUNT];
static SimpleMenuItem s_presets_menu_items[PRESET_MENU_ITEM_COUNT];

static TextLayer *s_title_layer;
static TextLayer *s_timer_layer;
static TextLayer *s_detail_layer;
static TextLayer *s_stage_layer;
static TextLayer *s_hint_layer;
static Layer *s_progress_layer;

static Layer *s_goal_background_layer;
static TextLayer *s_goal_title_layer;
static TextLayer *s_goal_time_layer;
static TextLayer *s_goal_stage_layer;
static TextLayer *s_goal_hint_layer;

static TextLayer *s_placeholder_title_layer;
static TextLayer *s_placeholder_body_layer;
static TextLayer *s_placeholder_hint_layer;
static TextLayer *s_stats_title_layer;
static TextLayer *s_stats_body_layer;
static TextLayer *s_stats_hint_layer;
static TextLayer *s_history_edit_title_layer;
static TextLayer *s_history_edit_start_layer;
static TextLayer *s_history_edit_end_layer;
static TextLayer *s_history_edit_duration_layer;
static TextLayer *s_history_edit_stage_layer;
static TextLayer *s_history_edit_hint_layer;
static TextLayer *s_running_edit_title_layer;
static TextLayer *s_running_edit_start_layer;
static TextLayer *s_running_edit_elapsed_layer;
static TextLayer *s_running_edit_goal_layer;
static TextLayer *s_running_edit_hint_layer;

static char s_title_text[24];
static char s_timer_text[16];
static char s_detail_text[48];
static char s_stage_text[32];
static char s_goal_time_text[24];
static char s_goal_stage_text[24];
static char s_menu_stop_subtitle[32];
static char s_placeholder_title_text[24];
static char s_placeholder_body_text[160];
static char s_placeholder_hint_text[24];
static char s_stats_body_text[160];
static char s_history_edit_title_text[24];
static char s_history_edit_start_text[32];
static char s_history_edit_end_text[32];
static char s_history_edit_duration_text[32];
static char s_history_edit_stage_text[24];
static char s_history_edit_hint_text[40];
static char s_running_edit_start_text[32];
static char s_running_edit_elapsed_text[32];
static char s_running_edit_goal_text[32];
static int s_history_edit_index = -1;
static FastEntry s_history_edit_draft = {0};
static EditField s_history_edit_field = EDIT_FIELD_START;
static bool s_history_edit_dirty = false;
static time_t s_last_streak_refresh_day = 0;
static const uint32_t s_alarm_vibe[] = {200, 100, 200, 100, 400, 100, 200};
static VibePattern s_alarm_pattern = {
  .durations = s_alarm_vibe,
  .num_segments = ARRAY_LENGTH(s_alarm_vibe),
};

static void refresh_timer_view(void);
static void refresh_goal_window_content(void);
static void sync_main_menu_state(void);
static void refresh_stats_window_content(void);
static void history_menu_reload(void);
static void refresh_running_edit_window_content(void);
static void recompute_streak_data_from_history(void);
static time_t entry_duration_seconds(const FastEntry *entry);
static void recompute_streak_data_for_today(void);
static bool refresh_streak_if_day_changed(void);

static bool safe_push_window(Window *window, bool animated) {
  if (!window) {
    return false;
  }
  if (window_stack_contains_window(window)) {
    return false;
  }
  window_stack_push(window, animated);
  return true;
}

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

static time_t local_day_start(time_t timestamp) {
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
  if (first_day <= 0 || second_day <= 0) {
    return false;
  }

  struct tm next_day_tm;
  struct tm *tm_info = localtime(&first_day);
  if (!tm_info) {
    return false;
  }

  next_day_tm = *tm_info;
  next_day_tm.tm_mday += 1;
  next_day_tm.tm_hour = 0;
  next_day_tm.tm_min = 0;
  next_day_tm.tm_sec = 0;
  next_day_tm.tm_isdst = -1;
  time_t expected_next_day = mktime(&next_day_tm);
  return expected_next_day == second_day;
}

static void recompute_streak_data_from_history(void) {
  streak_data.current_streak = 0;
  streak_data.longest_streak = 0;
  streak_data.last_completed_fast_end = 0;

  if (history_count <= 0) {
    return;
  }

  time_t completion_days[MAX_FASTS];
  int completion_day_count = 0;

  for (int i = 0; i < history_count; i++) {
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

  if (completion_day_count == 0) {
    return;
  }

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

  time_t today_day_start = local_day_start(time(NULL));
  time_t last_completion_day = completion_days[completion_day_count - 1];
  if (last_completion_day == today_day_start ||
      is_next_local_day(last_completion_day, today_day_start)) {
    streak_data.current_streak = run_length;
  } else {
    streak_data.current_streak = 0;
  }
  streak_data.longest_streak = longest;
}

static void recompute_streak_data_for_today(void) {
  recompute_streak_data_from_history();
  s_last_streak_refresh_day = local_day_start(time(NULL));
}

static bool refresh_streak_if_day_changed(void) {
  time_t today_day_start = local_day_start(time(NULL));
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
}

void load_all_data(void) {
  history_count = 0;
  memset(history, 0, sizeof(history));
  memset(&current_fast, 0, sizeof(current_fast));
  global_target_minutes = DEFAULT_TARGET_MINUTES;
  memset(&streak_data, 0, sizeof(streak_data));

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

  normalize_loaded_data();
  recompute_streak_data_for_today();
}

bool fast_is_running(void) {
  return current_fast.start_time != 0;
}

static void format_hhmmss(time_t seconds, char *buffer, size_t size) {
  if (seconds < 0) {
    seconds = 0;
  }
  snprintf(buffer, size, "%02d:%02d:%02d",
           (int)(seconds / 3600),
           (int)((seconds % 3600) / 60),
           (int)(seconds % 60));
}

static void format_duration_hours_minutes(time_t seconds, char *buffer, size_t size) {
  if (seconds < 0) {
    seconds = 0;
  }
  int hours = (int)(seconds / 3600);
  int minutes = (int)((seconds % 3600) / 60);
  snprintf(buffer, size, "%dh %02dm", hours, minutes);
}

static time_t entry_duration_seconds(const FastEntry *entry) {
  if (!entry || entry->start_time == 0 || entry->end_time <= entry->start_time) {
    return 0;
  }
  return entry->end_time - entry->start_time;
}

static void format_entry_datetime(time_t timestamp, char *buffer, size_t size) {
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

static const char *stage_label_for_level(uint8_t stage_level) {
  if (stage_level >= 3) {
    return "24h+";
  }
  if (stage_level == 2) {
    return "18h+";
  }
  if (stage_level == 1) {
    return "12h+";
  }
  return "--";
}

static int history_index_for_row(int row) {
  if (row < 0 || row >= history_count) {
    return -1;
  }
  return history_count - 1 - row;
}

static int compare_history_entries_by_end_time(const void *a, const void *b) {
  const FastEntry *entry_a = a;
  const FastEntry *entry_b = b;
  if (entry_a->end_time < entry_b->end_time) {
    return -1;
  }
  if (entry_a->end_time > entry_b->end_time) {
    return 1;
  }
  if (entry_a->start_time < entry_b->start_time) {
    return -1;
  }
  if (entry_a->start_time > entry_b->start_time) {
    return 1;
  }
  return 0;
}

static void sort_history_by_end_time(void) {
  if (history_count <= 1) {
    return;
  }
  qsort(history, (size_t)history_count, sizeof(FastEntry), compare_history_entries_by_end_time);
}

static void format_history_row(int row, char *title, size_t title_size, char *subtitle, size_t subtitle_size) {
  if (!title || !subtitle || title_size == 0 || subtitle_size == 0) {
    return;
  }
  if (history_count == 0) {
    snprintf(title, title_size, "No completed fasts");
    snprintf(subtitle, subtitle_size, "Start + stop a fast first");
    return;
  }

  int history_index = history_index_for_row(row);
  if (history_index < 0) {
    snprintf(title, title_size, "Unavailable");
    subtitle[0] = '\0';
    return;
  }

  const FastEntry *entry = &history[history_index];
  char date_text[24];
  char duration_text[20];
  format_entry_datetime(entry->end_time, date_text, sizeof(date_text));
  format_duration_hours_minutes(entry_duration_seconds(entry), duration_text, sizeof(duration_text));
  snprintf(title, title_size, "%s", date_text);
  snprintf(subtitle, subtitle_size, "%s | Max %s", duration_text, stage_label_for_level(entry->max_stage_reached));
}

static void refresh_stats_window_content(void) {
  if (!s_stats_body_layer) {
    return;
  }

  time_t total_seconds = 0;
  time_t longest_seconds = 0;
  int completed_count = 0;
  int successful_count = 0;

  for (int i = 0; i < history_count; i++) {
    const FastEntry *entry = &history[i];
    time_t duration = entry_duration_seconds(entry);
    if (duration <= 0) {
      continue;
    }

    completed_count++;
    total_seconds += duration;
    if (duration > longest_seconds) {
      longest_seconds = duration;
    }
    if (entry->target_minutes > 0 && duration >= (time_t)entry->target_minutes * 60) {
      successful_count++;
    }
  }

  if (completed_count == 0) {
    snprintf(s_stats_body_text, sizeof(s_stats_body_text),
             "No completed fasts yet.\n"
             "Start and stop your first\n"
             "fast to populate stats.\n"
             "Streak: %u current / %u best",
             streak_data.current_streak,
             streak_data.longest_streak);
  } else {
    char avg_text[20];
    char total_text[20];
    char longest_text[20];
    time_t average_seconds = total_seconds / completed_count;
    int success_rate = (successful_count * 100 + completed_count / 2) / completed_count;

    format_duration_hours_minutes(average_seconds, avg_text, sizeof(avg_text));
    format_duration_hours_minutes(total_seconds, total_text, sizeof(total_text));
    format_duration_hours_minutes(longest_seconds, longest_text, sizeof(longest_text));
    snprintf(s_stats_body_text, sizeof(s_stats_body_text),
             "Avg fast: %s\n"
             "Total fasted: %s\n"
             "Success: %d%% (%d/%d)\n"
             "Longest: %s\n"
             "Streak: %u / %u",
             avg_text,
             total_text,
             success_rate,
             successful_count,
             completed_count,
             longest_text,
             streak_data.current_streak,
             streak_data.longest_streak);
  }

  text_layer_set_text(s_stats_body_layer, s_stats_body_text);
}

static uint8_t stage_level_for_elapsed(time_t elapsed_seconds) {
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

static const char *stage_text_for_elapsed(time_t elapsed_seconds) {
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

static void update_max_stage_if_needed(time_t elapsed_seconds) {
  if (!fast_is_running()) {
    return;
  }

  uint8_t stage_level = stage_level_for_elapsed(elapsed_seconds);
  if (stage_level > current_fast.max_stage_reached) {
    current_fast.max_stage_reached = stage_level;
    save_all_data();
  }
}

static bool running_fast_is_at_target(time_t now) {
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
    global_target_minutes = preset_target_minutes;
  }
  if (global_target_minutes == 0) {
    global_target_minutes = DEFAULT_TARGET_MINUTES;
  }
  return global_target_minutes;
}

static void alarm_callback(void *data);

static void schedule_alarm_if_needed(void) {
  if (alarm_timer) {
    app_timer_cancel(alarm_timer);
    alarm_timer = NULL;
  }

  if (!fast_is_running() || current_fast.target_minutes == 0) {
    target_time = 0;
    return;
  }

  target_time = current_fast.start_time + (time_t)current_fast.target_minutes * 60;
  time_t now = time(NULL);
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

  current_fast.start_time = time(NULL);
  current_fast.end_time = 0;
  current_fast.target_minutes = resolve_target_minutes(preset_target_minutes);
  memset(current_fast.note, 0, sizeof(current_fast.note));
  current_fast.max_stage_reached = 0;
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
  completed.end_time = time(NULL);
  if (completed.end_time < completed.start_time) {
    completed.end_time = completed.start_time;
  }
  append_history_entry(&completed);
  sort_history_by_end_time();
  recompute_streak_data_for_today();
  memset(&current_fast, 0, sizeof(current_fast));
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

static void set_placeholder_content(const char *title, const char *body, const char *hint) {
  snprintf(s_placeholder_title_text, sizeof(s_placeholder_title_text), "%s", title ? title : "");
  snprintf(s_placeholder_body_text, sizeof(s_placeholder_body_text), "%s", body ? body : "");
  snprintf(s_placeholder_hint_text, sizeof(s_placeholder_hint_text), "%s", hint ? hint : "BACK Menu");
  if (s_placeholder_title_layer) {
    text_layer_set_text(s_placeholder_title_layer, s_placeholder_title_text);
  }
  if (s_placeholder_body_layer) {
    text_layer_set_text(s_placeholder_body_layer, s_placeholder_body_text);
  }
  if (s_placeholder_hint_layer) {
    text_layer_set_text(s_placeholder_hint_layer, s_placeholder_hint_text);
  }
}

static void show_placeholder_window(const char *title, const char *body, const char *hint) {
  set_placeholder_content(title, body, hint);
  if (!window_stack_contains_window(s_detail_window)) {
    window_stack_push(s_detail_window, true);
  }
}

static int progress_width_for_elapsed(time_t elapsed_seconds, uint32_t total_seconds, int width) {
  if (width <= 0 || total_seconds == 0 || elapsed_seconds <= 0) {
    return 0;
  }
  if ((uint32_t)elapsed_seconds >= total_seconds) {
    return width;
  }
  return (int)((elapsed_seconds * width) / (time_t)total_seconds);
}

static int tick_x_for_seconds(uint32_t total_seconds, uint32_t tick_seconds, int width) {
  if (width <= 0 || total_seconds == 0 || tick_seconds > total_seconds) {
    return -1;
  }
  int tick_x = (int)((tick_seconds * (uint32_t)width) / total_seconds);
  if (tick_x >= width) {
    return width - 1;
  }
  return tick_x;
}

static void timer_progress_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, bounds, 2, GCornersAll);
  if (!fast_is_running()) {
    return;
  }

  time_t elapsed = time(NULL) - current_fast.start_time;
  if (elapsed < 0) {
    elapsed = 0;
  }

  uint32_t total_seconds = (uint32_t)current_fast.target_minutes * 60;
  int fill_width = progress_width_for_elapsed(elapsed, total_seconds, bounds.size.w);
  if (fill_width > 0) {
    graphics_context_set_fill_color(ctx, GColorGreen);
    graphics_fill_rect(ctx, GRect(0, 0, fill_width, bounds.size.h), 2, GCornersAll);
  }

  graphics_context_set_stroke_color(ctx, GColorBlack);
  int tick_12h_x = tick_x_for_seconds(total_seconds, 12 * 3600, bounds.size.w);
  int tick_18h_x = tick_x_for_seconds(total_seconds, 18 * 3600, bounds.size.w);
  int tick_24h_x = tick_x_for_seconds(total_seconds, 24 * 3600, bounds.size.w);
  if (tick_12h_x >= 0) {
    graphics_draw_line(ctx, GPoint(tick_12h_x, 0), GPoint(tick_12h_x, bounds.size.h - 1));
  }
  if (tick_18h_x >= 0) {
    graphics_draw_line(ctx, GPoint(tick_18h_x, 0), GPoint(tick_18h_x, bounds.size.h - 1));
  }
  if (tick_24h_x >= 0) {
    graphics_draw_line(ctx, GPoint(tick_24h_x, 0), GPoint(tick_24h_x, bounds.size.h - 1));
  }
}

static void refresh_timer_view(void) {
  if (!s_title_layer || !s_timer_layer || !s_detail_layer || !s_stage_layer || !s_hint_layer) {
    return;
  }

  if (!fast_is_running()) {
    snprintf(s_title_text, sizeof(s_title_text), "NO FAST RUNNING");
    format_hhmmss(0, s_timer_text, sizeof(s_timer_text));
    snprintf(s_detail_text, sizeof(s_detail_text), "Target: %um  S:%u/%u",
             global_target_minutes, streak_data.current_streak, streak_data.longest_streak);
    snprintf(s_stage_text, sizeof(s_stage_text), "Stage: --");
    text_layer_set_text(s_hint_layer, "SELECT Start  DOWN/BACK Menu");
  } else {
    time_t elapsed = time(NULL) - current_fast.start_time;
    if (elapsed < 0) {
      elapsed = 0;
    }
    update_max_stage_if_needed(elapsed);
    uint32_t target_seconds = current_fast.target_minutes * 60;
    if (target_seconds > 0) {
      time_t remaining = (time_t)target_seconds - elapsed;
      if (remaining > 0) {
        snprintf(s_title_text, sizeof(s_title_text), "COUNTDOWN");
        format_hhmmss(remaining, s_timer_text, sizeof(s_timer_text));
      } else {
        snprintf(s_title_text, sizeof(s_title_text), "GOAL REACHED");
        format_hhmmss(0, s_timer_text, sizeof(s_timer_text));
      }
      char elapsed_text[16];
      format_hhmmss(elapsed, elapsed_text, sizeof(elapsed_text));
      snprintf(s_detail_text, sizeof(s_detail_text), "Elapsed %s  S:%u/%u",
               elapsed_text, streak_data.current_streak, streak_data.longest_streak);
    } else {
      snprintf(s_title_text, sizeof(s_title_text), "ELAPSED");
      format_hhmmss(elapsed, s_timer_text, sizeof(s_timer_text));
      snprintf(s_detail_text, sizeof(s_detail_text), "No target set  S:%u/%u",
               streak_data.current_streak, streak_data.longest_streak);
    }
    snprintf(s_stage_text, sizeof(s_stage_text), "Stage: %s", stage_text_for_elapsed(elapsed));
    text_layer_set_text(s_hint_layer, "UP Edit  SELECT Stop  DOWN/BACK Menu");
  }

  text_layer_set_text(s_title_layer, s_title_text);
  text_layer_set_text(s_timer_layer, s_timer_text);
  text_layer_set_text(s_detail_layer, s_detail_text);
  text_layer_set_text(s_stage_layer, s_stage_text);
  if (s_progress_layer) {
    layer_mark_dirty(s_progress_layer);
  }
}

static void refresh_goal_window_content(void) {
  if (!s_goal_time_layer || !s_goal_stage_layer) {
    return;
  }

  time_t elapsed = 0;
  if (fast_is_running()) {
    elapsed = time(NULL) - current_fast.start_time;
    if (elapsed < 0) {
      elapsed = 0;
    }
    update_max_stage_if_needed(elapsed);
  }

  char elapsed_text[16];
  format_hhmmss(elapsed, elapsed_text, sizeof(elapsed_text));
  snprintf(s_goal_time_text, sizeof(s_goal_time_text), "Elapsed %s", elapsed_text);
  snprintf(s_goal_stage_text, sizeof(s_goal_stage_text), "Stage: %s", stage_text_for_elapsed(elapsed));
  text_layer_set_text(s_goal_time_layer, s_goal_time_text);
  text_layer_set_text(s_goal_stage_layer, s_goal_stage_text);
}

static void sync_main_menu_state(void) {
  if (fast_is_running()) {
    snprintf(s_menu_stop_subtitle, sizeof(s_menu_stop_subtitle), "End now and save");
  } else {
    snprintf(s_menu_stop_subtitle, sizeof(s_menu_stop_subtitle), "No fast running");
  }
  s_main_menu_items[MAIN_MENU_INDEX_STOP_CURRENT].subtitle = s_menu_stop_subtitle;

  if (s_main_menu_layer) {
    menu_layer_reload_data(simple_menu_layer_get_menu_layer(s_main_menu_layer));
  }
}

static void refresh_all_ui_state(void) {
  refresh_timer_view();
  refresh_goal_window_content();
  sync_main_menu_state();
}

static void show_goal_reached_window(void) {
  refresh_goal_window_content();
  safe_push_window(s_goal_window, true);
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

static void start_fast_from_preset(uint16_t target_minutes, const char *label) {
  (void)label;
  if (!fast_start(target_minutes)) {
    show_placeholder_window("FAST RUNNING",
                            "Stop the current fast before starting a new one.",
                            "BACK Menu");
    return;
  }

  if (window_stack_contains_window(s_presets_window)) {
    window_stack_remove(s_presets_window, false);
  }
  safe_push_window(s_timer_window, true);
  refresh_all_ui_state();
}

static uint16_t history_menu_get_num_sections(MenuLayer *menu_layer, void *data) {
  (void)menu_layer;
  (void)data;
  return 1;
}

static uint16_t history_menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  (void)menu_layer;
  (void)section_index;
  (void)data;
  return history_count > 0 ? history_count : 1;
}

static int16_t history_menu_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  (void)menu_layer;
  (void)section_index;
  (void)data;
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void history_menu_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  (void)section_index;
  (void)data;
  char header_text[40];
  snprintf(header_text, sizeof(header_text), "History %d  S:%u/%u",
           history_count, streak_data.current_streak, streak_data.longest_streak);
  menu_cell_basic_header_draw(ctx, cell_layer, header_text);
}

static void history_menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  (void)data;
  int row = cell_index->row;
  if (row < 0 || row >= MAX_FASTS) {
    menu_cell_basic_draw(ctx, cell_layer, "Unavailable", "", NULL);
    return;
  }
  char title[24];
  char subtitle[40];
  format_history_row(row, title, sizeof(title), subtitle, sizeof(subtitle));
  menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

static void refresh_history_edit_window_content(void) {
  if (!s_history_edit_title_layer || !s_history_edit_start_layer || !s_history_edit_end_layer ||
      !s_history_edit_duration_layer || !s_history_edit_stage_layer || !s_history_edit_hint_layer) {
    return;
  }

  if (s_history_edit_index < 0 || s_history_edit_index >= history_count) {
    text_layer_set_text(s_history_edit_title_layer, "EDIT FAST");
    text_layer_set_text(s_history_edit_start_layer, "No entry selected");
    text_layer_set_text(s_history_edit_end_layer, "");
    text_layer_set_text(s_history_edit_duration_layer, "");
    text_layer_set_text(s_history_edit_stage_layer, "");
    text_layer_set_text(s_history_edit_hint_layer, "BACK");
    return;
  }

  char start_text[24];
  char end_text[24];
  char duration_text[20];
  format_entry_datetime(s_history_edit_draft.start_time, start_text, sizeof(start_text));
  format_entry_datetime(s_history_edit_draft.end_time, end_text, sizeof(end_text));
  format_duration_hours_minutes(entry_duration_seconds(&s_history_edit_draft), duration_text, sizeof(duration_text));

  snprintf(s_history_edit_title_text, sizeof(s_history_edit_title_text), "Edit %d/%d%s",
           s_history_edit_index + 1, history_count, s_history_edit_dirty ? "*" : "");
  snprintf(s_history_edit_start_text, sizeof(s_history_edit_start_text), "%cStart %s",
           s_history_edit_field == EDIT_FIELD_START ? '>' : ' ', start_text);
  snprintf(s_history_edit_end_text, sizeof(s_history_edit_end_text), "%cEnd   %s",
           s_history_edit_field == EDIT_FIELD_END ? '>' : ' ', end_text);
  snprintf(s_history_edit_duration_text, sizeof(s_history_edit_duration_text), "Dur %s", duration_text);
  snprintf(s_history_edit_stage_text, sizeof(s_history_edit_stage_text), "Stage %s",
           stage_text_for_elapsed(entry_duration_seconds(&s_history_edit_draft)));
  snprintf(s_history_edit_hint_text, sizeof(s_history_edit_hint_text), "UP/DN 15m SEL field HOLD save");

  text_layer_set_text(s_history_edit_title_layer, s_history_edit_title_text);
  text_layer_set_text(s_history_edit_start_layer, s_history_edit_start_text);
  text_layer_set_text(s_history_edit_end_layer, s_history_edit_end_text);
  text_layer_set_text(s_history_edit_duration_layer, s_history_edit_duration_text);
  text_layer_set_text(s_history_edit_stage_layer, s_history_edit_stage_text);
  text_layer_set_text(s_history_edit_hint_layer, s_history_edit_hint_text);
}

static void history_open_edit_for_row(int row) {
  int history_index = history_index_for_row(row);
  if (history_index < 0 || history_index >= history_count) {
    return;
  }
  s_history_edit_index = history_index;
  s_history_edit_draft = history[history_index];
  s_history_edit_field = EDIT_FIELD_START;
  s_history_edit_dirty = false;
  safe_push_window(s_history_edit_window, true);
  refresh_history_edit_window_content();
}

static void history_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  (void)menu_layer;
  (void)data;
  if (history_count == 0) {
    return;
  }
  history_open_edit_for_row(cell_index->row);
}

static void history_menu_reload(void) {
  if (s_history_menu_layer) {
    menu_layer_reload_data(s_history_menu_layer);
  }
}

static void history_adjust_edit_draft_by_minutes(int delta_minutes) {
  if (s_history_edit_index < 0 || s_history_edit_index >= history_count) {
    return;
  }
  time_t delta_seconds = (time_t)delta_minutes * 60;
  if (s_history_edit_field == EDIT_FIELD_START) {
    s_history_edit_draft.start_time += delta_seconds;
    if (s_history_edit_draft.start_time > s_history_edit_draft.end_time) {
      s_history_edit_draft.start_time = s_history_edit_draft.end_time;
    }
  } else {
    s_history_edit_draft.end_time += delta_seconds;
    if (s_history_edit_draft.end_time < s_history_edit_draft.start_time) {
      s_history_edit_draft.end_time = s_history_edit_draft.start_time;
    }
  }
  s_history_edit_dirty = true;
  refresh_history_edit_window_content();
}

static void history_edit_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  history_adjust_edit_draft_by_minutes(15);
}

static void history_edit_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  history_adjust_edit_draft_by_minutes(-15);
}

static void history_edit_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  s_history_edit_field = s_history_edit_field == EDIT_FIELD_START ? EDIT_FIELD_END : EDIT_FIELD_START;
  refresh_history_edit_window_content();
}

static void history_edit_save_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (s_history_edit_index < 0 || s_history_edit_index >= history_count) {
    return;
  }
  s_history_edit_draft.max_stage_reached = stage_level_for_elapsed(entry_duration_seconds(&s_history_edit_draft));
  history[s_history_edit_index] = s_history_edit_draft;
  sort_history_by_end_time();
  recompute_streak_data_for_today();
  save_all_data();
  s_history_edit_dirty = false;
  refresh_stats_window_content();
  history_menu_reload();
  window_stack_remove(s_history_edit_window, true);
}

static void history_edit_back_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_history_edit_window, true);
}

static void history_edit_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, history_edit_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, history_edit_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, history_edit_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, history_edit_back_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, history_edit_save_click_handler, NULL);
}

static void running_fast_edit_apply_delta_minutes(int delta_minutes) {
  if (!fast_is_running() || delta_minutes == 0) {
    return;
  }

  time_t now = time(NULL);
  time_t updated_start = current_fast.start_time + (time_t)delta_minutes * 60;
  if (updated_start > now) {
    updated_start = now;
  }
  if (updated_start <= 0) {
    updated_start = 1;
  }
  if (updated_start == current_fast.start_time) {
    return;
  }

  current_fast.start_time = updated_start;
  time_t elapsed = now - current_fast.start_time;
  if (elapsed < 0) {
    elapsed = 0;
  }
  current_fast.max_stage_reached = stage_level_for_elapsed(elapsed);
  save_all_data();
  schedule_alarm_if_needed();
  if (!running_fast_is_at_target(now) && window_stack_contains_window(s_goal_window)) {
    window_stack_remove(s_goal_window, false);
  }
  refresh_all_ui_state();
  refresh_running_edit_window_content();
}

static void refresh_running_edit_window_content(void) {
  if (!s_running_edit_title_layer || !s_running_edit_start_layer || !s_running_edit_elapsed_layer ||
      !s_running_edit_goal_layer || !s_running_edit_hint_layer) {
    return;
  }

  if (!fast_is_running()) {
    text_layer_set_text(s_running_edit_title_layer, "EDIT RUNNING");
    text_layer_set_text(s_running_edit_start_layer, "No fast running");
    text_layer_set_text(s_running_edit_elapsed_layer, "");
    text_layer_set_text(s_running_edit_goal_layer, "");
    text_layer_set_text(s_running_edit_hint_layer, "BACK");
    return;
  }

  time_t now = time(NULL);
  char start_text[24];
  format_entry_datetime(current_fast.start_time, start_text, sizeof(start_text));

  time_t elapsed = now - current_fast.start_time;
  if (elapsed < 0) {
    elapsed = 0;
  }
  char elapsed_text[20];
  format_duration_hours_minutes(elapsed, elapsed_text, sizeof(elapsed_text));

  snprintf(s_running_edit_start_text, sizeof(s_running_edit_start_text), "Start %s", start_text);
  snprintf(s_running_edit_elapsed_text, sizeof(s_running_edit_elapsed_text), "Elapsed %s", elapsed_text);
  if (current_fast.target_minutes > 0) {
    time_t remaining = (time_t)current_fast.target_minutes * 60 - elapsed;
    if (remaining > 0) {
      char remaining_text[16];
      format_hhmmss(remaining, remaining_text, sizeof(remaining_text));
      snprintf(s_running_edit_goal_text, sizeof(s_running_edit_goal_text), "Goal in %s", remaining_text);
    } else {
      snprintf(s_running_edit_goal_text, sizeof(s_running_edit_goal_text), "Goal already reached");
    }
  } else {
    snprintf(s_running_edit_goal_text, sizeof(s_running_edit_goal_text), "No target configured");
  }

  text_layer_set_text(s_running_edit_title_layer, "EDIT RUNNING");
  text_layer_set_text(s_running_edit_start_layer, s_running_edit_start_text);
  text_layer_set_text(s_running_edit_elapsed_layer, s_running_edit_elapsed_text);
  text_layer_set_text(s_running_edit_goal_layer, s_running_edit_goal_text);
  text_layer_set_text(s_running_edit_hint_layer, "UP earlier  DOWN later\nSELECT/BACK done");
}

static void running_edit_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  running_fast_edit_apply_delta_minutes(-15);
}

static void running_edit_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  running_fast_edit_apply_delta_minutes(15);
}

static void running_edit_up_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  running_fast_edit_apply_delta_minutes(-60);
}

static void running_edit_down_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  running_fast_edit_apply_delta_minutes(60);
}

static void running_edit_done_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_running_edit_window, true);
}

static void running_edit_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, running_edit_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, running_edit_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, running_edit_done_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, running_edit_done_click_handler);
  window_long_click_subscribe(BUTTON_ID_UP, 500, running_edit_up_long_click_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, running_edit_down_long_click_handler, NULL);
}

static void menu_start_new_fast_callback(int index, void *context) {
  (void)index;
  (void)context;
  safe_push_window(s_presets_window, true);
}

static void menu_current_timer_callback(int index, void *context) {
  (void)index;
  (void)context;
  safe_push_window(s_timer_window, true);
}

static void menu_stop_current_callback(int index, void *context) {
  (void)index;
  (void)context;
  if (!fast_stop()) {
    show_placeholder_window("NOT RUNNING", "There is no active fast to stop.", "BACK Menu");
    return;
  }
  show_placeholder_window("FAST STOPPED", "Current fast saved to history.", "BACK Menu");
  refresh_all_ui_state();
}

static void menu_history_callback(int index, void *context) {
  (void)index;
  (void)context;
  history_menu_reload();
  safe_push_window(s_history_window, true);
}

static void menu_statistics_callback(int index, void *context) {
  (void)index;
  (void)context;
  refresh_stats_window_content();
  safe_push_window(s_stats_window, true);
}

static void menu_science_callback(int index, void *context) {
  (void)index;
  (void)context;
  show_placeholder_window("FASTING SCIENCE",
                          "Screen pending.\nTimeline + protocol comparison will live here.",
                          "BACK Menu");
}

static void menu_settings_callback(int index, void *context) {
  (void)index;
  (void)context;
  char message[96];
  snprintf(message, sizeof(message),
           "Settings pending.\nDefault target is %u minutes.",
           global_target_minutes);
  show_placeholder_window("SETTINGS", message, "BACK Menu");
}

static void menu_backup_callback(int index, void *context) {
  (void)index;
  (void)context;
  show_placeholder_window("BACKUP TO PHONE",
                          "Backup flow pending.\nAppMessage export entry point is ready.",
                          "BACK Menu");
}

static void preset_default_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(global_target_minutes, "STARTED");
}

static void preset_16h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(16 * 60, "16H STARTED");
}

static void preset_18h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(18 * 60, "18H STARTED");
}

static void preset_20h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(20 * 60, "20H STARTED");
}

static void preset_24h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(24 * 60, "24H STARTED");
}

static void preset_36h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(36 * 60, "36H STARTED");
}

static void timer_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (fast_is_running()) {
    fast_stop();
  } else {
    fast_start(0);
  }
  refresh_all_ui_state();
}

static void timer_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (!fast_is_running()) {
    return;
  }
  safe_push_window(s_running_edit_window, true);
  refresh_running_edit_window_content();
}

static void timer_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_timer_window, true);
}

static void timer_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, timer_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, timer_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, timer_down_click_handler);
}

static void goal_window_stop_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  fast_stop();
  window_stack_remove(s_goal_window, true);
  refresh_all_ui_state();
}

static void goal_window_continue_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_goal_window, true);
  refresh_all_ui_state();
}

static void goal_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, goal_window_stop_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, goal_window_continue_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, goal_window_continue_handler);
  window_single_click_subscribe(BUTTON_ID_UP, goal_window_continue_handler);
}

static void placeholder_dismiss_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_detail_window, true);
}

static void placeholder_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, placeholder_dismiss_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, placeholder_dismiss_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, placeholder_dismiss_handler);
  window_single_click_subscribe(BUTTON_ID_UP, placeholder_dismiss_handler);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  (void)tick_time;
  (void)units_changed;
  if (refresh_streak_if_day_changed()) {
    refresh_stats_window_content();
    history_menu_reload();
  }
  refresh_timer_view();
  refresh_running_edit_window_content();
}

static void goal_background_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
}

static void menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  s_main_menu_layer = simple_menu_layer_create(bounds, window, s_main_menu_sections, 1, NULL);
  layer_add_child(window_layer, simple_menu_layer_get_layer(s_main_menu_layer));
  sync_main_menu_state();
}

static void menu_window_unload(Window *window) {
  (void)window;
  simple_menu_layer_destroy(s_main_menu_layer);
  s_main_menu_layer = NULL;
  save_all_data();
}

static void timer_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_title_layer = text_layer_create(GRect(0, 4, bounds.size.w, 24));
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_color(s_title_layer, GColorBlack);
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_timer_layer = text_layer_create(GRect(0, 28, bounds.size.w, 42));
  text_layer_set_background_color(s_timer_layer, GColorClear);
  text_layer_set_text_color(s_timer_layer, GColorBlack);
  text_layer_set_text_alignment(s_timer_layer, GTextAlignmentCenter);
  text_layer_set_font(s_timer_layer, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));

  s_detail_layer = text_layer_create(GRect(0, 76, bounds.size.w, 24));
  text_layer_set_background_color(s_detail_layer, GColorClear);
  text_layer_set_text_color(s_detail_layer, GColorBlack);
  text_layer_set_text_alignment(s_detail_layer, GTextAlignmentCenter);
  text_layer_set_font(s_detail_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));

  s_progress_layer = layer_create(GRect(10, 104, bounds.size.w - 20, 12));
  layer_set_update_proc(s_progress_layer, timer_progress_update_proc);

  s_stage_layer = text_layer_create(GRect(0, 118, bounds.size.w, 20));
  text_layer_set_background_color(s_stage_layer, GColorClear);
  text_layer_set_text_color(s_stage_layer, GColorBlack);
  text_layer_set_text_alignment(s_stage_layer, GTextAlignmentCenter);
  text_layer_set_font(s_stage_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_hint_layer = text_layer_create(GRect(0, 138, bounds.size.w, 28));
  text_layer_set_background_color(s_hint_layer, GColorClear);
  text_layer_set_text_color(s_hint_layer, GColorBlack);
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_timer_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_detail_layer));
  layer_add_child(window_layer, s_progress_layer);
  layer_add_child(window_layer, text_layer_get_layer(s_stage_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_hint_layer));
  refresh_timer_view();
}

static void timer_window_unload(Window *window) {
  (void)window;
  save_all_data();
  layer_destroy(s_progress_layer);
  s_progress_layer = NULL;
  text_layer_destroy(s_title_layer);
  s_title_layer = NULL;
  text_layer_destroy(s_timer_layer);
  s_timer_layer = NULL;
  text_layer_destroy(s_detail_layer);
  s_detail_layer = NULL;
  text_layer_destroy(s_stage_layer);
  s_stage_layer = NULL;
  text_layer_destroy(s_hint_layer);
  s_hint_layer = NULL;
}

static void goal_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_goal_background_layer = layer_create(bounds);
  layer_set_update_proc(s_goal_background_layer, goal_background_update_proc);
  layer_add_child(window_layer, s_goal_background_layer);

  s_goal_title_layer = text_layer_create(GRect(0, 20, bounds.size.w, 30));
  text_layer_set_background_color(s_goal_title_layer, GColorBlack);
  text_layer_set_text_color(s_goal_title_layer, GColorWhite);
  text_layer_set_text_alignment(s_goal_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_goal_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text(s_goal_title_layer, "GOAL HIT");

  s_goal_time_layer = text_layer_create(GRect(0, 56, bounds.size.w, 26));
  text_layer_set_background_color(s_goal_time_layer, GColorBlack);
  text_layer_set_text_color(s_goal_time_layer, GColorWhite);
  text_layer_set_text_alignment(s_goal_time_layer, GTextAlignmentCenter);
  text_layer_set_font(s_goal_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text(s_goal_time_layer, "Elapsed 00:00:00");

  s_goal_stage_layer = text_layer_create(GRect(0, 84, bounds.size.w, 24));
  text_layer_set_background_color(s_goal_stage_layer, GColorBlack);
  text_layer_set_text_color(s_goal_stage_layer, GColorWhite);
  text_layer_set_text_alignment(s_goal_stage_layer, GTextAlignmentCenter);
  text_layer_set_font(s_goal_stage_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text(s_goal_stage_layer, "Stage: --");

  s_goal_hint_layer = text_layer_create(GRect(0, 120, bounds.size.w, 42));
  text_layer_set_background_color(s_goal_hint_layer, GColorBlack);
  text_layer_set_text_color(s_goal_hint_layer, GColorWhite);
  text_layer_set_text_alignment(s_goal_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_goal_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text(s_goal_hint_layer, "SELECT Stop\nDOWN Continue");

  layer_add_child(window_layer, text_layer_get_layer(s_goal_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_goal_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_goal_stage_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_goal_hint_layer));
  refresh_goal_window_content();
}

static void goal_window_unload(Window *window) {
  (void)window;
  layer_destroy(s_goal_background_layer);
  s_goal_background_layer = NULL;
  text_layer_destroy(s_goal_title_layer);
  s_goal_title_layer = NULL;
  text_layer_destroy(s_goal_time_layer);
  s_goal_time_layer = NULL;
  text_layer_destroy(s_goal_stage_layer);
  s_goal_stage_layer = NULL;
  text_layer_destroy(s_goal_hint_layer);
  s_goal_hint_layer = NULL;
}

static void presets_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  s_presets_menu_layer = simple_menu_layer_create(bounds, window, s_presets_menu_sections, 1, NULL);
  layer_add_child(window_layer, simple_menu_layer_get_layer(s_presets_menu_layer));
}

static void presets_window_unload(Window *window) {
  (void)window;
  simple_menu_layer_destroy(s_presets_menu_layer);
  s_presets_menu_layer = NULL;
}

static void history_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_history_menu_layer = menu_layer_create(bounds);
  menu_layer_set_click_config_onto_window(s_history_menu_layer, window);
  menu_layer_set_callbacks(s_history_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = history_menu_get_num_sections,
    .get_num_rows = history_menu_get_num_rows,
    .get_header_height = history_menu_get_header_height,
    .draw_header = history_menu_draw_header,
    .draw_row = history_menu_draw_row,
    .select_click = history_menu_select_callback
  });

  layer_add_child(window_layer, menu_layer_get_layer(s_history_menu_layer));
  history_menu_reload();
}

static void history_window_unload(Window *window) {
  (void)window;
  menu_layer_destroy(s_history_menu_layer);
  s_history_menu_layer = NULL;
}

static void history_window_appear(Window *window) {
  (void)window;
  history_menu_reload();
}

static void history_edit_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_history_edit_title_layer = text_layer_create(GRect(4, 4, bounds.size.w - 8, 24));
  text_layer_set_background_color(s_history_edit_title_layer, GColorClear);
  text_layer_set_text_color(s_history_edit_title_layer, GColorBlack);
  text_layer_set_text_alignment(s_history_edit_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_history_edit_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_history_edit_start_layer = text_layer_create(GRect(6, 30, bounds.size.w - 12, 22));
  text_layer_set_background_color(s_history_edit_start_layer, GColorClear);
  text_layer_set_text_color(s_history_edit_start_layer, GColorBlack);
  text_layer_set_text_alignment(s_history_edit_start_layer, GTextAlignmentLeft);
  text_layer_set_font(s_history_edit_start_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_history_edit_end_layer = text_layer_create(GRect(6, 54, bounds.size.w - 12, 22));
  text_layer_set_background_color(s_history_edit_end_layer, GColorClear);
  text_layer_set_text_color(s_history_edit_end_layer, GColorBlack);
  text_layer_set_text_alignment(s_history_edit_end_layer, GTextAlignmentLeft);
  text_layer_set_font(s_history_edit_end_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_history_edit_duration_layer = text_layer_create(GRect(6, 82, bounds.size.w - 12, 22));
  text_layer_set_background_color(s_history_edit_duration_layer, GColorClear);
  text_layer_set_text_color(s_history_edit_duration_layer, GColorBlack);
  text_layer_set_text_alignment(s_history_edit_duration_layer, GTextAlignmentLeft);
  text_layer_set_font(s_history_edit_duration_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_history_edit_stage_layer = text_layer_create(GRect(6, 106, bounds.size.w - 12, 22));
  text_layer_set_background_color(s_history_edit_stage_layer, GColorClear);
  text_layer_set_text_color(s_history_edit_stage_layer, GColorBlack);
  text_layer_set_text_alignment(s_history_edit_stage_layer, GTextAlignmentLeft);
  text_layer_set_font(s_history_edit_stage_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_history_edit_hint_layer = text_layer_create(GRect(4, 130, bounds.size.w - 8, 34));
  text_layer_set_background_color(s_history_edit_hint_layer, GColorClear);
  text_layer_set_text_color(s_history_edit_hint_layer, GColorBlack);
  text_layer_set_text_alignment(s_history_edit_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_history_edit_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

  layer_add_child(window_layer, text_layer_get_layer(s_history_edit_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_history_edit_start_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_history_edit_end_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_history_edit_duration_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_history_edit_stage_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_history_edit_hint_layer));
  refresh_history_edit_window_content();
}

static void history_edit_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_history_edit_title_layer);
  s_history_edit_title_layer = NULL;
  text_layer_destroy(s_history_edit_start_layer);
  s_history_edit_start_layer = NULL;
  text_layer_destroy(s_history_edit_end_layer);
  s_history_edit_end_layer = NULL;
  text_layer_destroy(s_history_edit_duration_layer);
  s_history_edit_duration_layer = NULL;
  text_layer_destroy(s_history_edit_stage_layer);
  s_history_edit_stage_layer = NULL;
  text_layer_destroy(s_history_edit_hint_layer);
  s_history_edit_hint_layer = NULL;
}

static void history_edit_window_appear(Window *window) {
  (void)window;
  refresh_history_edit_window_content();
}

static void running_edit_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_running_edit_title_layer = text_layer_create(GRect(4, 4, bounds.size.w - 8, 26));
  text_layer_set_background_color(s_running_edit_title_layer, GColorClear);
  text_layer_set_text_color(s_running_edit_title_layer, GColorBlack);
  text_layer_set_text_alignment(s_running_edit_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_running_edit_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));

  s_running_edit_start_layer = text_layer_create(GRect(6, 36, bounds.size.w - 12, 24));
  text_layer_set_background_color(s_running_edit_start_layer, GColorClear);
  text_layer_set_text_color(s_running_edit_start_layer, GColorBlack);
  text_layer_set_text_alignment(s_running_edit_start_layer, GTextAlignmentLeft);
  text_layer_set_font(s_running_edit_start_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_running_edit_elapsed_layer = text_layer_create(GRect(6, 62, bounds.size.w - 12, 24));
  text_layer_set_background_color(s_running_edit_elapsed_layer, GColorClear);
  text_layer_set_text_color(s_running_edit_elapsed_layer, GColorBlack);
  text_layer_set_text_alignment(s_running_edit_elapsed_layer, GTextAlignmentLeft);
  text_layer_set_font(s_running_edit_elapsed_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_running_edit_goal_layer = text_layer_create(GRect(6, 88, bounds.size.w - 12, 34));
  text_layer_set_background_color(s_running_edit_goal_layer, GColorClear);
  text_layer_set_text_color(s_running_edit_goal_layer, GColorBlack);
  text_layer_set_text_alignment(s_running_edit_goal_layer, GTextAlignmentLeft);
  text_layer_set_font(s_running_edit_goal_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));

  s_running_edit_hint_layer = text_layer_create(GRect(4, 124, bounds.size.w - 8, 40));
  text_layer_set_background_color(s_running_edit_hint_layer, GColorClear);
  text_layer_set_text_color(s_running_edit_hint_layer, GColorBlack);
  text_layer_set_text_alignment(s_running_edit_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_running_edit_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

  layer_add_child(window_layer, text_layer_get_layer(s_running_edit_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_running_edit_start_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_running_edit_elapsed_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_running_edit_goal_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_running_edit_hint_layer));
  refresh_running_edit_window_content();
}

static void running_edit_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_running_edit_title_layer);
  s_running_edit_title_layer = NULL;
  text_layer_destroy(s_running_edit_start_layer);
  s_running_edit_start_layer = NULL;
  text_layer_destroy(s_running_edit_elapsed_layer);
  s_running_edit_elapsed_layer = NULL;
  text_layer_destroy(s_running_edit_goal_layer);
  s_running_edit_goal_layer = NULL;
  text_layer_destroy(s_running_edit_hint_layer);
  s_running_edit_hint_layer = NULL;
}

static void running_edit_window_appear(Window *window) {
  (void)window;
  refresh_running_edit_window_content();
}

static void stats_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_stats_title_layer = text_layer_create(GRect(4, 8, bounds.size.w - 8, 26));
  text_layer_set_background_color(s_stats_title_layer, GColorClear);
  text_layer_set_text_color(s_stats_title_layer, GColorBlack);
  text_layer_set_text_alignment(s_stats_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_stats_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text(s_stats_title_layer, "STATISTICS");

  s_stats_body_layer = text_layer_create(GRect(6, 40, bounds.size.w - 12, 98));
  text_layer_set_background_color(s_stats_body_layer, GColorClear);
  text_layer_set_text_color(s_stats_body_layer, GColorBlack);
  text_layer_set_text_alignment(s_stats_body_layer, GTextAlignmentLeft);
  text_layer_set_font(s_stats_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_stats_hint_layer = text_layer_create(GRect(4, 140, bounds.size.w - 8, 24));
  text_layer_set_background_color(s_stats_hint_layer, GColorClear);
  text_layer_set_text_color(s_stats_hint_layer, GColorBlack);
  text_layer_set_text_alignment(s_stats_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_stats_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text(s_stats_hint_layer, "BACK Menu");

  layer_add_child(window_layer, text_layer_get_layer(s_stats_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_stats_body_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_stats_hint_layer));
  refresh_stats_window_content();
}

static void stats_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_stats_title_layer);
  s_stats_title_layer = NULL;
  text_layer_destroy(s_stats_body_layer);
  s_stats_body_layer = NULL;
  text_layer_destroy(s_stats_hint_layer);
  s_stats_hint_layer = NULL;
}

static void stats_window_appear(Window *window) {
  (void)window;
  refresh_stats_window_content();
}

static void detail_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_placeholder_title_layer = text_layer_create(GRect(4, 8, bounds.size.w - 8, 26));
  text_layer_set_background_color(s_placeholder_title_layer, GColorClear);
  text_layer_set_text_color(s_placeholder_title_layer, GColorBlack);
  text_layer_set_text_alignment(s_placeholder_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_placeholder_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));

  s_placeholder_body_layer = text_layer_create(GRect(6, 40, bounds.size.w - 12, 98));
  text_layer_set_background_color(s_placeholder_body_layer, GColorClear);
  text_layer_set_text_color(s_placeholder_body_layer, GColorBlack);
  text_layer_set_text_alignment(s_placeholder_body_layer, GTextAlignmentCenter);
  text_layer_set_font(s_placeholder_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));

  s_placeholder_hint_layer = text_layer_create(GRect(4, 140, bounds.size.w - 8, 24));
  text_layer_set_background_color(s_placeholder_hint_layer, GColorClear);
  text_layer_set_text_color(s_placeholder_hint_layer, GColorBlack);
  text_layer_set_text_alignment(s_placeholder_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_placeholder_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));

  layer_add_child(window_layer, text_layer_get_layer(s_placeholder_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_placeholder_body_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_placeholder_hint_layer));
  set_placeholder_content(s_placeholder_title_text, s_placeholder_body_text, s_placeholder_hint_text);
}

static void detail_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_placeholder_title_layer);
  s_placeholder_title_layer = NULL;
  text_layer_destroy(s_placeholder_body_layer);
  s_placeholder_body_layer = NULL;
  text_layer_destroy(s_placeholder_hint_layer);
  s_placeholder_hint_layer = NULL;
}

static void menu_window_appear(Window *window) {
  (void)window;
  sync_main_menu_state();
}

static void configure_main_menu_items(void) {
  s_main_menu_items[MAIN_MENU_INDEX_START_NEW] = (SimpleMenuItem) {
    .title = "Start New Fast",
    .subtitle = "Open preset targets",
    .callback = menu_start_new_fast_callback
  };
  s_main_menu_items[MAIN_MENU_INDEX_CURRENT_TIMER] = (SimpleMenuItem) {
    .title = "Current Timer",
    .subtitle = "Live countdown screen",
    .callback = menu_current_timer_callback
  };
  s_main_menu_items[MAIN_MENU_INDEX_STOP_CURRENT] = (SimpleMenuItem) {
    .title = "Stop Current Fast",
    .subtitle = "",
    .callback = menu_stop_current_callback
  };
  s_main_menu_items[MAIN_MENU_INDEX_HISTORY] = (SimpleMenuItem) {
    .title = "History",
    .subtitle = "Completed fasts",
    .callback = menu_history_callback
  };
  s_main_menu_items[MAIN_MENU_INDEX_STATS] = (SimpleMenuItem) {
    .title = "Statistics",
    .subtitle = "Dashboard metrics",
    .callback = menu_statistics_callback
  };
  s_main_menu_items[MAIN_MENU_INDEX_SCIENCE] = (SimpleMenuItem) {
    .title = "Fasting Science",
    .subtitle = "Physiology timeline",
    .callback = menu_science_callback
  };
  s_main_menu_items[MAIN_MENU_INDEX_SETTINGS] = (SimpleMenuItem) {
    .title = "Settings",
    .subtitle = "Defaults and behavior",
    .callback = menu_settings_callback
  };
  s_main_menu_items[MAIN_MENU_INDEX_BACKUP] = (SimpleMenuItem) {
    .title = "Backup to Phone",
    .subtitle = "Export history data",
    .callback = menu_backup_callback
  };

  s_main_menu_sections[0] = (SimpleMenuSection) {
    .title = "FastForge",
    .num_items = MAIN_MENU_ITEM_COUNT,
    .items = s_main_menu_items
  };
}

static void configure_preset_items(void) {
  s_presets_menu_items[PRESET_MENU_INDEX_DEFAULT] = (SimpleMenuItem) {
    .title = "Use Default Target",
    .subtitle = "Current app setting",
    .callback = preset_default_callback
  };
  s_presets_menu_items[PRESET_MENU_INDEX_16H] = (SimpleMenuItem) {
    .title = "16 hours",
    .subtitle = "Beginner baseline",
    .callback = preset_16h_callback
  };
  s_presets_menu_items[PRESET_MENU_INDEX_18H] = (SimpleMenuItem) {
    .title = "18 hours",
    .subtitle = "Moderate challenge",
    .callback = preset_18h_callback
  };
  s_presets_menu_items[PRESET_MENU_INDEX_20H] = (SimpleMenuItem) {
    .title = "20 hours",
    .subtitle = "Aggressive cutting",
    .callback = preset_20h_callback
  };
  s_presets_menu_items[PRESET_MENU_INDEX_24H] = (SimpleMenuItem) {
    .title = "24 hours",
    .subtitle = "OMAD extended",
    .callback = preset_24h_callback
  };
  s_presets_menu_items[PRESET_MENU_INDEX_36H] = (SimpleMenuItem) {
    .title = "36 hours",
    .subtitle = "Deep ketosis push",
    .callback = preset_36h_callback
  };

  s_presets_menu_sections[0] = (SimpleMenuSection) {
    .title = "Start New Fast",
    .num_items = PRESET_MENU_ITEM_COUNT,
    .items = s_presets_menu_items
  };
}

static void init_windows(void) {
  s_menu_window = window_create();
  window_set_window_handlers(s_menu_window, (WindowHandlers) {
    .load = menu_window_load,
    .appear = menu_window_appear,
    .unload = menu_window_unload
  });

  s_timer_window = window_create();
  window_set_click_config_provider(s_timer_window, timer_click_config_provider);
  window_set_window_handlers(s_timer_window, (WindowHandlers) {
    .load = timer_window_load,
    .unload = timer_window_unload
  });

  s_goal_window = window_create();
  window_set_background_color(s_goal_window, GColorBlack);
  window_set_click_config_provider(s_goal_window, goal_click_config_provider);
  window_set_window_handlers(s_goal_window, (WindowHandlers) {
    .load = goal_window_load,
    .unload = goal_window_unload
  });

  s_presets_window = window_create();
  window_set_window_handlers(s_presets_window, (WindowHandlers) {
    .load = presets_window_load,
    .unload = presets_window_unload
  });

  s_history_window = window_create();
  window_set_window_handlers(s_history_window, (WindowHandlers) {
    .load = history_window_load,
    .appear = history_window_appear,
    .unload = history_window_unload
  });

  s_history_edit_window = window_create();
  window_set_click_config_provider(s_history_edit_window, history_edit_click_config_provider);
  window_set_window_handlers(s_history_edit_window, (WindowHandlers) {
    .load = history_edit_window_load,
    .appear = history_edit_window_appear,
    .unload = history_edit_window_unload
  });

  s_running_edit_window = window_create();
  window_set_click_config_provider(s_running_edit_window, running_edit_click_config_provider);
  window_set_window_handlers(s_running_edit_window, (WindowHandlers) {
    .load = running_edit_window_load,
    .appear = running_edit_window_appear,
    .unload = running_edit_window_unload
  });

  s_stats_window = window_create();
  window_set_window_handlers(s_stats_window, (WindowHandlers) {
    .load = stats_window_load,
    .appear = stats_window_appear,
    .unload = stats_window_unload
  });

  s_detail_window = window_create();
  window_set_click_config_provider(s_detail_window, placeholder_click_config_provider);
  window_set_window_handlers(s_detail_window, (WindowHandlers) {
    .load = detail_window_load,
    .unload = detail_window_unload
  });
}

static void destroy_windows(void) {
  window_destroy(s_detail_window);
  window_destroy(s_stats_window);
  window_destroy(s_running_edit_window);
  window_destroy(s_history_edit_window);
  window_destroy(s_history_window);
  window_destroy(s_presets_window);
  window_destroy(s_goal_window);
  window_destroy(s_timer_window);
  window_destroy(s_menu_window);
}

static void init(void) {
  load_all_data();
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  configure_main_menu_items();
  configure_preset_items();
  set_placeholder_content("DETAIL", "FastForge destination placeholder.", "BACK Menu");
  init_windows();
  window_stack_push(s_menu_window, true);
  schedule_alarm_if_needed();
  sync_main_menu_state();
}

static void deinit(void) {
  if (alarm_timer) {
    app_timer_cancel(alarm_timer);
    alarm_timer = NULL;
  }
  target_time = 0;
  save_all_data();
  tick_timer_service_unsubscribe();
  destroy_windows();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
