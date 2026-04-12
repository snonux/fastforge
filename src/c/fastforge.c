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
static TextLayer *s_text_layer;
static char s_status_text[96];

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

static void refresh_status_text(void) {
  if (!fast_is_running()) {
    snprintf(s_status_text, sizeof(s_status_text), "No active fast\nTarget: %u min\nSELECT: Start\nDOWN: Quit",
             global_target_minutes);
  } else {
    const time_t elapsed = time(NULL) - current_fast.start_time;
    const int elapsed_hours = (int)(elapsed / 3600);
    const int elapsed_minutes = (int)((elapsed % 3600) / 60);
    snprintf(s_status_text, sizeof(s_status_text),
             "Fast running (%u min)\n%02dh %02dm elapsed\nSELECT: Stop\nDOWN: Quit",
             current_fast.target_minutes, elapsed_hours, elapsed_minutes);
  }
  text_layer_set_text(s_text_layer, s_status_text);
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
  refresh_status_text();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;

  APP_LOG(APP_LOG_LEVEL_INFO, "DOWN pressed - Quitting app...");
  window_stack_pop_all(false);   // Cleanly quit the app
}

// ==================== CLICK CONFIG ====================

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,  down_click_handler);
}

// ==================== WINDOW LOAD / UNLOAD ====================

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_text_layer = text_layer_create(GRect(0, 20, bounds.size.w, bounds.size.h - 40));
  text_layer_set_background_color(s_text_layer, GColorClear);
  text_layer_set_text_color(s_text_layer, GColorBlack);
  text_layer_set_text_alignment(s_text_layer, GTextAlignmentCenter);
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  refresh_status_text();

  layer_add_child(window_layer, text_layer_get_layer(s_text_layer));
}

static void window_unload(Window *window) {
  (void)window;
  save_all_data();
  text_layer_destroy(s_text_layer);
}

// ==================== APP INIT / DEINIT ====================

static void init(void) {
  load_all_data();
  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

  window_stack_push(s_main_window, true);
}

static void deinit(void) {
  save_all_data();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
