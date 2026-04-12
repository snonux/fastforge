#include <pebble.h>
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
static Window *s_detail_window;

static SimpleMenuLayer *s_main_menu_layer;
static SimpleMenuLayer *s_presets_menu_layer;
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
static const uint32_t s_alarm_vibe[] = {200, 100, 200, 100, 400, 100, 200};
static VibePattern s_alarm_pattern = {
  .durations = s_alarm_vibe,
  .num_segments = ARRAY_LENGTH(s_alarm_vibe),
};

static void refresh_timer_view(void);
static void refresh_goal_window_content(void);
static void sync_main_menu_state(void);

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

static void mark_fast_completed(time_t end_time) {
  streak_data.current_streak++;
  if (streak_data.current_streak > streak_data.longest_streak) {
    streak_data.longest_streak = streak_data.current_streak;
  }
  streak_data.last_completed_fast_end = end_time;
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
  mark_fast_completed(completed.end_time);
  memset(&current_fast, 0, sizeof(current_fast));
  if (alarm_timer) {
    app_timer_cancel(alarm_timer);
    alarm_timer = NULL;
  }
  target_time = 0;
  save_all_data();
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
    snprintf(s_detail_text, sizeof(s_detail_text), "Target: %um", global_target_minutes);
    snprintf(s_stage_text, sizeof(s_stage_text), "Stage: --");
    text_layer_set_text(s_hint_layer, "SELECT Start  BACK Menu");
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
      snprintf(s_detail_text, sizeof(s_detail_text), "Elapsed %s", elapsed_text);
    } else {
      snprintf(s_title_text, sizeof(s_title_text), "ELAPSED");
      format_hhmmss(elapsed, s_timer_text, sizeof(s_timer_text));
      snprintf(s_detail_text, sizeof(s_detail_text), "No target set");
    }
    snprintf(s_stage_text, sizeof(s_stage_text), "Stage: %s", stage_text_for_elapsed(elapsed));
    text_layer_set_text(s_hint_layer, "SELECT Stop   BACK Menu");
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
  char message[80];
  snprintf(message, sizeof(message), "History screen pending.\nSaved fasts: %d", history_count);
  show_placeholder_window("HISTORY", message, "BACK Menu");
}

static void menu_statistics_callback(int index, void *context) {
  (void)index;
  (void)context;
  char message[100];
  snprintf(message, sizeof(message),
           "Stats screen pending.\nStreak: %u  Longest: %u",
           streak_data.current_streak, streak_data.longest_streak);
  show_placeholder_window("STATISTICS", message, "BACK Menu");
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

static void timer_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_timer_window, true);
}

static void timer_click_config_provider(void *context) {
  (void)context;
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
  refresh_timer_view();
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
    .subtitle = "Streak and totals",
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

  s_detail_window = window_create();
  window_set_click_config_provider(s_detail_window, placeholder_click_config_provider);
  window_set_window_handlers(s_detail_window, (WindowHandlers) {
    .load = detail_window_load,
    .unload = detail_window_unload
  });
}

static void destroy_windows(void) {
  window_destroy(s_detail_window);
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
