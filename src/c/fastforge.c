#include "fastforge_internal.h"

#include <stdlib.h>
#include <string.h>

#include "message_keys.auto.h"

enum {
  MAIN_MENU_INDEX_START_NEW = 0,
  MAIN_MENU_INDEX_CURRENT_TIMER = 1,
  MAIN_MENU_INDEX_STOP_CURRENT = 2,
  MAIN_MENU_INDEX_CANCEL_CURRENT = 3,
  MAIN_MENU_INDEX_HISTORY = 4,
  MAIN_MENU_INDEX_STATS = 5,
  MAIN_MENU_INDEX_SETTINGS = 6,
  MAIN_MENU_INDEX_BACKUP = 7,
  MAIN_MENU_INDEX_ABOUT = 8,
  MAIN_MENU_ITEM_COUNT = 9
};

enum {
  PRESET_MENU_INDEX_16H = 0,
  PRESET_MENU_INDEX_18H = 1,
  PRESET_MENU_INDEX_20H = 2,
  PRESET_MENU_INDEX_24H = 3,
  PRESET_MENU_INDEX_26H = 4,
  PRESET_MENU_INDEX_28H = 5,
  PRESET_MENU_INDEX_30H = 6,
  PRESET_MENU_INDEX_36H = 7,
  PRESET_MENU_INDEX_10S = 8, /* dev/test: fires alarm after 10 s */
  PRESET_MENU_ITEM_COUNT = 9
};

static Window *s_menu_window;
static Window *s_timer_window;
static Window *s_goal_window;
static Window *s_presets_window;
static Window *s_settings_window;
static Window *s_stats_window;
static Window *s_detail_window;
static Window *s_history_window;
static Window *s_history_edit_window;
static Window *s_running_edit_window;

static SimpleMenuLayer *s_main_menu_layer;
static SimpleMenuLayer *s_presets_menu_layer;
MenuLayer *s_history_menu_layer;
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

static TextLayer *s_settings_title_layer;
static TextLayer *s_settings_target_layer;
#ifdef DEBUG
static TextLayer *s_settings_dev_layer;
#endif
static TextLayer *s_settings_hint_layer;
static TextLayer *s_placeholder_title_layer;
static TextLayer *s_placeholder_body_layer;
static TextLayer *s_placeholder_hint_layer;
static TextLayer *s_stats_title_layer;
TextLayer *s_stats_body_layer;
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
static char s_settings_target_text[32];
#ifdef DEBUG
static char s_settings_dev_text[32];
#endif
static char s_menu_stop_subtitle[32];
static char s_menu_cancel_subtitle[32];
static char s_placeholder_title_text[24];
static char s_placeholder_body_text[160];
static char s_placeholder_hint_text[24];
static char s_history_edit_title_text[48];
static char s_history_edit_start_text[32];
static char s_history_edit_end_text[32];
static char s_history_edit_duration_text[48];
static char s_history_edit_stage_text[24];
/* 45 bytes needed: "UP/DN adj SEL field\nHOLD save  BACK-hold del" + NUL */
static char s_history_edit_hint_text[48];
static char s_running_edit_start_text[32];
static char s_running_edit_elapsed_text[32];
static char s_running_edit_goal_text[32];
static int s_history_edit_index = -1;
static FastEntry s_history_edit_draft = {0};
#ifdef DEBUG
static Window *s_debug_window;
static SimpleMenuLayer *s_debug_menu_layer;
static SimpleMenuSection s_debug_menu_sections[1];
static SimpleMenuItem s_debug_menu_items[6];
static char s_debug_menu_clock_text[40];
#endif

typedef enum {
  EDIT_FIELD_START = 0,
  EDIT_FIELD_END = 1,
  EDIT_FIELD_NOTE = 2
} EditField;

static void refresh_timer_view(void);
static void refresh_goal_window_content(void);
static void sync_main_menu_state(void);
static void refresh_running_edit_window_content(void);
static void refresh_settings_window_content(void);
#ifdef DEBUG
static void show_debug_menu_window(void);
#endif

static EditField s_history_edit_field = EDIT_FIELD_START;
static bool s_history_edit_dirty = false;

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

void show_placeholder_window(const char *title, const char *body, const char *hint) {
  set_placeholder_content(title, body, hint);
  if (!window_stack_contains_window(s_detail_window)) {
    window_stack_push(s_detail_window, true);
  }
}

#ifdef DEBUG
static void show_developer_info_window(void) {
  char message[160];
  snprintf(message, sizeof(message),
           "running=%d hist=%d\n"
           "default=%u target=%u\n"
           "start=%ld last=%ld\n"
           "streak=%u/%u",
           fast_is_running() ? 1 : 0,
           history_count,
           global_target_minutes,
           current_fast.target_minutes,
           (long)current_fast.start_time,
           (long)streak_data.last_completed_fast_end,
           streak_data.current_streak,
           streak_data.longest_streak);
  show_placeholder_window("DEV INFO", message, "BACK Menu");
}
#endif

static bool debug_controls_available(void) {
#ifdef DEBUG
  return developer_mode_enabled;
#else
  return false;
#endif
}

static TextLayer *create_text_layer(GRect frame, GTextAlignment alignment,
                                    const char *font_key, GColor text_color,
                                    GColor background_color, bool wrap_text) {
  TextLayer *text_layer = text_layer_create(frame);
  text_layer_set_background_color(text_layer, background_color);
  text_layer_set_text_color(text_layer, text_color);
  text_layer_set_text_alignment(text_layer, alignment);
  text_layer_set_font(text_layer, fonts_get_system_font(font_key));
  if (wrap_text) {
    text_layer_set_overflow_mode(text_layer, GTextOverflowModeWordWrap);
  }
  return text_layer;
}

static void add_text_layer(Layer *window_layer, TextLayer *text_layer) {
  layer_add_child(window_layer, text_layer_get_layer(text_layer));
}

static bool is_color_platform(void) {
#ifdef PBL_COLOR
  return true;
#else
  return false;
#endif
}

static GColor theme_surface_background_color(void) {
  return is_color_platform() ? GColorMintGreen : GColorWhite;
}

static GColor theme_goal_background_color(void) {
  return is_color_platform() ? GColorOxfordBlue : GColorBlack;
}

static GColor theme_goal_text_color(void) {
  return GColorWhite;
}

static GColor theme_timer_background_color(bool goal_reached) {
  return goal_reached ? theme_goal_background_color()
                      : theme_surface_background_color();
}

static GColor theme_timer_text_color(bool goal_reached) {
  return goal_reached ? theme_goal_text_color() : GColorBlack;
}

static GColor theme_progress_track_color(void) {
  return GColorLightGray;
}

static GColor theme_progress_fill_color(void) {
  return is_color_platform() ? GColorJaegerGreen : GColorGreen;
}

static bool timer_goal_reached_for_elapsed(time_t elapsed_seconds) {
  uint32_t target_seconds = current_fast.target_minutes * 60;
  return target_seconds > 0 && elapsed_seconds >= (time_t)target_seconds;
}

static void apply_timer_theme(bool goal_reached) {
  GColor background = theme_timer_background_color(goal_reached);
  GColor foreground = theme_timer_text_color(goal_reached);
  window_set_background_color(s_timer_window, background);
  text_layer_set_text_color(s_title_layer, foreground);
  text_layer_set_text_color(s_timer_layer, foreground);
  text_layer_set_text_color(s_detail_layer, foreground);
  text_layer_set_text_color(s_stage_layer, foreground);
  text_layer_set_text_color(s_hint_layer, foreground);
}

static void format_remaining_with_overtime(time_t remaining_seconds,
                                           char *buffer, size_t size) {
  if (!buffer || size == 0) {
    return;
  }

  if (remaining_seconds >= 0) {
    format_hhmmss(remaining_seconds, buffer, size);
    return;
  }

  char overtime_text[16];
  format_hhmmss(-remaining_seconds, overtime_text, sizeof(overtime_text));

  size_t copy_len = strlen(overtime_text);
  if (copy_len > size - 2) {
    copy_len = size - 2;
  }

  buffer[0] = '-';
  memcpy(buffer + 1, overtime_text, copy_len);
  buffer[copy_len + 1] = '\0';
}

static Window *create_window_with_handlers(WindowHandlers handlers,
                                           ClickConfigProvider click_provider) {
  Window *window = window_create();
  if (click_provider) {
    window_set_click_config_provider(window, click_provider);
  }
  window_set_window_handlers(window, handlers);
  return window;
}

static uint16_t clamp_default_target_minutes(int target_minutes) {
  if (target_minutes < 8 * 60) {
    return 8 * 60;
  }
  if (target_minutes > 48 * 60) {
    return 48 * 60;
  }
  return (uint16_t)target_minutes;
}

static void refresh_settings_window_content(void) {
  if (!s_settings_target_layer || !s_settings_hint_layer) {
    return;
  }

  snprintf(s_settings_target_text, sizeof(s_settings_target_text), "Default: %dh %02dm",
           global_target_minutes / 60, global_target_minutes % 60);
#ifdef DEBUG
  snprintf(s_settings_dev_text, sizeof(s_settings_dev_text), "Dev Mode: %s",
           developer_mode_enabled ? "ON (timer dbg)" : "OFF");
  text_layer_set_text(s_settings_target_layer, s_settings_target_text);
  text_layer_set_text(s_settings_dev_layer, s_settings_dev_text);
  text_layer_set_text(s_settings_hint_layer, "UP/DN Target\nSEL Dev\nBACK Save");
#else
  text_layer_set_text(s_settings_target_layer, s_settings_target_text);
  text_layer_set_text(s_settings_hint_layer, "UP/DOWN Target\nBACK Save");
#endif
}

static void settings_persist_and_refresh(void) {
  save_all_data();
  refresh_timer_view();
  refresh_settings_window_content();
}

static void settings_adjust_target(int delta_minutes) {
  global_target_minutes = clamp_default_target_minutes((int)global_target_minutes + delta_minutes);
  settings_persist_and_refresh();
}

#ifdef DEBUG
static void settings_toggle_developer_mode(void) {
  developer_mode_enabled = !developer_mode_enabled;
  settings_persist_and_refresh();
}
#endif

#ifdef DEBUG
static void debug_refresh_menu(void) {
  snprintf(s_debug_menu_clock_text, sizeof(s_debug_menu_clock_text), "Debug %+ldh %s",
           (long)(s_fake_time_offset_seconds / 3600),
           s_fake_time_enabled ? "fake" : "real");
  s_debug_menu_items[0] = (SimpleMenuItem) {
    .title = "+1 Hour",
    .subtitle = "Advance debug clock",
    .callback = NULL
  };
  s_debug_menu_items[1] = (SimpleMenuItem) {
    .title = "+6 Hours",
    .subtitle = "Jump to next stage",
    .callback = NULL
  };
  s_debug_menu_items[2] = (SimpleMenuItem) {
    .title = "+24 Hours",
    .subtitle = "Cross whole-day boundary",
    .callback = NULL
  };
  s_debug_menu_items[3] = (SimpleMenuItem) {
    .title = "Use Real Clock",
    .subtitle = "Clear fake-time offset",
    .callback = NULL
  };
  s_debug_menu_items[4] = (SimpleMenuItem) {
    .title = "Force Goal Alarm",
    .subtitle = "Trigger goal-hit flow now",
    .callback = NULL
  };
  s_debug_menu_items[5] = (SimpleMenuItem) {
    .title = "Show Raw State",
    .subtitle = "Open debug snapshot",
    .callback = NULL
  };
  s_debug_menu_sections[0] = (SimpleMenuSection) {
    .title = s_debug_menu_clock_text,
    .num_items = ARRAY_LENGTH(s_debug_menu_items),
    .items = s_debug_menu_items
  };
  if (s_debug_menu_layer) {
    menu_layer_reload_data(simple_menu_layer_get_menu_layer(s_debug_menu_layer));
  }
}

static void debug_apply_time_offset_hours(int hours) {
  s_fake_time_enabled = true;
  s_fake_time_offset_seconds += hours * 3600;
  save_all_data();
  recompute_streak_data_for_today();
  schedule_alarm_if_needed();
  refresh_all_ui_state();
  debug_refresh_menu();
}

static void debug_force_goal_alarm(void) {
  if (!fast_is_running()) {
    show_placeholder_window("DEBUG", "Start a fast before forcing the goal alarm.", "BACK Menu");
    return;
  }
  if (alarm_timer) {
    app_timer_cancel(alarm_timer);
    alarm_timer = NULL;
  }
  fastforge_force_goal_alarm();
  debug_refresh_menu();
}

static void debug_reset_fake_time(void) {
  if (fast_is_running() && s_current_fast_origin_offset_seconds != 0) {
    current_fast.start_time -= s_current_fast_origin_offset_seconds;
    if (current_fast.start_time <= 0) {
      current_fast.start_time = 1;
    }
  }
  s_fake_time_enabled = false;
  s_fake_time_offset_seconds = 0;
  s_current_fast_origin_offset_seconds = 0;
  save_all_data();
  recompute_streak_data_for_today();
  schedule_alarm_if_needed();
  refresh_all_ui_state();
  debug_refresh_menu();
}

static void debug_menu_select_callback(int index, void *context) {
  (void)context;
  switch (index) {
    case 0:
      debug_apply_time_offset_hours(1);
      break;
    case 1:
      debug_apply_time_offset_hours(6);
      break;
    case 2:
      debug_apply_time_offset_hours(24);
      break;
    case 3:
      debug_reset_fake_time();
      break;
    case 4:
      debug_force_goal_alarm();
      break;
    case 5:
      show_developer_info_window();
      break;
  }
}

static void debug_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  for (size_t i = 0; i < ARRAY_LENGTH(s_debug_menu_items); i++) {
    s_debug_menu_items[i].callback = debug_menu_select_callback;
  }
  debug_refresh_menu();
  s_debug_menu_layer = simple_menu_layer_create(bounds, window, s_debug_menu_sections, 1, NULL);
  layer_add_child(window_layer, simple_menu_layer_get_layer(s_debug_menu_layer));
}

static void debug_window_unload(Window *window) {
  (void)window;
  simple_menu_layer_destroy(s_debug_menu_layer);
  s_debug_menu_layer = NULL;
}

static void debug_window_appear(Window *window) {
  (void)window;
  debug_refresh_menu();
}

static void show_debug_menu_window(void) {
  if (!debug_controls_available()) {
    return;
  }
  debug_refresh_menu();
  safe_push_window(s_debug_window, true);
}
#endif

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
  bool goal_reached = false;
  if (fast_is_running()) {
    goal_reached = timer_goal_reached_for_elapsed(fastforge_now() - current_fast.start_time);
  }

  graphics_context_set_fill_color(ctx,
                                  goal_reached ? theme_goal_background_color()
                                               : theme_progress_track_color());
  graphics_fill_rect(ctx, bounds, 2, GCornersAll);
  if (!fast_is_running()) {
    return;
  }

  time_t elapsed = fastforge_now() - current_fast.start_time;
  if (elapsed < 0) {
    elapsed = 0;
  }

  uint32_t total_seconds = (uint32_t)current_fast.target_minutes * 60;
  int fill_width = progress_width_for_elapsed(elapsed, total_seconds, bounds.size.w);
  if (fill_width > 0) {
    graphics_context_set_fill_color(ctx,
                                    goal_reached ? theme_goal_text_color()
                                                 : theme_progress_fill_color());
    graphics_fill_rect(ctx, GRect(0, 0, fill_width, bounds.size.h), 2, GCornersAll);
  }

  graphics_context_set_stroke_color(ctx, goal_reached ? theme_goal_text_color() : GColorBlack);
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

static void refresh_timer_view_layers(void) {
  text_layer_set_text(s_title_layer, s_title_text);
  text_layer_set_text(s_timer_layer, s_timer_text);
  text_layer_set_text(s_detail_layer, s_detail_text);
  text_layer_set_text(s_stage_layer, s_stage_text);
  if (s_progress_layer) {
    layer_mark_dirty(s_progress_layer);
  }
}

static void refresh_timer_view_idle(void) {
  apply_timer_theme(false);
  /* Restore the large number font in case we were in overtime mode before. */
  if (s_timer_layer) {
    text_layer_set_font(s_timer_layer,
                        fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  }
  snprintf(s_title_text, sizeof(s_title_text), "NO FAST RUNNING");
  format_hhmmss(0, s_timer_text, sizeof(s_timer_text));
  snprintf(s_detail_text, sizeof(s_detail_text), "Target: %um  S:%u/%u",
           global_target_minutes, streak_data.current_streak, streak_data.longest_streak);
  snprintf(s_stage_text, sizeof(s_stage_text), "Stage: --");
  if (debug_controls_available()) {
    snprintf(s_stage_text, sizeof(s_stage_text), "Stage: -- [DEV]");
  }
  text_layer_set_text(s_hint_layer, "SELECT Start\nDOWN Menu");
}

static void refresh_timer_view_running(time_t elapsed) {
  update_max_stage_if_needed(elapsed);
  uint32_t target_seconds = current_fast.target_minutes * 60;
  bool goal_reached = timer_goal_reached_for_elapsed(elapsed);
  apply_timer_theme(goal_reached);
  if (target_seconds > 0) {
    time_t remaining = (time_t)target_seconds - elapsed;
    if (remaining > 0) {
      /* Positive countdown: use the large number font — "HH:MM:SS" = 8 chars fits fine. */
      text_layer_set_font(s_timer_layer,
                          fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
      snprintf(s_title_text, sizeof(s_title_text), "COUNTDOWN");
      format_remaining_with_overtime(remaining, s_timer_text, sizeof(s_timer_text));
    } else {
      /* Overtime: "-HH:MM:SS" is 9 chars which overflows BITHAM_34 on 144 px wide display.
       * Switch to GOTHIC_28_BOLD which fits all 9 characters comfortably. */
      text_layer_set_font(s_timer_layer,
                          fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
      snprintf(s_title_text, sizeof(s_title_text), "GOAL REACHED");
      format_remaining_with_overtime(remaining, s_timer_text, sizeof(s_timer_text));
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

  if (debug_controls_available()) {
    snprintf(s_stage_text, sizeof(s_stage_text), "Stage: %s [DEV]",
             stage_text_for_elapsed(elapsed));
  } else {
    snprintf(s_stage_text, sizeof(s_stage_text), "Stage: %s",
             stage_text_for_elapsed(elapsed));
  }
  text_layer_set_text(s_hint_layer, "UP Edit  SEL Stop\nDOWN Menu");
}

static void refresh_timer_view(void) {
  if (!s_title_layer || !s_timer_layer || !s_detail_layer || !s_stage_layer || !s_hint_layer) {
    return;
  }

  if (!fast_is_running()) {
    refresh_timer_view_idle();
  } else {
    time_t elapsed = fastforge_now() - current_fast.start_time;
    if (elapsed < 0) {
      elapsed = 0;
    }
    refresh_timer_view_running(elapsed);
  }

  refresh_timer_view_layers();
}

static void refresh_goal_window_content(void) {
  if (!s_goal_time_layer || !s_goal_stage_layer) {
    return;
  }

  time_t elapsed = 0;
  if (fast_is_running()) {
    elapsed = fastforge_now() - current_fast.start_time;
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
    snprintf(s_menu_cancel_subtitle, sizeof(s_menu_cancel_subtitle), "Discard, no history");
  } else {
    snprintf(s_menu_stop_subtitle, sizeof(s_menu_stop_subtitle), "No fast running");
    snprintf(s_menu_cancel_subtitle, sizeof(s_menu_cancel_subtitle), "No fast running");
  }
  s_main_menu_items[MAIN_MENU_INDEX_STOP_CURRENT].subtitle = s_menu_stop_subtitle;
  s_main_menu_items[MAIN_MENU_INDEX_CANCEL_CURRENT].subtitle = s_menu_cancel_subtitle;

  if (s_main_menu_layer) {
    menu_layer_reload_data(simple_menu_layer_get_menu_layer(s_main_menu_layer));
  }
}

void refresh_all_ui_state(void) {
  refresh_timer_view();
  refresh_goal_window_content();
  sync_main_menu_state();
}

void show_goal_reached_window(void) {
  refresh_goal_window_content();
  safe_push_window(s_goal_window, true);
}

static void start_fast_from_preset(uint16_t target_minutes) {
  if (fast_is_running()) {
    show_placeholder_window("FAST RUNNING",
                            "Stop the current fast before starting a new one.",
                            "BACK Menu");
    return;
  }

  global_target_minutes = target_minutes;
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

static int16_t history_menu_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  (void)menu_layer;
  (void)cell_index;
  (void)data;
  return 44;
}

static void history_menu_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  (void)section_index;
  (void)data;
  char header_text[40];
  GRect bounds = layer_get_bounds(cell_layer);
  snprintf(header_text, sizeof(header_text), "History %d  S:%u/%u",
           history_count, streak_data.current_streak, streak_data.longest_streak);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, header_text,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(4, 0, bounds.size.w - 8, bounds.size.h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void history_menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  (void)data;
  int row = cell_index->row;
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  GColor background = highlighted ? GColorBlack : GColorWhite;
  GColor foreground = highlighted ? GColorWhite : GColorBlack;

  graphics_context_set_fill_color(ctx, background);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, foreground);

  if (row < 0 || row >= MAX_FASTS) {
    graphics_draw_text(ctx, "Unavailable",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(4, 0, bounds.size.w - 8, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return;
  }
  char title[24];
  char subtitle[96];
  format_history_row(row, title, sizeof(title), subtitle, sizeof(subtitle));
  graphics_draw_text(ctx, title,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(4, 2, bounds.size.w - 8, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, subtitle,
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(4, 20, bounds.size.w - 8, bounds.size.h - 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
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
  char note_text[40];
  const char *badge_label = milestone_badge_label_for_level(stage_level_for_elapsed(entry_duration_seconds(&s_history_edit_draft)));
  format_entry_datetime(s_history_edit_draft.start_time, start_text, sizeof(start_text));
  format_entry_datetime(s_history_edit_draft.end_time, end_text, sizeof(end_text));
  format_duration_hours_minutes(entry_duration_seconds(&s_history_edit_draft), duration_text, sizeof(duration_text));
  format_optional_tag_text("Note ", s_history_edit_draft.note, note_text, sizeof(note_text));

  snprintf(s_history_edit_title_text, sizeof(s_history_edit_title_text), "Edit %d/%d%s",
           s_history_edit_index + 1, history_count, s_history_edit_dirty ? "*" : "");
  snprintf(s_history_edit_start_text, sizeof(s_history_edit_start_text), "%cStart %s",
           s_history_edit_field == EDIT_FIELD_START ? '>' : ' ', start_text);
  snprintf(s_history_edit_end_text, sizeof(s_history_edit_end_text), "%cEnd   %s",
           s_history_edit_field == EDIT_FIELD_END ? '>' : ' ', end_text);
  snprintf(s_history_edit_duration_text, sizeof(s_history_edit_duration_text), "%c%s",
           s_history_edit_field == EDIT_FIELD_NOTE ? '>' : ' ', note_text);
  snprintf(s_history_edit_stage_text, sizeof(s_history_edit_stage_text), "Badge %s",
           badge_label ? badge_label : "--");
  snprintf(s_history_edit_hint_text, sizeof(s_history_edit_hint_text), "UP/DN adj SEL field\nHOLD save  BACK-hold del");

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

static void history_adjust_edit_note_by_delta(int delta) {
  if (s_history_edit_index < 0 || s_history_edit_index >= history_count || delta == 0) {
    return;
  }

  int tag_index = note_tag_index_for_entry(&s_history_edit_draft);
  int tag_count = history_note_tag_count();
  tag_index = (tag_index + delta) % tag_count;
  if (tag_index < 0) {
    tag_index += tag_count;
  }
  set_entry_note_from_tag_index(&s_history_edit_draft, tag_index);
  s_history_edit_dirty = true;
  refresh_history_edit_window_content();
}

static void history_edit_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (s_history_edit_field == EDIT_FIELD_NOTE) {
    history_adjust_edit_note_by_delta(1);
    return;
  }
  history_adjust_edit_draft_by_minutes(15);
}

static void history_edit_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (s_history_edit_field == EDIT_FIELD_NOTE) {
    history_adjust_edit_note_by_delta(-1);
    return;
  }
  history_adjust_edit_draft_by_minutes(-15);
}

static void history_edit_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (s_history_edit_field == EDIT_FIELD_START) {
    s_history_edit_field = EDIT_FIELD_END;
  } else if (s_history_edit_field == EDIT_FIELD_END) {
    s_history_edit_field = EDIT_FIELD_NOTE;
  } else {
    s_history_edit_field = EDIT_FIELD_START;
  }
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
  refresh_timer_view();
  refresh_stats_window_content();
  history_menu_reload();
  window_stack_remove(s_history_edit_window, true);
}

static void history_edit_back_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_history_edit_window, true);
}

/* Long-press BACK deletes the current entry and returns to the history list. */
static void history_edit_delete_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (!history_delete_entry(s_history_edit_index)) {
    return;
  }
  s_history_edit_index = -1;
  refresh_timer_view();
  refresh_stats_window_content();
  window_stack_remove(s_history_edit_window, true);
}

static void history_edit_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, history_edit_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, history_edit_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, history_edit_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, history_edit_back_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, history_edit_save_click_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_BACK, 700, history_edit_delete_click_handler, NULL);
}

static void running_fast_edit_apply_delta_minutes(int delta_minutes) {
  if (!fast_is_running() || delta_minutes == 0) {
    return;
  }

  time_t now = fastforge_now();
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
  if (!running_current_fast_is_at_target(now) && window_stack_contains_window(s_goal_window)) {
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

  time_t now = fastforge_now();
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

/* Cancel discards the running fast without saving it to history. */
static void menu_cancel_current_callback(int index, void *context) {
  (void)index;
  (void)context;
  if (!fast_cancel()) {
    show_placeholder_window("NOT RUNNING", "There is no active fast to cancel.", "BACK Menu");
    return;
  }
  show_placeholder_window("FAST CANCELLED", "Fast discarded. Not in history.", "BACK Menu");
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

static void menu_settings_callback(int index, void *context) {
  (void)index;
  (void)context;
  refresh_settings_window_content();
  safe_push_window(s_settings_window, true);
}

static void menu_backup_callback(int index, void *context) {
  (void)index;
  (void)context;
  request_history_export();
}

/* Show author credit and source code location in the shared detail window. */
static void menu_about_callback(int index, void *context) {
  (void)index;
  (void)context;
  show_placeholder_window("ABOUT",
                          "By Paul Buetow\n\nSource code:\ncodeberg.org/\nsnonux/fastforge",
                          "BACK Menu");
}

/* Start a fast whose alarm fires after 10 seconds, used for quick dev/test
 * runs of the goal-reached flow.  The stored target_minutes stays 1 (the
 * SDK minimum) so the data model remains consistent; only the live alarm
 * timer is shortened via fastforge_reschedule_alarm_for_seconds(). */
static void start_fast_from_test_preset(void) {
  if (fast_is_running()) {
    show_placeholder_window("FAST RUNNING",
                            "Stop the current fast before starting a new one.",
                            "BACK Menu");
    return;
  }

  global_target_minutes = 1;
  if (!fast_start(1)) {
    show_placeholder_window("FAST RUNNING",
                            "Stop the current fast before starting a new one.",
                            "BACK Menu");
    return;
  }

  /* Shorten the alarm to 10 s (fast_start registered a 60 s one). */
  fastforge_reschedule_alarm_for_seconds(10);

  if (window_stack_contains_window(s_presets_window)) {
    window_stack_remove(s_presets_window, false);
  }
  safe_push_window(s_timer_window, true);
  refresh_all_ui_state();
}

static void preset_10s_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_test_preset();
}

static void preset_16h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(16 * 60);
}

static void preset_18h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(18 * 60);
}

static void preset_20h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(20 * 60);
}

static void preset_24h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(24 * 60);
}

static void preset_26h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(26 * 60);
}

static void preset_28h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(28 * 60);
}

static void preset_30h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(30 * 60);
}

static void preset_36h_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_preset(36 * 60);
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

 #ifdef DEBUG
static void timer_debug_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  show_debug_menu_window();
}
#endif

static void timer_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, timer_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, timer_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, timer_down_click_handler);
#ifdef DEBUG
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, timer_debug_long_click_handler, NULL);
#endif
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

static void settings_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  settings_adjust_target(30);
}

static void settings_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  settings_adjust_target(-30);
}

#ifdef DEBUG
static void settings_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  settings_toggle_developer_mode();
}
#endif

static void settings_back_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_settings_window, true);
}

static void settings_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, settings_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, settings_down_click_handler);
#ifdef DEBUG
  window_single_click_subscribe(BUTTON_ID_SELECT, settings_select_click_handler);
#endif
  window_single_click_subscribe(BUTTON_ID_BACK, settings_back_click_handler);
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
  refresh_goal_window_content(); /* keep elapsed time live while goal window is open */
  refresh_running_edit_window_content();
}

static void goal_background_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, theme_goal_background_color());
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

  s_title_layer = create_text_layer(GRect(0, 4, bounds.size.w, 24),
                                    GTextAlignmentCenter,
                                    FONT_KEY_GOTHIC_18_BOLD,
                                    GColorBlack, GColorClear, false);
  s_timer_layer = create_text_layer(GRect(0, 28, bounds.size.w, 42),
                                    GTextAlignmentCenter,
                                    FONT_KEY_BITHAM_34_MEDIUM_NUMBERS,
                                    GColorBlack, GColorClear, false);
  s_detail_layer = create_text_layer(GRect(0, 76, bounds.size.w, 24),
                                     GTextAlignmentCenter,
                                     FONT_KEY_GOTHIC_18,
                                     GColorBlack, GColorClear, false);

  s_progress_layer = layer_create(GRect(10, 104, bounds.size.w - 20, 12));
  layer_set_update_proc(s_progress_layer, timer_progress_update_proc);

  s_stage_layer = create_text_layer(GRect(0, 118, bounds.size.w, 20),
                                    GTextAlignmentCenter,
                                    FONT_KEY_GOTHIC_18_BOLD,
                                    GColorBlack, GColorClear, false);
  s_hint_layer = create_text_layer(GRect(0, 138, bounds.size.w, 28),
                                   GTextAlignmentCenter,
                                   FONT_KEY_GOTHIC_14,
                                   GColorBlack, GColorClear, true);

  add_text_layer(window_layer, s_title_layer);
  add_text_layer(window_layer, s_timer_layer);
  add_text_layer(window_layer, s_detail_layer);
  layer_add_child(window_layer, s_progress_layer);
  add_text_layer(window_layer, s_stage_layer);
  add_text_layer(window_layer, s_hint_layer);
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

  s_goal_title_layer = create_text_layer(GRect(0, 20, bounds.size.w, 30),
                                         GTextAlignmentCenter,
                                         FONT_KEY_GOTHIC_28_BOLD,
                                         theme_goal_text_color(), theme_goal_background_color(), false);
  text_layer_set_text(s_goal_title_layer, "GOAL HIT");

  s_goal_time_layer = create_text_layer(GRect(0, 56, bounds.size.w, 26),
                                        GTextAlignmentCenter,
                                        FONT_KEY_GOTHIC_24_BOLD,
                                        theme_goal_text_color(), theme_goal_background_color(), false);
  text_layer_set_text(s_goal_time_layer, "Elapsed 00:00:00");

  s_goal_stage_layer = create_text_layer(GRect(0, 84, bounds.size.w, 24),
                                         GTextAlignmentCenter,
                                         FONT_KEY_GOTHIC_18_BOLD,
                                         theme_goal_text_color(), theme_goal_background_color(), false);
  text_layer_set_text(s_goal_stage_layer, "Stage: --");

  s_goal_hint_layer = create_text_layer(GRect(0, 120, bounds.size.w, 42),
                                        GTextAlignmentCenter,
                                        FONT_KEY_GOTHIC_14_BOLD,
                                        theme_goal_text_color(), theme_goal_background_color(), true);
  /* Fast continues automatically; any key dismisses this overlay. */
  text_layer_set_text(s_goal_hint_layer, "BACK/DN Dismiss\nSEL Stop fast");

  add_text_layer(window_layer, s_goal_title_layer);
  add_text_layer(window_layer, s_goal_time_layer);
  add_text_layer(window_layer, s_goal_stage_layer);
  add_text_layer(window_layer, s_goal_hint_layer);
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
  menu_layer_set_normal_colors(s_history_menu_layer,
                               GColorWhite,
                               GColorBlack);
  menu_layer_set_highlight_colors(s_history_menu_layer,
                                  GColorBlack,
                                  GColorWhite);
  menu_layer_set_click_config_onto_window(s_history_menu_layer, window);
  menu_layer_set_callbacks(s_history_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = history_menu_get_num_sections,
    .get_num_rows = history_menu_get_num_rows,
    .get_header_height = history_menu_get_header_height,
    .get_cell_height = history_menu_get_cell_height,
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

  s_history_edit_title_layer = create_text_layer(GRect(4, 4, bounds.size.w - 8, 24),
                                                 GTextAlignmentCenter,
                                                 FONT_KEY_GOTHIC_18_BOLD,
                                                 GColorBlack, GColorClear, false);
  s_history_edit_start_layer = create_text_layer(GRect(6, 30, bounds.size.w - 12, 22),
                                                 GTextAlignmentLeft,
                                                 FONT_KEY_GOTHIC_18_BOLD,
                                                 GColorBlack, GColorClear, false);
  s_history_edit_end_layer = create_text_layer(GRect(6, 54, bounds.size.w - 12, 22),
                                               GTextAlignmentLeft,
                                               FONT_KEY_GOTHIC_18_BOLD,
                                               GColorBlack, GColorClear, false);
  s_history_edit_duration_layer = create_text_layer(GRect(6, 82, bounds.size.w - 12, 22),
                                                    GTextAlignmentLeft,
                                                    FONT_KEY_GOTHIC_18_BOLD,
                                                    GColorBlack, GColorClear, false);
  s_history_edit_stage_layer = create_text_layer(GRect(6, 106, bounds.size.w - 12, 22),
                                                 GTextAlignmentLeft,
                                                 FONT_KEY_GOTHIC_18_BOLD,
                                                 GColorBlack, GColorClear, false);
  s_history_edit_hint_layer = create_text_layer(GRect(4, 130, bounds.size.w - 8, 34),
                                                GTextAlignmentCenter,
                                                FONT_KEY_GOTHIC_14,
                                                GColorBlack, GColorClear, false);

  add_text_layer(window_layer, s_history_edit_title_layer);
  add_text_layer(window_layer, s_history_edit_start_layer);
  add_text_layer(window_layer, s_history_edit_end_layer);
  add_text_layer(window_layer, s_history_edit_duration_layer);
  add_text_layer(window_layer, s_history_edit_stage_layer);
  add_text_layer(window_layer, s_history_edit_hint_layer);
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

  s_running_edit_title_layer = create_text_layer(GRect(4, 4, bounds.size.w, 26),
                                                 GTextAlignmentCenter,
                                                 FONT_KEY_GOTHIC_24_BOLD,
                                                 GColorBlack, GColorClear, false);
  s_running_edit_start_layer = create_text_layer(GRect(6, 36, bounds.size.w - 12, 24),
                                                 GTextAlignmentLeft,
                                                 FONT_KEY_GOTHIC_18_BOLD,
                                                 GColorBlack, GColorClear, false);
  s_running_edit_elapsed_layer = create_text_layer(GRect(6, 62, bounds.size.w - 12, 24),
                                                    GTextAlignmentLeft,
                                                    FONT_KEY_GOTHIC_18_BOLD,
                                                    GColorBlack, GColorClear, false);
  s_running_edit_goal_layer = create_text_layer(GRect(6, 88, bounds.size.w - 12, 34),
                                                GTextAlignmentLeft,
                                                FONT_KEY_GOTHIC_18,
                                                GColorBlack, GColorClear, false);
  s_running_edit_hint_layer = create_text_layer(GRect(4, 124, bounds.size.w - 8, 40),
                                                GTextAlignmentCenter,
                                                FONT_KEY_GOTHIC_14,
                                                GColorBlack, GColorClear, false);

  add_text_layer(window_layer, s_running_edit_title_layer);
  add_text_layer(window_layer, s_running_edit_start_layer);
  add_text_layer(window_layer, s_running_edit_elapsed_layer);
  add_text_layer(window_layer, s_running_edit_goal_layer);
  add_text_layer(window_layer, s_running_edit_hint_layer);
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

  s_stats_title_layer = create_text_layer(GRect(4, 8, bounds.size.w - 8, 26),
                                          GTextAlignmentCenter,
                                          FONT_KEY_GOTHIC_24_BOLD,
                                          GColorBlack, GColorClear, false);
  text_layer_set_text(s_stats_title_layer, "STATISTICS");

  s_stats_body_layer = create_text_layer(GRect(6, 40, bounds.size.w - 12, 98),
                                         GTextAlignmentLeft,
                                         FONT_KEY_GOTHIC_18_BOLD,
                                         GColorBlack, GColorClear, false);

  s_stats_hint_layer = create_text_layer(GRect(4, 140, bounds.size.w - 8, 24),
                                         GTextAlignmentCenter,
                                         FONT_KEY_GOTHIC_14_BOLD,
                                         GColorBlack, GColorClear, false);
  text_layer_set_text(s_stats_hint_layer, "BACK Menu");

  add_text_layer(window_layer, s_stats_title_layer);
  add_text_layer(window_layer, s_stats_body_layer);
  add_text_layer(window_layer, s_stats_hint_layer);
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

static void settings_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_settings_title_layer = create_text_layer(GRect(0, 6, bounds.size.w, 26),
                                             GTextAlignmentCenter,
                                             FONT_KEY_GOTHIC_24_BOLD,
                                             GColorBlack, GColorClear, false);
  text_layer_set_text(s_settings_title_layer, "SETTINGS");

  s_settings_target_layer = create_text_layer(GRect(6, 42, bounds.size.w - 12, 24),
                                              GTextAlignmentCenter,
                                              FONT_KEY_GOTHIC_18_BOLD,
                                              GColorBlack, GColorClear, false);

  s_settings_hint_layer = create_text_layer(GRect(6, 94, bounds.size.w - 12, 54),
                                            GTextAlignmentCenter,
                                            FONT_KEY_GOTHIC_14,
                                            GColorBlack, GColorClear, true);

#ifdef DEBUG
  s_settings_dev_layer = create_text_layer(GRect(6, 76, bounds.size.w - 12, 18),
                                           GTextAlignmentCenter,
                                           FONT_KEY_GOTHIC_14_BOLD,
                                           GColorBlack, GColorClear, false);
#endif

  add_text_layer(window_layer, s_settings_title_layer);
  add_text_layer(window_layer, s_settings_target_layer);
#ifdef DEBUG
  add_text_layer(window_layer, s_settings_dev_layer);
#endif
  add_text_layer(window_layer, s_settings_hint_layer);
  refresh_settings_window_content();
}

static void settings_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_settings_title_layer);
  s_settings_title_layer = NULL;
  text_layer_destroy(s_settings_target_layer);
  s_settings_target_layer = NULL;
#ifdef DEBUG
  text_layer_destroy(s_settings_dev_layer);
  s_settings_dev_layer = NULL;
#endif
  text_layer_destroy(s_settings_hint_layer);
  s_settings_hint_layer = NULL;
}

static void settings_window_appear(Window *window) {
  (void)window;
  refresh_settings_window_content();
}

static void detail_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_placeholder_title_layer = create_text_layer(GRect(4, 8, bounds.size.w - 8, 26),
                                                GTextAlignmentCenter,
                                                FONT_KEY_GOTHIC_24_BOLD,
                                                GColorBlack, GColorClear, false);
  s_placeholder_body_layer = create_text_layer(GRect(6, 40, bounds.size.w - 12, 98),
                                               GTextAlignmentCenter,
                                               FONT_KEY_GOTHIC_18,
                                               GColorBlack, GColorClear, false);
  s_placeholder_hint_layer = create_text_layer(GRect(4, 140, bounds.size.w - 8, 24),
                                               GTextAlignmentCenter,
                                               FONT_KEY_GOTHIC_14_BOLD,
                                               GColorBlack, GColorClear, false);

  add_text_layer(window_layer, s_placeholder_title_layer);
  add_text_layer(window_layer, s_placeholder_body_layer);
  add_text_layer(window_layer, s_placeholder_hint_layer);
  /* Buffers were already filled by show_placeholder_window before the window
   * was pushed.  Set the layer pointers directly to avoid snprintf(buf, "%s",
   * buf) undefined-behaviour (self-copy clears the string on Pebble's libc). */
  text_layer_set_text(s_placeholder_title_layer, s_placeholder_title_text);
  text_layer_set_text(s_placeholder_body_layer, s_placeholder_body_text);
  text_layer_set_text(s_placeholder_hint_layer, s_placeholder_hint_text);
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
  s_main_menu_items[MAIN_MENU_INDEX_CANCEL_CURRENT] = (SimpleMenuItem) {
    .title = "Cancel Current Fast",
    .subtitle = "",
    .callback = menu_cancel_current_callback
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
  s_main_menu_items[MAIN_MENU_INDEX_ABOUT] = (SimpleMenuItem) {
    .title = "About",
    .subtitle = "Author and source",
    .callback = menu_about_callback
  };

  s_main_menu_sections[0] = (SimpleMenuSection) {
    .title = "FastForge",
    .num_items = MAIN_MENU_ITEM_COUNT,
    .items = s_main_menu_items
  };
}

static void configure_preset_items(void) {
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
  s_presets_menu_items[PRESET_MENU_INDEX_26H] = (SimpleMenuItem) {
    .title = "26 hours",
    .subtitle = "Long adaptation",
    .callback = preset_26h_callback
  };
  s_presets_menu_items[PRESET_MENU_INDEX_28H] = (SimpleMenuItem) {
    .title = "28 hours",
    .subtitle = "Extended burn",
    .callback = preset_28h_callback
  };
  s_presets_menu_items[PRESET_MENU_INDEX_30H] = (SimpleMenuItem) {
    .title = "30 hours",
    .subtitle = "Deep focus",
    .callback = preset_30h_callback
  };
  s_presets_menu_items[PRESET_MENU_INDEX_36H] = (SimpleMenuItem) {
    .title = "36 hours",
    .subtitle = "Deep ketosis push",
    .callback = preset_36h_callback
  };
  /* Dev/test preset: alarm fires after 10 s so the goal-reached flow can be
   * exercised quickly without waiting hours. Kept permanently as last item. */
  s_presets_menu_items[PRESET_MENU_INDEX_10S] = (SimpleMenuItem) {
    .title = "10 seconds",
    .subtitle = "Dev: test goal alarm",
    .callback = preset_10s_callback
  };

  s_presets_menu_sections[0] = (SimpleMenuSection) {
    .title = "Start New Fast",
    .num_items = PRESET_MENU_ITEM_COUNT,
    .items = s_presets_menu_items
  };
}

static void init_primary_windows(void) {
  s_menu_window = create_window_with_handlers((WindowHandlers) {
    .load = menu_window_load,
    .appear = menu_window_appear,
    .unload = menu_window_unload
  }, NULL);
  window_set_background_color(s_menu_window, theme_surface_background_color());

  s_timer_window = create_window_with_handlers((WindowHandlers) {
    .load = timer_window_load,
    .unload = timer_window_unload
  }, timer_click_config_provider);
  window_set_background_color(s_timer_window, theme_surface_background_color());

  s_goal_window = create_window_with_handlers((WindowHandlers) {
    .load = goal_window_load,
    .unload = goal_window_unload
  }, goal_click_config_provider);
  window_set_background_color(s_goal_window, theme_goal_background_color());

  s_presets_window = create_window_with_handlers((WindowHandlers) {
    .load = presets_window_load,
    .unload = presets_window_unload
  }, NULL);
  window_set_background_color(s_presets_window, theme_surface_background_color());
}

static void init_history_windows(void) {
  s_history_window = create_window_with_handlers((WindowHandlers) {
    .load = history_window_load,
    .appear = history_window_appear,
    .unload = history_window_unload
  }, NULL);

  s_history_edit_window = create_window_with_handlers((WindowHandlers) {
    .load = history_edit_window_load,
    .appear = history_edit_window_appear,
    .unload = history_edit_window_unload
  }, history_edit_click_config_provider);
  window_set_background_color(s_history_edit_window, theme_surface_background_color());

  s_running_edit_window = create_window_with_handlers((WindowHandlers) {
    .load = running_edit_window_load,
    .appear = running_edit_window_appear,
    .unload = running_edit_window_unload
  }, running_edit_click_config_provider);
  window_set_background_color(s_running_edit_window, theme_surface_background_color());
  window_set_background_color(s_history_window, GColorWhite);
}

static void init_info_windows(void) {
  s_stats_window = create_window_with_handlers((WindowHandlers) {
    .load = stats_window_load,
    .appear = stats_window_appear,
    .unload = stats_window_unload
  }, NULL);
  window_set_background_color(s_stats_window, theme_surface_background_color());

  s_settings_window = create_window_with_handlers((WindowHandlers) {
    .load = settings_window_load,
    .appear = settings_window_appear,
    .unload = settings_window_unload
  }, settings_click_config_provider);
  window_set_background_color(s_settings_window, theme_surface_background_color());
  s_detail_window = create_window_with_handlers((WindowHandlers) {
    .load = detail_window_load,
    .unload = detail_window_unload
  }, placeholder_click_config_provider);
  window_set_background_color(s_detail_window, theme_surface_background_color());
}

#ifdef DEBUG
static void init_debug_window(void) {
  s_debug_window = create_window_with_handlers((WindowHandlers) {
    .load = debug_window_load,
    .appear = debug_window_appear,
    .unload = debug_window_unload
  }, NULL);
}
#endif

static void init_windows(void) {
  init_primary_windows();
  init_history_windows();
  init_info_windows();
#ifdef DEBUG
  init_debug_window();
#endif
}

static void destroy_windows(void) {
  window_destroy(s_detail_window);
#ifdef DEBUG
  window_destroy(s_debug_window);
#endif
  window_destroy(s_settings_window);
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
#ifndef PBL_PLATFORM_APLITE
  AppMessageResult app_message_result = app_message_open(128, 128);
  if (app_message_result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "app_message_open failed (%d)", app_message_result);
  } else {
    fastforge_history_register_app_message_handlers();
  }
#endif
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
#ifndef PBL_PLATFORM_APLITE
  fastforge_history_stop_export();
#endif
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
