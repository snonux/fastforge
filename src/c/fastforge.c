#include <pebble.h>
#include "fastforge.h"

FastEntry history[MAX_FASTS];
int history_count = 0;
FastEntry current_fast = {0};
uint16_t global_target_minutes = DEFAULT_TARGET_MINUTES;
StreakData streak_data = {0};
AppTimer *alarm_timer = NULL;
time_t target_time = 0;

static Window *s_main_window;
static Window *s_goal_window;
static TextLayer *s_title_layer;
static TextLayer *s_timer_layer;
static TextLayer *s_detail_layer;
static TextLayer *s_stage_layer;
static TextLayer *s_hint_layer;
static TextLayer *s_goal_title_layer;
static TextLayer *s_goal_time_layer;
static TextLayer *s_goal_hint_layer;
static char s_title_text[24];
static char s_timer_text[16];
static char s_detail_text[48];
static char s_stage_text[24];
static const uint32_t s_alarm_vibe[] = {200, 100, 200, 100, 400, 100, 200};
static VibePattern s_alarm_pattern = {
  .durations = s_alarm_vibe,
  .num_segments = ARRAY_LENGTH(s_alarm_vibe),
};

static void refresh_timer_view(void);

static void show_goal_reached_window(void) {
  if (!s_goal_window) {
    return;
  }
  if (!window_stack_contains_window(s_goal_window)) {
    window_stack_push(s_goal_window, true);
  }
}

static void alarm_callback(void *data) {
  (void)data;
  alarm_timer = NULL;
  target_time = 0;
  vibes_enqueue_custom_pattern(s_alarm_pattern);
  light_enable_interaction();
  show_goal_reached_window();
  refresh_timer_view();
}

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

bool fast_is_running(void) {
  return current_fast.start_time != 0;
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

static void format_hhmmss(time_t seconds, char *buffer, size_t size) {
  if (seconds < 0) {
    seconds = 0;
  }
  int hours = (int)(seconds / 3600);
  int minutes = (int)((seconds % 3600) / 60);
  int secs = (int)(seconds % 60);
  snprintf(buffer, size, "%02d:%02d:%02d", hours, minutes, secs);
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
  if (elapsed_seconds >= 18 * 3600) {
    return "Stage: KETOSIS";
  }
  if (elapsed_seconds >= 12 * 3600) {
    return "Stage: FAT BURN";
  }
  return "Stage: GLYCOGEN";
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

static void refresh_timer_view(void) {
  if (!fast_is_running()) {
    snprintf(s_title_text, sizeof(s_title_text), "NO FAST RUNNING");
    format_hhmmss(0, s_timer_text, sizeof(s_timer_text));
    snprintf(s_detail_text, sizeof(s_detail_text), "Target: %um", global_target_minutes);
    snprintf(s_stage_text, sizeof(s_stage_text), "Stage: --");
    text_layer_set_text(s_hint_layer, "SELECT Start  DOWN Quit");
  } else {
    time_t now = time(NULL);
    time_t elapsed = now - current_fast.start_time;
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

    snprintf(s_stage_text, sizeof(s_stage_text), "%s", stage_text_for_elapsed(elapsed));
    text_layer_set_text(s_hint_layer, "SELECT Stop   DOWN Quit");
  }

  text_layer_set_text(s_title_layer, s_title_text);
  text_layer_set_text(s_timer_layer, s_timer_text);
  text_layer_set_text(s_detail_layer, s_detail_text);
  text_layer_set_text(s_stage_layer, s_stage_text);
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

  APP_LOG(APP_LOG_LEVEL_INFO, "Started fast at %ld (target=%u)",
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

  APP_LOG(APP_LOG_LEVEL_INFO, "Stopped fast and saved to history (count=%d)", history_count);
  return true;
}

// ==================== BUTTON HANDLERS ====================

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;

  if (fast_is_running()) {
    fast_stop();
  } else {
    fast_start(0);
  }
  refresh_timer_view();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;

  APP_LOG(APP_LOG_LEVEL_INFO, "DOWN pressed - Quitting app...");
  window_stack_pop_all(false);   // Cleanly quit the app
}

static void goal_window_dismiss_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_goal_window, true);
}

// ==================== CLICK CONFIG ====================

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,  down_click_handler);
}

static void goal_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, goal_window_dismiss_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, goal_window_dismiss_handler);
  window_single_click_subscribe(BUTTON_ID_UP, goal_window_dismiss_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, goal_window_dismiss_handler);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  (void)tick_time;
  (void)units_changed;
  refresh_timer_view();
}

// ==================== WINDOW LOAD / UNLOAD ====================

static void window_load(Window *window) {
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

  s_stage_layer = text_layer_create(GRect(0, 102, bounds.size.w, 24));
  text_layer_set_background_color(s_stage_layer, GColorClear);
  text_layer_set_text_color(s_stage_layer, GColorBlack);
  text_layer_set_text_alignment(s_stage_layer, GTextAlignmentCenter);
  text_layer_set_font(s_stage_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_hint_layer = text_layer_create(GRect(0, 136, bounds.size.w, 30));
  text_layer_set_background_color(s_hint_layer, GColorClear);
  text_layer_set_text_color(s_hint_layer, GColorBlack);
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_timer_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_detail_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_stage_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_hint_layer));

  refresh_timer_view();
}

static void window_unload(Window *window) {
  (void)window;
  save_all_data();
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_timer_layer);
  text_layer_destroy(s_detail_layer);
  text_layer_destroy(s_stage_layer);
  text_layer_destroy(s_hint_layer);
}

static void goal_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_goal_title_layer = text_layer_create(GRect(0, 28, bounds.size.w, 42));
  text_layer_set_background_color(s_goal_title_layer, GColorBlack);
  text_layer_set_text_color(s_goal_title_layer, GColorWhite);
  text_layer_set_text_alignment(s_goal_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_goal_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text(s_goal_title_layer, "Goal");

  s_goal_time_layer = text_layer_create(GRect(0, 70, bounds.size.w, 44));
  text_layer_set_background_color(s_goal_time_layer, GColorBlack);
  text_layer_set_text_color(s_goal_time_layer, GColorWhite);
  text_layer_set_text_alignment(s_goal_time_layer, GTextAlignmentCenter);
  text_layer_set_font(s_goal_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text(s_goal_time_layer, "Reached!");

  s_goal_hint_layer = text_layer_create(GRect(0, 126, bounds.size.w, 28));
  text_layer_set_background_color(s_goal_hint_layer, GColorBlack);
  text_layer_set_text_color(s_goal_hint_layer, GColorWhite);
  text_layer_set_text_alignment(s_goal_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_goal_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text(s_goal_hint_layer, "Any button");

  layer_add_child(window_layer, text_layer_get_layer(s_goal_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_goal_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_goal_hint_layer));
}

static void goal_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_goal_title_layer);
  text_layer_destroy(s_goal_time_layer);
  text_layer_destroy(s_goal_hint_layer);
}

// ==================== APP INIT / DEINIT ====================

static void init(void) {
  load_all_data();
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

  window_stack_push(s_main_window, true);

  s_goal_window = window_create();
  window_set_background_color(s_goal_window, GColorBlack);
  window_set_click_config_provider(s_goal_window, goal_click_config_provider);
  window_set_window_handlers(s_goal_window, (WindowHandlers) {
    .load = goal_window_load,
    .unload = goal_window_unload,
  });

  schedule_alarm_if_needed();
}

static void deinit(void) {
  if (alarm_timer) {
    app_timer_cancel(alarm_timer);
    alarm_timer = NULL;
  }
  target_time = 0;
  save_all_data();
  tick_timer_service_unsubscribe();
  window_destroy(s_goal_window);
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
