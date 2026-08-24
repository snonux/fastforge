#include "fastforge_internal.h"

#include <stdlib.h>
#include <string.h>

enum {
  MAIN_MENU_INDEX_START_NEW = 0,
  MAIN_MENU_INDEX_RESUME_LAST = 1,  /* undo an accidental fast_stop */
  MAIN_MENU_INDEX_CURRENT_TIMER = 2,
  MAIN_MENU_INDEX_STOP_CURRENT = 3,
  MAIN_MENU_INDEX_CANCEL_CURRENT = 4,
  MAIN_MENU_INDEX_HISTORY = 5,
  MAIN_MENU_INDEX_STATS = 6,
  MAIN_MENU_INDEX_SETTINGS = 7,
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
  PRESET_MENU_INDEX_OPEN = 8, /* count up, no goal and no alarm */
  PRESET_MENU_INDEX_10S = 9,  /* dev/test: fires alarm after 10 s */
  PRESET_MENU_ITEM_COUNT = 10
};

/* Target duration behind each preset, in whole hours; 0 marks a preset with
 * no fixed duration (Open-ended, and the dev 10s test). Shared by the accent
 * colour ramp and the presets menu's projected "Ends HH:MM" line. */
static const uint8_t s_preset_target_hours[PRESET_MENU_ITEM_COUNT] = {
  [PRESET_MENU_INDEX_16H] = 16, [PRESET_MENU_INDEX_18H] = 18, [PRESET_MENU_INDEX_20H] = 20,
  [PRESET_MENU_INDEX_24H] = 24, [PRESET_MENU_INDEX_26H] = 26, [PRESET_MENU_INDEX_28H] = 28,
  [PRESET_MENU_INDEX_30H] = 30, [PRESET_MENU_INDEX_36H] = 36,
  [PRESET_MENU_INDEX_OPEN] = 0, [PRESET_MENU_INDEX_10S] = 0
};

static Window *s_menu_window;
static Window *s_timer_window;
static Window *s_goal_window;
static Window *s_stop_confirm_window;
static Window *s_presets_window;
static Window *s_settings_window;
static Window *s_stats_window;
static Window *s_detail_window;
static Window *s_about_window;
static Window *s_history_window;
static Window *s_history_edit_window;
static Window *s_delete_confirm_window;
static Window *s_running_edit_window;

static MenuLayer *s_main_menu_layer;
static MenuLayer *s_presets_menu_layer;
MenuLayer *s_history_menu_layer;
static SimpleMenuSection s_main_menu_sections[1];
static SimpleMenuSection s_presets_menu_sections[1];
static SimpleMenuItem s_main_menu_items[MAIN_MENU_ITEM_COUNT];
static char s_menu_resume_subtitle[40];
static SimpleMenuItem s_presets_menu_items[PRESET_MENU_ITEM_COUNT];
/* Left-edge accent bar per row, parallel to the item arrays above; populated
 * once by configure_main_menu_items()/configure_preset_items(). */
static GColor s_main_menu_accent_colors[MAIN_MENU_ITEM_COUNT];
static GColor s_presets_menu_accent_colors[PRESET_MENU_ITEM_COUNT];
/* "Ends HH:MM" projection per preset, refreshed every time the presets window
 * appears (the projection is relative to "now", so it must be recomputed on
 * each visit, not just once at startup). Rows with no fixed duration (Open
 * ended, dev 10s) are left as empty strings, which ff_menu_draw_row skips. */
static char s_presets_menu_end_time_lines[PRESET_MENU_ITEM_COUNT][32];
static const char *s_presets_menu_end_time_ptrs[PRESET_MENU_ITEM_COUNT];

/* Context passed to the generic MenuLayer callbacks so the same draw/select
 * code serves the main, preset, and debug menus.
 *
 * `map` is an optional indirection table: when non-NULL, menu row R is backed
 * by items[map[R]] instead of items[R]. The main menu uses this to hide items
 * that make no sense in the current state (e.g. "Stop Current Fast" when no
 * fast is running). Preset/debug menus leave `map` NULL for an identity map.
 *
 * `accent_colors` is an optional array parallel to `items` (indexed by the
 * *backing* item index, same as `map` resolves to, not the visible row) that
 * draws a slim left-edge bar per row, the same device already used for
 * History's stage-color bar. NULL means no bar (used for the debug menu).
 *
 * `third_lines` is an optional array of strings, also parallel to `items`,
 * drawn as an extra line below the subtitle (used by the presets menu to
 * show the projected end-of-fast clock time). A NULL or empty entry for a
 * given row skips the line (e.g. Open-ended has no fixed end time). NULL
 * disables the extra line for the whole menu, which also keeps cell height
 * at the normal two-line size. */
typedef struct {
  const SimpleMenuItem *items;
  int count;
  const char *header;
  const int *map;
  const GColor *accent_colors;
  const char *const *third_lines;
} FfMenuCtx;
static FfMenuCtx s_main_menu_ctx;
static FfMenuCtx s_presets_menu_ctx;
/* Visible-row indirection for the main menu, rebuilt by sync_main_menu_state. */
static int s_main_menu_map[MAIN_MENU_ITEM_COUNT];
#ifdef DEBUG
static FfMenuCtx s_debug_menu_ctx;
#endif

static TextLayer *s_title_layer;
static TextLayer *s_timer_layer;
#if FASTFORGE_SHOW_GOAL_CLOCK
static TextLayer *s_eta_layer;
#endif
static TextLayer *s_detail_layer;
static TextLayer *s_stage_layer;
static TextLayer *s_hint_layer;
static Layer *s_progress_layer;
static Layer *s_timer_indicator_layer;
/* Current timer foreground colour (set by apply_timer_theme) so the page
 * indicator can draw its active chevron in the same colour as the text. */
static GColor s_timer_foreground = GColorBlack;

static Layer *s_goal_background_layer;
static TextLayer *s_goal_title_layer;
static TextLayer *s_goal_time_layer;
static TextLayer *s_goal_stage_layer;
static TextLayer *s_goal_hint_layer;
static TextLayer *s_stop_confirm_title_layer;
static TextLayer *s_stop_confirm_caption_layer; /* "Fasted" caption above the hero time */
static TextLayer *s_stop_confirm_time_layer;    /* hero fasting time, large font */
static TextLayer *s_stop_confirm_body_layer;    /* "Save to history?" question */
static TextLayer *s_stop_confirm_hint_layer;

/* Delete-confirmation window (s31): shown before a history entry is removed.
 * Mirrors the stop-confirm layout but swaps the hero time for a two-line
 * body describing the entry being deleted (duration + end datetime), so the
 * user can confirm they are removing the right fast. */
static TextLayer *s_delete_confirm_title_layer;
static TextLayer *s_delete_confirm_body_layer;
static TextLayer *s_delete_confirm_hint_layer;
static char s_delete_confirm_body_text[56];

static TextLayer *s_settings_title_layer;
static TextLayer *s_settings_target_layer;
static TextLayer *s_settings_min_layer;
#ifdef DEBUG
static TextLayer *s_settings_dev_layer;
#endif
static TextLayer *s_settings_hint_layer;
/* Which settings field UP/DOWN adjusts; SELECT cycles it. */
typedef enum { SETTINGS_FIELD_TARGET = 0, SETTINGS_FIELD_MIN_FAST = 1 } SettingsField;
static SettingsField s_settings_field = SETTINGS_FIELD_TARGET;
static TextLayer *s_placeholder_title_layer;
static TextLayer *s_placeholder_body_layer;
static TextLayer *s_placeholder_hint_layer;
static TextLayer *s_about_title_layer;
static TextLayer *s_about_body_layer;
static TextLayer *s_about_hint_layer;
static TextLayer *s_history_title_layer;
static TextLayer *s_stats_title_layer;
static TextLayer *s_stats_value_layer;
static TextLayer *s_stats_sub_layer;
static TextLayer *s_stats_hint_layer;
static Layer *s_stats_indicator_layer;
static uint8_t s_stats_page = 0;
static uint8_t s_stats_page_count = 1;
#define STATS_PAGE_COUNT 5
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
#if FASTFORGE_SHOW_GOAL_CLOCK
static char s_eta_text[32];
#endif
static char s_detail_text[48];
static char s_stage_text[32];
static char s_goal_time_text[24];
static char s_goal_stage_text[24];
static char s_settings_target_text[32];
static char s_settings_min_text[32];
#ifdef DEBUG
static char s_settings_dev_text[32];
#endif
static char s_menu_stop_subtitle[32];
static char s_menu_cancel_subtitle[32];
static char s_stop_confirm_time_text[12];
static char s_history_title_text[32];
static char s_placeholder_title_text[24];
static char s_placeholder_body_text[160];
static char s_placeholder_hint_text[24];
static char s_history_edit_title_text[48];
static char s_history_edit_start_text[32];
static char s_history_edit_end_text[32];
static char s_history_edit_duration_text[48];
static char s_history_edit_stage_text[24];
/* 44 bytes needed: "UP/DN adj SEL field\nHOLD save  DN-hold del" + NUL */
static char s_history_edit_hint_text[48];
static char s_running_edit_start_text[32];
static char s_running_edit_elapsed_text[32];
static char s_running_edit_goal_text[32];
static int s_history_edit_index = -1;
static FastEntry s_history_edit_draft = {0};
#ifdef DEBUG
static Window *s_debug_window;
static MenuLayer *s_debug_menu_layer;
static SimpleMenuSection s_debug_menu_sections[1];
static SimpleMenuItem s_debug_menu_items[6];
static char s_debug_menu_clock_text[40];
#endif

/* Timer screen has two sub-screens (pages) because the hero countdown, goal
 * clock, progress bar, detail, stage, and hint do not all fit at the largest
 * fonts on the round display. UP/DOWN switches pages. */
static uint8_t s_timer_page = 0;
#define TIMER_PAGE_COUNT 2

/* The detail/about screen is ScrollLayer-backed so its long body text can
 * scroll; the statistics screen is now paged (one large-font stat per page).
 */
static ScrollLayer *s_detail_scroll_layer;

typedef enum {
  EDIT_FIELD_START = 0,
  EDIT_FIELD_END = 1,
  EDIT_FIELD_NOTE = 2
} EditField;

static void refresh_timer_view(void);
static void refresh_timer_page(void);
static void refresh_goal_window_content(void);
static void sync_main_menu_state(void);
static void refresh_running_edit_window_content(void);
static void refresh_settings_window_content(void);
static MenuLayer *create_ff_menu_layer(Window *window, GRect bounds, FfMenuCtx *ctx);
void fastforge_detail_layout_refresh(void);
static void show_stop_confirmation(void);
static void show_delete_confirmation(void);
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
  fastforge_detail_layout_refresh();
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
  /* GColorIslamicGreen gives a vivid celebratory contrast against white text
   * and is clearly distinct from the mint-green running state. */
  return is_color_platform() ? GColorIslamicGreen : GColorBlack;
}

static GColor theme_goal_text_color(void) {
  return GColorWhite;
}

static GColor theme_timer_background_color(bool goal_reached) {
  if (goal_reached) return theme_goal_background_color();
  /* Blue background while countdown is active; mint green when idle (no fast). */
  if (fast_is_running()) return is_color_platform() ? GColorVividCerulean : GColorWhite;
  return theme_surface_background_color();
}

static GColor theme_timer_text_color(bool goal_reached) {
  if (goal_reached) return theme_goal_text_color();
  /* White text on blue countdown background for readability. */
  if (fast_is_running()) return is_color_platform() ? GColorWhite : GColorBlack;
  return GColorBlack;
}

static GColor theme_progress_track_color(void) {
  return GColorLightGray;
}

static GColor theme_progress_fill_color(void) {
  return is_color_platform() ? GColorJaegerGreen : GColorGreen;
}

/* Functional colour per fasting stage so the metabolic state reads at a glance
 * on the light surface screens (history, history-edit). Chosen to stay
 * readable on both the white and black (highlighted) menu row backgrounds. */
static GColor stage_color_for_level(uint8_t level) {
  if (!is_color_platform()) {
    return GColorBlack;
  }
  switch (level) {
    case 3: return GColorIslamicGreen;        /* DEEP KETOSIS  */
    case 2: return GColorCobaltBlue;             /* EARLY KETOSIS*/
    case 1: return GColorOrange;               /* FAT BURN     */
    default: return GColorDarkGray;            /* GLYCOGEN/none*/
  }
}

/* One fixed accent colour per main-menu item, so the row keeps its identity
 * regardless of which mutually-exclusive state items (Start/Resume/Current
 * Timer, Stop/Cancel) happen to be visible. Start New Fast and Current Timer
 * share the running-timer blue because they lead to the same screen and are
 * never shown together. */
static GColor main_menu_accent_color_for_index(int index) {
  if (!is_color_platform()) {
    return GColorBlack;
  }
  switch (index) {
    case MAIN_MENU_INDEX_START_NEW:
    case MAIN_MENU_INDEX_CURRENT_TIMER:  return GColorVividCerulean;
    case MAIN_MENU_INDEX_RESUME_LAST:    return GColorPictonBlue;
    case MAIN_MENU_INDEX_STOP_CURRENT:   return GColorSunsetOrange;
    case MAIN_MENU_INDEX_CANCEL_CURRENT: return GColorDarkCandyAppleRed;
    case MAIN_MENU_INDEX_HISTORY:        return GColorIndigo;
    case MAIN_MENU_INDEX_STATS:          return GColorDukeBlue;
    case MAIN_MENU_INDEX_SETTINGS:       return GColorCadetBlue;
    default:                             return GColorLiberty; /* About */
  }
}

/* Preset accent colour reuses the fasting-stage ramp keyed to the preset's
 * own target duration, so the list previews the metabolic depth each preset
 * commits to. Open-ended (no fixed target) and the dev 10s preset fall
 * through to level 0 (neutral grey) since neither has a real duration. */
static GColor preset_menu_accent_color_for_index(int index) {
  time_t target_seconds = (time_t)s_preset_target_hours[index] * 3600;
  return stage_color_for_level(stage_level_for_elapsed(target_seconds));
}

static bool timer_goal_reached_for_elapsed(time_t elapsed_seconds) {
  uint32_t target_seconds = current_fast.target_minutes * 60;
  return target_seconds > 0 && elapsed_seconds >= (time_t)target_seconds;
}

static void apply_timer_theme(bool goal_reached) {
  GColor background = theme_timer_background_color(goal_reached);
  GColor foreground = theme_timer_text_color(goal_reached);
  s_timer_foreground = foreground;
  window_set_background_color(s_timer_window, background);
  text_layer_set_text_color(s_title_layer, foreground);
  text_layer_set_text_color(s_timer_layer, foreground);
#if FASTFORGE_SHOW_GOAL_CLOCK
  text_layer_set_text_color(s_eta_layer, foreground);
#endif
  text_layer_set_text_color(s_detail_layer, foreground);
  text_layer_set_text_color(s_stage_layer, foreground);
  text_layer_set_text_color(s_hint_layer, foreground);
  if (s_timer_indicator_layer) {
    layer_mark_dirty(s_timer_indicator_layer);
  }
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

/* Layout helper: content origin and width for the current display shape.
 *
 * FastForge targets two large displays:
 *   - emery  (Pebble Time 2): 200x228 rectangular color
 *   - gabbro (Pebble Round 2): 260x260 round color
 * On the round display we inset horizontally by 1/6 of the width and vertically
 * by 1/8 of the height so fixed-layout rows stay inside the visible circle.
 * On the rectangular display a small 6 px margin keeps text off the bezel. */
typedef struct { int16_t ox; int16_t oy; int16_t cw; } ContentRect;
static ContentRect content_rect(GRect bounds) {
#ifdef PBL_ROUND
  int16_t inset_x = bounds.size.w / 6;
  int16_t inset_y = bounds.size.h / 8;
  return (ContentRect){ inset_x, inset_y, bounds.size.w - 2 * inset_x };
#else
  return (ContentRect){ 6, 6, bounds.size.w - 12 };
#endif
}

/* Font choices. The two target platforms share the same system font set, so a
 * single set of macros sizes every screen. Fonts are pushed as large as the
 * layouts in each *_window_load() allow. */
#define FF_FONT_MENU_TITLE   FONT_KEY_GOTHIC_24_BOLD  /* menu section/cell title */
#define FF_FONT_MENU_SUB     FONT_KEY_GOTHIC_18       /* menu cell subtitle     */
#define FF_FONT_SCREEN_TITLE FONT_KEY_GOTHIC_28_BOLD  /* full-screen titles     */
#define FF_FONT_HERO         FONT_KEY_BITHAM_42_BOLD  /* big countdown number   */
#define FF_FONT_BIG          FONT_KEY_GOTHIC_28_BOLD  /* large body line        */
#define FF_FONT_BODY_BOLD    FONT_KEY_GOTHIC_24_BOLD  /* primary body text      */
#define FF_FONT_BODY         FONT_KEY_GOTHIC_24       /* regular body text      */
#define FF_FONT_SUB_BOLD     FONT_KEY_GOTHIC_18_BOLD  /* secondary line         */
#define FF_FONT_SUB          FONT_KEY_GOTHIC_18       /* hint / small text      */
#define FF_FONT_HINT         FONT_KEY_GOTHIC_18_BOLD  /* control hints          */

/* Vertical pixel height each font occupies (ascender+descender). Used to size
 * layer frames so glyphs never get clipped. These match the Pebble system
 * font metrics for the families used above. */
#define FF_H_MENU_TITLE   26
#define FF_H_MENU_SUB     22
#define FF_H_SCREEN_TITLE 34
#define FF_H_HERO         50
#define FF_H_BIG          34
#define FF_H_BODY_BOLD    28
#define FF_H_BODY         28
#define FF_H_SUB_BOLD     22
#define FF_H_SUB          22
#define FF_H_HINT         22

static uint16_t clamp_default_target_minutes(int target_minutes) {
  if (target_minutes < 8 * 60) {
    return 8 * 60;
  }
  if (target_minutes > 48 * 60) {
    return 48 * 60;
  }
  return (uint16_t)target_minutes;
}

static uint16_t clamp_min_fast_minutes(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > MAX_MIN_FAST_MINUTES) {
    return MAX_MIN_FAST_MINUTES;
  }
  return (uint16_t)value;
}

static void refresh_settings_window_content(void) {
  if (!s_settings_target_layer || !s_settings_hint_layer || !s_settings_min_layer) {
    return;
  }

  snprintf(s_settings_target_text, sizeof(s_settings_target_text), "Default: %dh %02dm",
           global_target_minutes / 60, global_target_minutes % 60);
  if (global_min_fast_minutes == 0) {
    snprintf(s_settings_min_text, sizeof(s_settings_min_text), "Min fast: Off");
  } else {
    snprintf(s_settings_min_text, sizeof(s_settings_min_text), "Min fast: %dm",
             global_min_fast_minutes);
  }

  /* The selected field is drawn black; the other is dimmed so the cursor is
   * obvious on the mint-green settings surface. */
  GColor sel = GColorBlack;
  GColor dim = GColorDarkGray;
  text_layer_set_text_color(s_settings_target_layer,
                            s_settings_field == SETTINGS_FIELD_TARGET ? sel : dim);
  text_layer_set_text_color(s_settings_min_layer,
                            s_settings_field == SETTINGS_FIELD_MIN_FAST ? sel : dim);
  text_layer_set_text(s_settings_target_layer, s_settings_target_text);
  text_layer_set_text(s_settings_min_layer, s_settings_min_text);
#ifdef DEBUG
  snprintf(s_settings_dev_text, sizeof(s_settings_dev_text), "Dev Mode: %s",
           developer_mode_enabled ? "ON (timer dbg)" : "OFF");
  text_layer_set_text(s_settings_dev_layer, s_settings_dev_text);
  text_layer_set_text(s_settings_hint_layer, "UP/DN adjust\nSEL next / hold Dev\nBACK Save");
#else
  text_layer_set_text(s_settings_hint_layer, "UP/DN adjust\nSEL next field\nBACK Save");
#endif
}

static void settings_persist_and_refresh(void) {
  save_all_data();
  refresh_timer_view();
  refresh_settings_window_content();
}

/* UP/DOWN adjusts the selected setting: ±30 min for the default target, ±5 min
 * for the history minimum. */
static void settings_adjust_field(int direction) {
  if (s_settings_field == SETTINGS_FIELD_TARGET) {
    global_target_minutes =
        clamp_default_target_minutes((int)global_target_minutes + direction * 30);
  } else {
    global_min_fast_minutes =
        clamp_min_fast_minutes((int)global_min_fast_minutes + direction * 5);
  }
  settings_persist_and_refresh();
}

static void settings_cycle_field(void) {
  s_settings_field = (SettingsField)((s_settings_field + 1) % 2);
  refresh_settings_window_content();
}

#ifdef DEBUG
static void settings_toggle_developer_mode(void) {
  developer_mode_enabled = !developer_mode_enabled;
  settings_persist_and_refresh();
}
#endif

#ifdef DEBUG
static void debug_menu_select_callback(int index, void *context);

static void debug_refresh_menu(void) {
  snprintf(s_debug_menu_clock_text, sizeof(s_debug_menu_clock_text), "Debug %+ldh %s",
           (long)(s_fake_time_offset_seconds / 3600),
           s_fake_time_enabled ? "fake" : "real");
  /* Callbacks must be set here (not in load) because debug_refresh_menu is
   * also called from debug_window_appear, which would otherwise clobber them. */
  s_debug_menu_items[0] = (SimpleMenuItem) {
    .title = "+1 Hour",
    .subtitle = "Advance debug clock",
    .callback = debug_menu_select_callback
  };
  s_debug_menu_items[1] = (SimpleMenuItem) {
    .title = "+6 Hours",
    .subtitle = "Jump to next stage",
    .callback = debug_menu_select_callback
  };
  s_debug_menu_items[2] = (SimpleMenuItem) {
    .title = "+24 Hours",
    .subtitle = "Cross whole-day boundary",
    .callback = debug_menu_select_callback
  };
  s_debug_menu_items[3] = (SimpleMenuItem) {
    .title = "Use Real Clock",
    .subtitle = "Clear fake-time offset",
    .callback = debug_menu_select_callback
  };
  s_debug_menu_items[4] = (SimpleMenuItem) {
    .title = "Force Goal Alarm",
    .subtitle = "Trigger goal-hit flow now",
    .callback = debug_menu_select_callback
  };
  s_debug_menu_items[5] = (SimpleMenuItem) {
    .title = "Show Raw State",
    .subtitle = "Open debug snapshot",
    .callback = debug_menu_select_callback
  };
  s_debug_menu_sections[0] = (SimpleMenuSection) {
    .title = s_debug_menu_clock_text,
    .num_items = ARRAY_LENGTH(s_debug_menu_items),
    .items = s_debug_menu_items
  };
  if (s_debug_menu_layer) {
    menu_layer_reload_data(s_debug_menu_layer);
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
  ContentRect cr = content_rect(bounds);
  GRect menu_bounds = GRect(cr.ox, cr.oy, cr.cw, bounds.size.h - 2 * cr.oy);
  debug_refresh_menu();
  s_debug_menu_ctx = (FfMenuCtx){ s_debug_menu_items, (int)ARRAY_LENGTH(s_debug_menu_items),
                                  s_debug_menu_clock_text, NULL, NULL, NULL };
  s_debug_menu_layer = create_ff_menu_layer(window, menu_bounds, &s_debug_menu_ctx);
  layer_add_child(window_layer, menu_layer_get_layer(s_debug_menu_layer));
}

static void debug_window_unload(Window *window) {
  (void)window;
  menu_layer_destroy(s_debug_menu_layer);
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

/* Page indicator for the timer's two sub-screens: an up chevron (active when a
 * previous page exists), a "n/2" counter, and a down chevron (active when a
 * next page exists). Active chevrons use the current timer foreground colour;
 * inactive ones use mid-grey so they read on every timer background. */
static void timer_chevron(GContext *ctx, int16_t cx, int16_t cy, bool points_up,
                         GColor color) {
  const int16_t s = 6, t = 4;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  if (points_up) {
    graphics_draw_line(ctx, GPoint(cx - s, cy + t), GPoint(cx, cy - t));
    graphics_draw_line(ctx, GPoint(cx, cy - t), GPoint(cx + s, cy + t));
  } else {
    graphics_draw_line(ctx, GPoint(cx - s, cy - t), GPoint(cx, cy + t));
    graphics_draw_line(ctx, GPoint(cx, cy + t), GPoint(cx + s, cy - t));
  }
}

static void timer_indicator_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int16_t cx = bounds.size.w / 2;
  int16_t cy = bounds.size.h / 2;
  GColor active = s_timer_foreground;
  GColor inactive = GColorLightGray;
  bool up = (s_timer_page > 0);
  bool down = (s_timer_page < TIMER_PAGE_COUNT - 1);
  timer_chevron(ctx, cx - 30, cy, true,  up   ? active : inactive);
  timer_chevron(ctx, cx + 30, cy, false, down ? active : inactive);

  char label[8];
  snprintf(label, sizeof(label), "%u/%u", (unsigned)(s_timer_page + 1),
           (unsigned)TIMER_PAGE_COUNT);
  graphics_context_set_text_color(ctx, active);
  graphics_draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(cx - 20, cy - 8, 40, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
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

  /* Open-ended fasts have no target to scale the bar against, so they run on a
   * fixed 24 h window instead — that still places the 12/18/24 h stage ticks
   * and shows progress through the stages rather than an empty track. */
  uint32_t total_seconds = (current_fast.target_minutes > 0)
                               ? (uint32_t)current_fast.target_minutes * 60
                               : 24 * 3600;
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
#if FASTFORGE_SHOW_GOAL_CLOCK
  text_layer_set_text(s_eta_layer, s_eta_text);
#endif
  text_layer_set_text(s_detail_layer, s_detail_text);
  text_layer_set_text(s_stage_layer, s_stage_text);
  if (s_progress_layer) {
    layer_mark_dirty(s_progress_layer);
  }
}

/* Wall-clock time at which the running fast hits its target, shown right under
 * the countdown so the finishing time can be read off without doing the
 * arithmetic. Blank when no target is set — there is nothing to project then. */
static void refresh_timer_eta_text(uint32_t target_seconds, time_t remaining) {
#if FASTFORGE_SHOW_GOAL_CLOCK
  if (target_seconds == 0) {
    s_eta_text[0] = '\0';
    return;
  }

  char clock_text[20];
  format_clock_time(current_fast.start_time + (time_t)target_seconds,
                    fastforge_now(), clock_text, sizeof(clock_text));
  /* Kept short ("Goal", not "Goal was") so the larger GOTHIC_18 line still fits
   * the narrowest display once a weekday and an AM/PM suffix are present. */
  snprintf(s_eta_text, sizeof(s_eta_text),
           remaining > 0 ? "Ends %s" : "Goal %s", clock_text);
#else
  (void)target_seconds;
  (void)remaining;
#endif
}

static void refresh_timer_view_idle(void) {
  apply_timer_theme(false);
  /* '*' suffix on the title indicates developer mode is active. Short titles
   * keep the GOTHIC_28_BOLD title font on the narrow round display. */
  snprintf(s_title_text, sizeof(s_title_text),
           debug_controls_available() ? "NO FAST*" : "NO FAST");
  format_hhmmss(0, s_timer_text, sizeof(s_timer_text));
  refresh_timer_eta_text(0, 0);
  char target_text[20];
  format_duration_hours_minutes((time_t)global_target_minutes * 60,
                                target_text, sizeof(target_text));
  /* Page 1 hero counter mirrors the page-0 hero (elapsed is 0 while idle);
   * the target moves to the secondary line so it isn't squeezed into the hero
   * font. */
  format_hhmmss(0, s_detail_text, sizeof(s_detail_text));
  snprintf(s_stage_text, sizeof(s_stage_text), "Target %s", target_text);
}

static void refresh_timer_view_running(time_t elapsed) {
  update_max_stage_if_needed(elapsed);
  uint32_t target_seconds = current_fast.target_minutes * 60;
  bool goal_reached = timer_goal_reached_for_elapsed(elapsed);
  apply_timer_theme(goal_reached);
  /* '*' appended to title when developer mode is active. */
  bool dev = debug_controls_available();
  if (target_seconds > 0) {
    time_t remaining = (time_t)target_seconds - elapsed;
    if (remaining > 0) {
      snprintf(s_title_text, sizeof(s_title_text), dev ? "COUNTDOWN*" : "COUNTDOWN");
      /* Hero shows the time remaining. The hero font (BITHAM_42_BOLD) has no
       * room for the leading '-' on overtime, so once the goal is hit the hero
       * switches to showing the overtime amount as an absolute value — the
       * "GOAL!" title and green theme already signal that it is overtime. */
      format_hhmmss(remaining, s_timer_text, sizeof(s_timer_text));
    } else {
      snprintf(s_title_text, sizeof(s_title_text), dev ? "GOAL!*" : "GOAL!");
      format_hhmmss(-remaining, s_timer_text, sizeof(s_timer_text));
    }
    refresh_timer_eta_text(target_seconds, remaining);
    /* Page 1 hero counter: elapsed time, same hero font as the page-0 hero. */
    format_hhmmss(elapsed, s_detail_text, sizeof(s_detail_text));
  } else {
    /* Open-ended fast: nothing to count down to, so the big number counts up
     * and there is no goal clock to show. Page 1 mirrors the elapsed hero. */
    snprintf(s_title_text, sizeof(s_title_text), dev ? "OPEN FAST*" : "OPEN FAST");
    format_hhmmss(elapsed, s_timer_text, sizeof(s_timer_text));
    refresh_timer_eta_text(target_seconds, 0);
    format_hhmmss(elapsed, s_detail_text, sizeof(s_detail_text));
  }

  snprintf(s_stage_text, sizeof(s_stage_text), "Stage: %s",
           stage_text_for_elapsed(elapsed));
}

/* Hint text and layer visibility for the current timer sub-screen.
 * Page 0 = hero countdown + goal clock + progress; page 1 = elapsed/stage. */
static void refresh_timer_page(void) {
  if (!s_title_layer || !s_hint_layer) {
    return;
  }
  bool running = fast_is_running();
  bool page0 = (s_timer_page == 0);

  layer_set_hidden(text_layer_get_layer(s_title_layer), !page0);
  layer_set_hidden(text_layer_get_layer(s_timer_layer), !page0);
#if FASTFORGE_SHOW_GOAL_CLOCK
  layer_set_hidden(text_layer_get_layer(s_eta_layer), !page0);
#endif
  layer_set_hidden(s_progress_layer, !page0);
  layer_set_hidden(text_layer_get_layer(s_detail_layer), page0);
  layer_set_hidden(text_layer_get_layer(s_stage_layer), page0);
  if (s_timer_indicator_layer) {
    layer_mark_dirty(s_timer_indicator_layer);
  }

  if (page0) {
    text_layer_set_text(s_hint_layer, running ? "DN +   SEL stop"
                                              : "DN +   SEL start");
  } else {
    text_layer_set_text(s_hint_layer, running ? "HOLD SEL edit  BACK menu"
                                              : "SEL start  BACK menu");
  }
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
  refresh_timer_page();
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
  snprintf(s_goal_time_text, sizeof(s_goal_time_text), "%s", elapsed_text);
  snprintf(s_goal_stage_text, sizeof(s_goal_stage_text), "Stage: %s", stage_text_for_elapsed(elapsed));
  text_layer_set_text(s_goal_time_layer, s_goal_time_text);
  text_layer_set_text(s_goal_stage_layer, s_goal_stage_text);
}

/* Whether a main-menu item is relevant in the current state. Items that are
 * only meaningful with (or without) a running fast are hidden rather than
 * left as dead-end placeholder screens, so the menu only ever offers actions
 * the user can actually take. History/Statistics/Settings/About stay visible
 * in every state because they have their own empty-state messaging.
 *
 * sync_main_menu_state() rebuilds the visible-row map from this predicate on
 * every menu appear and after every fast start/stop/cancel/resume (via
 * refresh_all_ui_state). History edits that change history_count rely on the
 * menu_window_appear handler to re-sync when the user returns to the menu. */
static bool main_menu_item_is_visible(int index) {
  bool running = fast_is_running();
  switch (index) {
    case MAIN_MENU_INDEX_START_NEW:       return !running;
    case MAIN_MENU_INDEX_RESUME_LAST:     return !running && history_count > 0;
    /* The three running-only items below are hidden when idle so the menu never
     * offers a dead-end "not running" placeholder. */
    case MAIN_MENU_INDEX_CURRENT_TIMER:   return running;
    case MAIN_MENU_INDEX_STOP_CURRENT:    return running;
    case MAIN_MENU_INDEX_CANCEL_CURRENT:  return running;
    case MAIN_MENU_INDEX_HISTORY:
    case MAIN_MENU_INDEX_STATS:
    case MAIN_MENU_INDEX_SETTINGS:
    case MAIN_MENU_INDEX_ABOUT:
    default:                               return true;
  }
}

static void sync_main_menu_state(void) {
  if (fast_is_running()) {
    snprintf(s_menu_stop_subtitle, sizeof(s_menu_stop_subtitle), "End now and save");
    snprintf(s_menu_cancel_subtitle, sizeof(s_menu_cancel_subtitle), "Discard, no history");
  } else {
    snprintf(s_menu_stop_subtitle, sizeof(s_menu_stop_subtitle), "No fast running");
    snprintf(s_menu_cancel_subtitle, sizeof(s_menu_cancel_subtitle), "No fast running");
  }

  /* Resume subtitle: show the duration of the last fast when it is resumable. */
  if (!fast_is_running() && history_count > 0) {
    char dur[16];
    format_duration_hours_minutes(
      entry_duration_seconds(&history[history_count - 1]), dur, sizeof(dur));
    snprintf(s_menu_resume_subtitle, sizeof(s_menu_resume_subtitle), "Last: %s", dur);
  } else {
    snprintf(s_menu_resume_subtitle, sizeof(s_menu_resume_subtitle), "No previous fast");
  }

  s_main_menu_items[MAIN_MENU_INDEX_STOP_CURRENT].subtitle = s_menu_stop_subtitle;
  s_main_menu_items[MAIN_MENU_INDEX_CANCEL_CURRENT].subtitle = s_menu_cancel_subtitle;
  s_main_menu_items[MAIN_MENU_INDEX_RESUME_LAST].subtitle = s_menu_resume_subtitle;

  /* Rebuild the visible-row map and publish the live row count to the menu
   * context so the MenuLayer draws/selects only relevant items. */
  int visible = 0;
  for (int i = 0; i < MAIN_MENU_ITEM_COUNT; i++) {
    if (main_menu_item_is_visible(i)) {
      s_main_menu_map[visible++] = i;
    }
  }
  s_main_menu_ctx.count = visible;

  if (s_main_menu_layer) {
    menu_layer_reload_data(s_main_menu_layer);
    /* The visible set may have shrunk past the current selection (e.g. a fast
     * just stopped, removing the timer/stop/cancel rows). Clamp it back to a
     * valid row so the highlight does not land on an empty slot. */
    MenuIndex sel = menu_layer_get_selected_index(s_main_menu_layer);
    if (sel.row >= visible) {
      menu_layer_set_selected_index(
        s_main_menu_layer, (MenuIndex){.row = 0, .section = 0},
        MenuRowAlignTop, false);
    }
  }
}

/* ---- Generic MenuLayer callbacks (main / preset / debug menus) ----
 *
 * These menus used SimpleMenuLayer, which renders with fixed small system
 * fonts. To use the largest fonts that fit, each is now a MenuLayer driven by
 * an FfMenuCtx that points at its SimpleMenuItem array. The item's own
 * .callback is invoked on select, so menu behaviour is unchanged. */
static uint16_t ff_menu_get_num_sections(MenuLayer *menu_layer, void *data) {
  (void)menu_layer;
  (void)data;
  return 1;
}

static uint16_t ff_menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  (void)menu_layer;
  (void)section_index;
  const FfMenuCtx *ctx = data;
  return ctx ? (uint16_t)ctx->count : 0;
}

static int16_t ff_menu_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  (void)menu_layer;
  (void)section_index;
  const FfMenuCtx *ctx = data;
  return ctx && ctx->header ? FF_H_MENU_TITLE + 6 : 0;
}

static int16_t ff_menu_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  (void)menu_layer;
  /* One bold title line plus one regular subtitle line, with padding; rows
   * that also carry a third_lines entry (e.g. presets' "Ends HH:MM") grow by
   * one more subtitle-sized line instead of paying for it on every row. */
  int16_t height = FF_H_MENU_TITLE + FF_H_MENU_SUB + 12;
  const FfMenuCtx *ctx_data = data;
  if (ctx_data && ctx_data->third_lines && cell_index->row < ctx_data->count) {
    int row = ctx_data->map ? ctx_data->map[cell_index->row] : (int)cell_index->row;
    const char *third_line = ctx_data->third_lines[row];
    if (third_line && third_line[0] != '\0') {
      height += FF_H_MENU_SUB;
    }
  }
  return height;
}

static void ff_menu_draw_header(GContext *ctx, const Layer *cell_layer,
                                uint16_t section_index, void *data) {
  (void)section_index;
  const FfMenuCtx *ctx_data = data;
  if (!ctx_data || !ctx_data->header) {
    return;
  }
  GRect bounds = layer_get_bounds(cell_layer);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, ctx_data->header,
                     fonts_get_system_font(FF_FONT_MENU_TITLE),
                     GRect(4, 0, bounds.size.w - 8, FF_H_MENU_TITLE),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void ff_menu_draw_row(GContext *ctx, const Layer *cell_layer,
                             MenuIndex *cell_index, void *data) {
  const FfMenuCtx *ctx_data = data;
  if (!ctx_data || cell_index->row >= ctx_data->count) {
    return;
  }
  int row = ctx_data->map ? ctx_data->map[cell_index->row] : (int)cell_index->row;
  const SimpleMenuItem *item = &ctx_data->items[row];
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  GColor background = highlighted ? GColorBlack : GColorWhite;
  GColor foreground = highlighted ? GColorWhite : GColorBlack;

  graphics_context_set_fill_color(ctx, background);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (ctx_data->accent_colors) {
    graphics_context_set_fill_color(ctx, ctx_data->accent_colors[row]);
    graphics_fill_rect(ctx, GRect(0, 0, 4, bounds.size.h), 0, GCornerNone);
  }

  graphics_context_set_text_color(ctx, foreground);

  graphics_draw_text(ctx, item->title ? item->title : "",
                     fonts_get_system_font(FF_FONT_MENU_TITLE),
                     GRect(6, 2, bounds.size.w - 12, FF_H_MENU_TITLE),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (item->subtitle && item->subtitle[0] != '\0') {
    graphics_draw_text(ctx, item->subtitle,
                       fonts_get_system_font(FF_FONT_MENU_SUB),
                       GRect(6, FF_H_MENU_TITLE + 4, bounds.size.w - 12, FF_H_MENU_SUB),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
  const char *third_line = ctx_data->third_lines ? ctx_data->third_lines[row] : NULL;
  if (third_line && third_line[0] != '\0') {
    graphics_draw_text(ctx, third_line,
                       fonts_get_system_font(FF_FONT_MENU_SUB),
                       GRect(6, FF_H_MENU_TITLE + 4 + FF_H_MENU_SUB, bounds.size.w - 12, FF_H_MENU_SUB),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void ff_menu_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  (void)menu_layer;
  const FfMenuCtx *ctx_data = data;
  if (!ctx_data || cell_index->row >= ctx_data->count) {
    return;
  }
  int row = ctx_data->map ? ctx_data->map[cell_index->row] : (int)cell_index->row;
  const SimpleMenuItem *item = &ctx_data->items[row];
  if (item->callback) {
    /* Pass the *backing* item index so callbacks that use it (e.g. the debug
     * menu switch) see the real item, not the visible-row position. For NULL-map
     * menus the two are identical. */
    item->callback(row, NULL);
  }
}

/* Common callback table wired into every FfMenuCtx-driven MenuLayer. */
static MenuLayerCallbacks ff_menu_callbacks(void) {
  return (MenuLayerCallbacks) {
    .get_num_sections = ff_menu_get_num_sections,
    .get_num_rows = ff_menu_get_num_rows,
    .get_header_height = ff_menu_get_header_height,
    .get_cell_height = ff_menu_get_cell_height,
    .draw_header = ff_menu_draw_header,
    .draw_row = ff_menu_draw_row,
    .select_click = ff_menu_select_click
  };
}

/* Build a MenuLayer driven by an FfMenuCtx, with the high-contrast colours
 * the SimpleMenuLayer used before. */
static MenuLayer *create_ff_menu_layer(Window *window, GRect bounds, FfMenuCtx *ctx) {
  MenuLayer *menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(menu, ctx, ff_menu_callbacks());
  menu_layer_set_click_config_onto_window(menu, window);
  menu_layer_set_normal_colors(menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(menu, GColorBlack, GColorWhite);
  return menu;
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

static void show_already_running_notice(void) {
  show_placeholder_window("FAST RUNNING",
                          "Stop the current fast before starting a new one.",
                          "BACK Menu");
}

/* Shared tail of every preset entry: drop the preset list and show the timer. */
static void enter_running_timer_from_presets(void) {
  if (window_stack_contains_window(s_presets_window)) {
    window_stack_remove(s_presets_window, false);
  }
  safe_push_window(s_timer_window, true);
  refresh_all_ui_state();
}

static void start_fast_from_preset(uint16_t target_minutes) {
  if (fast_is_running()) {
    show_already_running_notice();
    return;
  }

  global_target_minutes = target_minutes;
  if (!fast_start(target_minutes)) {
    show_already_running_notice();
    return;
  }

  enter_running_timer_from_presets();
}

/* Open-ended fast: counts up with no goal and no alarm. The saved default
 * target is deliberately left alone, so the next timed start still uses it. */
static void start_open_ended_fast(void) {
  if (fast_is_running()) {
    show_already_running_notice();
    return;
  }

  if (!fast_start_open_ended()) {
    show_already_running_notice();
    return;
  }

  enter_running_timer_from_presets();
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
  /* No section header — info is shown in the fixed title layer above the menu. */
  return 0;
}

static int16_t history_menu_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  (void)menu_layer;
  (void)cell_index;
  (void)data;
  /* One GOTHIC_24_BOLD title line plus one GOTHIC_18 subtitle line. */
  return FF_H_MENU_TITLE + FF_H_MENU_SUB + 12;
}

static void history_menu_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  /* Section header is unused — all header content lives in s_history_title_layer. */
  (void)ctx;
  (void)cell_layer;
  (void)section_index;
  (void)data;
}

/* Update the fixed title bar with current history count. Streak counts live
 * on the statistics screen so the title can use the largest font that fits the
 * round display. */
static void refresh_history_title(void) {
  if (!s_history_title_layer) return;
  snprintf(s_history_title_text, sizeof(s_history_title_text),
           "HISTORY (%d)",
           history_count);
  text_layer_set_text(s_history_title_layer, s_history_title_text);
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
                       fonts_get_system_font(FF_FONT_MENU_TITLE),
                       GRect(6, 2, bounds.size.w - 12, FF_H_MENU_TITLE),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return;
  }
  char title[24];
  char subtitle[96];
  format_history_row(row, title, sizeof(title), subtitle, sizeof(subtitle));

  /* Coloured left-edge bar encodes the fast's deepest fasting stage so the
   * history list can be scanned by metabolic state. Only stages >= 1 get a
   * bar; a sub-Fat-Burn fast has none. */
  int history_index = history_index_for_row(row);
  if (history_index >= 0 && history_index < history_count) {
    uint8_t stage = history[history_index].max_stage_reached;
    if (stage >= 1) {
      GColor bar = stage_color_for_level(stage);
      graphics_context_set_fill_color(ctx, bar);
      graphics_fill_rect(ctx, GRect(0, 0, 4, bounds.size.h), 0, GCornerNone);
      graphics_context_set_text_color(ctx, foreground);
    }
  }

  graphics_draw_text(ctx, title,
                     fonts_get_system_font(FF_FONT_MENU_TITLE),
                     GRect(6, 2, bounds.size.w - 12, FF_H_MENU_TITLE),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, subtitle,
                     fonts_get_system_font(FF_FONT_MENU_SUB),
                     GRect(6, FF_H_MENU_TITLE + 4, bounds.size.w - 12, FF_H_MENU_SUB),
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
    text_layer_set_text_color(s_history_edit_stage_layer, GColorBlack);
    text_layer_set_text(s_history_edit_hint_layer, "BACK");
    return;
  }

  char start_text[24];
  char end_text[24];
  char duration_text[20];
  char note_text[40];
  uint8_t stage_level = stage_level_for_elapsed(entry_duration_seconds(&s_history_edit_draft));
  const char *badge_label = milestone_badge_label_for_level(stage_level);
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
  snprintf(s_history_edit_hint_text, sizeof(s_history_edit_hint_text), "UP/DN adj SEL field\nHOLD save  DN-hold del");

  text_layer_set_text(s_history_edit_title_layer, s_history_edit_title_text);
  text_layer_set_text(s_history_edit_start_layer, s_history_edit_start_text);
  text_layer_set_text(s_history_edit_end_layer, s_history_edit_end_text);
  text_layer_set_text(s_history_edit_duration_layer, s_history_edit_duration_text);
  text_layer_set_text(s_history_edit_stage_layer, s_history_edit_stage_text);
  text_layer_set_text_color(s_history_edit_stage_layer, stage_color_for_level(stage_level));
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
  /* Explicit handler prevents the system from popping on press, allowing the
   * 700 ms long-click delete handler below to receive the button event. */
  window_stack_remove(s_history_edit_window, true);
}

/* Long-press BACK (700 ms) deletes the entry; plain BACK discards edits. */
static void history_edit_delete_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  /* Instead of removing the entry outright, push a confirmation window so the
   * user can verify which fast they are deleting. The actual deletion happens
   * in delete_confirm_delete_handler() once the user confirms. */
  show_delete_confirmation();
}

/* Delete-confirmation window click handlers (s31). SEL/UP confirms the
 * deletion; DOWN/BACK cancels and returns to the edit screen. */
static void delete_confirm_delete_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  bool deleted = false;
  if (s_history_edit_index >= 0 && s_history_edit_index < history_count) {
    deleted = history_delete_entry(s_history_edit_index);
  }
  /* Pop the edit window first while this confirm window still sits on top of
   * it, so the edit window's .appear callback never re-renders a now-gone entry
   * during the transition. Then remove this confirm window to land back on the
   * history list. */
  if (window_stack_contains_window(s_history_edit_window)) {
    window_stack_remove(s_history_edit_window, false);
  }
  s_history_edit_index = -1;
  s_history_edit_dirty = false;
  window_stack_remove(s_delete_confirm_window, true);
  if (deleted) {
    refresh_timer_view();
    refresh_stats_window_content();
  }
}

static void delete_confirm_cancel_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_delete_confirm_window, true);
}

static void delete_confirm_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, delete_confirm_delete_handler);
  window_single_click_subscribe(BUTTON_ID_UP, delete_confirm_delete_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, delete_confirm_cancel_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, delete_confirm_cancel_handler);
}

static void show_delete_confirmation(void) {
  if (s_history_edit_index < 0 || s_history_edit_index >= history_count) {
    return;
  }
  FastEntry *entry = &history[s_history_edit_index];
  char duration_text[20];
  char end_text[24];
  format_duration_hours_minutes(entry_duration_seconds(entry), duration_text,
                                 sizeof(duration_text));
  format_entry_datetime(entry->end_time, end_text, sizeof(end_text));
  snprintf(s_delete_confirm_body_text, sizeof(s_delete_confirm_body_text),
           "%s ending %s", duration_text, end_text);
  if (s_delete_confirm_body_layer) {
    text_layer_set_text(s_delete_confirm_body_layer, s_delete_confirm_body_text);
  }
  safe_push_window(s_delete_confirm_window, true);
}

static void history_edit_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, history_edit_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, history_edit_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, history_edit_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, history_edit_back_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, history_edit_save_click_handler, NULL);
  /* Long-press BACK does NOT work in Pebble SDK — the OS intercepts BACK before
   * the long-click threshold fires.  Use long-press DOWN for delete instead. */
  window_long_click_subscribe(BUTTON_ID_DOWN, 700, history_edit_delete_click_handler, NULL);
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
  if (elapsed < 60) {
    /* Show seconds when elapsed is sub-minute so the user can see DOWN/UP
     * adjustments that would otherwise be invisible at "0h 00m" resolution. */
    snprintf(elapsed_text, sizeof(elapsed_text), "%ds", (int)elapsed);
  } else {
    format_duration_hours_minutes(elapsed, elapsed_text, sizeof(elapsed_text));
  }

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

/* Undo an accidental stop: restore the last history entry as the running fast. */
static void menu_resume_last_callback(int index, void *context) {
  (void)index;
  (void)context;
  if (!fast_resume_last()) {
    show_placeholder_window("CANNOT RESUME",
                            "No previous fast to resume, or a fast is already running.",
                            "BACK Menu");
    return;
  }
  safe_push_window(s_timer_window, true);
  refresh_all_ui_state();
}

static void menu_current_timer_callback(int index, void *context) {
  (void)index;
  (void)context;
  safe_push_window(s_timer_window, true);
}

static void menu_stop_current_callback(int index, void *context) {
  (void)index;
  (void)context;
  if (!fast_is_running()) {
    show_placeholder_window("NOT RUNNING", "There is no active fast to stop.", "BACK Menu");
    return;
  }
  /* Ask whether to save to history or discard before stopping. */
  show_stop_confirmation();
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

/* Show author credit and source code location in the shared detail window. */
static void menu_about_callback(int index, void *context) {
  (void)index;
  (void)context;
  /* Dedicated About window (not the shared placeholder) so the body can use a
   * smaller font: the source URL then fits as a clean host/path split instead
   * of wrapping into a broken three-line mess at the large body font. */
  safe_push_window(s_about_window, true);
}

/* Start a fast whose alarm fires after 10 seconds, used for quick dev/test
 * runs of the goal-reached flow.  The stored target_minutes stays 1 (the
 * SDK minimum) so the data model remains consistent; only the live alarm
 * timer is shortened via fastforge_reschedule_alarm_for_seconds(). */
static void start_fast_from_test_preset(void) {
  if (fast_is_running()) {
    show_already_running_notice();
    return;
  }

  global_target_minutes = 1;
  if (!fast_start(1)) {
    show_already_running_notice();
    return;
  }

  /* Shorten the alarm to 10 s (fast_start registered a 60 s one). */
  fastforge_reschedule_alarm_for_seconds(10);

  enter_running_timer_from_presets();
}

static void preset_10s_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_fast_from_test_preset();
}

static void preset_open_callback(int index, void *context) {
  (void)index;
  (void)context;
  start_open_ended_fast();
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
    /* Ask before stopping: save to history or discard. */
    show_stop_confirmation();
  } else {
    fast_start(0);
    refresh_all_ui_state();
  }
}

/* UP/DOWN switch between the two timer sub-screens (hero countdown page and
 * the elapsed/stage page). BACK returns to the menu. */
static void timer_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (s_timer_page == 0) {
    s_timer_page = TIMER_PAGE_COUNT - 1;
  } else {
    s_timer_page--;
  }
  refresh_timer_page();
}

static void timer_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  s_timer_page = (uint8_t)((s_timer_page + 1) % TIMER_PAGE_COUNT);
  refresh_timer_page();
}

/* Long-press SELECT opens the running-fast editor (was on UP before the
 * buttons became page navigation). */
static void timer_select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (!fast_is_running()) {
    return;
  }
  safe_push_window(s_running_edit_window, true);
  refresh_running_edit_window_content();
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
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, timer_select_long_click_handler, NULL);
#ifdef DEBUG
  window_long_click_subscribe(BUTTON_ID_DOWN, 700, timer_debug_long_click_handler, NULL);
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

/* Stop-confirmation window (yz0): instead of silently stopping+saving, ask
 * the user whether to save the fast to history or discard it. Shown when the
 * user stops a running fast (timer SELECT, or the menu "Stop Current Fast").
 * The body shows the elapsed duration so the choice is informed. */

static void stop_confirm_save_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  /* fast_stop honours the configured minimum: a sub-minimum fast is discarded
   * even if the user picks Save. */
  fast_stop();
  window_stack_remove(s_stop_confirm_window, true);
  refresh_all_ui_state();
}

static void stop_confirm_discard_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  fast_cancel();
  window_stack_remove(s_stop_confirm_window, true);
  refresh_all_ui_state();
}

static void stop_confirm_back_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  /* Cancel the stop: return to the running fast, change nothing. */
  window_stack_remove(s_stop_confirm_window, true);
}

static void stop_confirm_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, stop_confirm_save_handler);
  window_single_click_subscribe(BUTTON_ID_UP, stop_confirm_save_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, stop_confirm_discard_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, stop_confirm_back_handler);
}

static void settings_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  settings_adjust_field(+1);
}

static void settings_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  settings_adjust_field(-1);
}

/* Short SELECT cycles between the adjustable fields (target / min fast). In
 * DEBUG, a long SELECT toggles developer mode instead. */
static void settings_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  settings_cycle_field();
}

#ifdef DEBUG
static void settings_select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
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
  window_single_click_subscribe(BUTTON_ID_SELECT, settings_select_click_handler);
#ifdef DEBUG
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, settings_select_long_click_handler, NULL);
#endif
  window_single_click_subscribe(BUTTON_ID_BACK, settings_back_click_handler);
}

static void stats_dismiss_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_stats_window, true);
}

/* UP/DOWN page through the per-stat sub-screens (wrapping, like the timer).
 * SELECT/BACK dismiss back to the menu. */
static void stats_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (s_stats_page == 0) {
    s_stats_page = s_stats_page_count - 1;
  } else {
    s_stats_page--;
  }
  refresh_stats_window_content();
}

static void stats_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  s_stats_page = (uint8_t)((s_stats_page + 1) % s_stats_page_count);
  refresh_stats_window_content();
}

static void detail_dismiss_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_detail_window, true);
}

/* Statistics screen: UP/DOWN page through the large-font per-stat
 * sub-screens; SELECT/BACK dismiss. */
static void stats_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, stats_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, stats_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, stats_dismiss_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, stats_dismiss_handler);
}

static void detail_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, scroll_layer_scroll_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, scroll_layer_scroll_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, detail_dismiss_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, detail_dismiss_handler);
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
  ContentRect cr = content_rect(bounds);
  GRect menu_bounds = GRect(cr.ox, cr.oy, cr.cw, bounds.size.h - 2 * cr.oy);
  s_main_menu_ctx = (FfMenuCtx){ s_main_menu_items, MAIN_MENU_ITEM_COUNT,
                                  "FastForge", s_main_menu_map, s_main_menu_accent_colors, NULL };
  s_main_menu_layer = create_ff_menu_layer(window, menu_bounds, &s_main_menu_ctx);
  layer_add_child(window_layer, menu_layer_get_layer(s_main_menu_layer));
  sync_main_menu_state();
}

static void menu_window_unload(Window *window) {
  (void)window;
  menu_layer_destroy(s_main_menu_layer);
  s_main_menu_layer = NULL;
  save_all_data();
}

static void timer_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;

  /* The timer screen is split into two sub-screens because the hero countdown,
   * goal clock, progress bar, elapsed time, stage, and hint do not all fit at
   * the largest fonts on the round display. UP/DOWN toggles pages.
   *
   * Page 0: title, hero countdown, goal clock, progress bar.
   * Page 1: hero elapsed time, stage. The hint line is shared. */
  const int16_t title_y = oy + 2;
  const int16_t hero_y  = title_y + FF_H_SCREEN_TITLE + 4;
  const int16_t eta_y   = hero_y + FF_H_HERO + 6;
  const int16_t prog_y  = eta_y + FF_H_BIG + 6;
  const int16_t hint_y  = bounds.size.h - oy - FF_H_HINT; /* one-line hint at the very bottom */

  /* The hero row sits near the vertical centre where a round display is at its
   * widest, so it can use a smaller horizontal inset than the other rows and
   * still fit BITHAM_42_BOLD. */
#ifdef PBL_ROUND
  const int16_t hero_inset = bounds.size.w / 12;
  const int16_t eta_inset  = bounds.size.w / 10;
#else
  const int16_t hero_inset = 0;
  const int16_t eta_inset  = ox;
#endif
  const int16_t hero_w = bounds.size.w - 2 * hero_inset;

  s_title_layer = create_text_layer(GRect(ox, title_y, cw, FF_H_SCREEN_TITLE),
                                    GTextAlignmentCenter,
                                    FF_FONT_SCREEN_TITLE,
                                    GColorBlack, GColorClear, false);
  s_timer_layer = create_text_layer(GRect(hero_inset, hero_y, hero_w, FF_H_HERO),
                                    GTextAlignmentCenter,
                                    FF_FONT_HERO,
                                    GColorBlack, GColorClear, false);
#if FASTFORGE_SHOW_GOAL_CLOCK
  /* Goal wall-clock finish time, e.g. "Ends Thu 03:44" / "Goal Sun 12:30 PM".
   * GOTHIC_28_BOLD is the largest font that still fits the longest 12-hour
   * cross-day variant inside the round face's reduced eta inset. */
  s_eta_layer = create_text_layer(GRect(eta_inset, eta_y,
                                        bounds.size.w - 2 * eta_inset, FF_H_BIG),
                                  GTextAlignmentCenter,
                                  FF_FONT_BIG,
                                  GColorBlack, GColorClear, false);
#endif
  /* Page 1 hero counter: same hero font/size/position as the page-0 hero so
   * the big number stays stationary when the user pages up/down. The layer is
   * hidden on page 0, so it never visually overlaps the page-0 hero. */
  s_detail_layer = create_text_layer(GRect(hero_inset, hero_y, hero_w, FF_H_HERO),
                                     GTextAlignmentCenter,
                                     FF_FONT_HERO,
                                     GColorBlack, GColorClear, false);
  s_progress_layer = layer_create(GRect(ox + 8, prog_y, cw - 16, 14));
  layer_set_update_proc(s_progress_layer, timer_progress_update_proc);
  s_stage_layer = create_text_layer(GRect(ox, hero_y + FF_H_HERO + 6, cw, FF_H_BODY),
                                    GTextAlignmentCenter,
                                    FF_FONT_BODY_BOLD,
                                    GColorBlack, GColorClear, false);
  s_hint_layer = create_text_layer(GRect(ox, hint_y, cw, FF_H_HINT),
                                   GTextAlignmentCenter,
                                   FF_FONT_HINT,
                                   GColorBlack, GColorClear, true);

  add_text_layer(window_layer, s_title_layer);
  add_text_layer(window_layer, s_timer_layer);
#if FASTFORGE_SHOW_GOAL_CLOCK
  add_text_layer(window_layer, s_eta_layer);
#endif
  add_text_layer(window_layer, s_detail_layer);
  layer_add_child(window_layer, s_progress_layer);
  add_text_layer(window_layer, s_stage_layer);
  add_text_layer(window_layer, s_hint_layer);

  /* Page indicator gets its own 20 px strip directly above the one-line
   * hint, well clear of the progress bar (which sits higher up, on page 0). */
  s_timer_indicator_layer = layer_create(GRect(ox, hint_y - 22, cw, 20));
  layer_set_update_proc(s_timer_indicator_layer, timer_indicator_update_proc);
  layer_add_child(window_layer, s_timer_indicator_layer);

  s_timer_page = 0;
  refresh_timer_view();
}

static void timer_window_unload(Window *window) {
  (void)window;
  save_all_data();
  layer_destroy(s_progress_layer);
  s_progress_layer = NULL;
  layer_destroy(s_timer_indicator_layer);
  s_timer_indicator_layer = NULL;
  text_layer_destroy(s_title_layer);
  s_title_layer = NULL;
  text_layer_destroy(s_timer_layer);
  s_timer_layer = NULL;
#if FASTFORGE_SHOW_GOAL_CLOCK
  text_layer_destroy(s_eta_layer);
  s_eta_layer = NULL;
#endif
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
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;

  /* Background fills the entire display (including rounded corners). */
  s_goal_background_layer = layer_create(bounds);
  layer_set_update_proc(s_goal_background_layer, goal_background_update_proc);
  layer_add_child(window_layer, s_goal_background_layer);

  s_goal_title_layer = create_text_layer(GRect(ox, oy + 24, cw, FF_H_SCREEN_TITLE),
                                         GTextAlignmentCenter,
                                         FF_FONT_SCREEN_TITLE,
                                         theme_goal_text_color(), theme_goal_background_color(), false);
  text_layer_set_text(s_goal_title_layer, "GOAL HIT");

  /* Big elapsed-time hero, same font as the countdown screen. */
#ifdef PBL_ROUND
  const int16_t hero_inset = bounds.size.w / 12;
#else
  const int16_t hero_inset = 0;
#endif
  s_goal_time_layer = create_text_layer(GRect(hero_inset, oy + 64,
                                              bounds.size.w - 2 * hero_inset, FF_H_HERO),
                                        GTextAlignmentCenter,
                                        FF_FONT_HERO,
                                        theme_goal_text_color(), theme_goal_background_color(), false);
  text_layer_set_text(s_goal_time_layer, "00:00:00");

  s_goal_stage_layer = create_text_layer(GRect(ox, oy + 118, cw, FF_H_BODY),
                                         GTextAlignmentCenter,
                                         FF_FONT_BODY_BOLD,
                                         theme_goal_text_color(), theme_goal_background_color(), false);
  text_layer_set_text(s_goal_stage_layer, "Stage: --");

  s_goal_hint_layer = create_text_layer(GRect(ox, bounds.size.h - oy - (FF_H_HINT * 2) - 2,
                                              cw, FF_H_HINT * 2),
                                        GTextAlignmentCenter,
                                        FF_FONT_HINT,
                                        theme_goal_text_color(), theme_goal_background_color(), true);
  /* Fast continues automatically; any key dismisses this overlay. */
  text_layer_set_text(s_goal_hint_layer, "DN dismiss   SEL stop");

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

static void stop_confirm_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;
  int16_t title_y = oy + 2;
  int16_t hint_y = bounds.size.h - oy - FF_H_HINT;
  int16_t caption_y = title_y + FF_H_SCREEN_TITLE + 8;
  int16_t time_y = caption_y + FF_H_SUB_BOLD + 4;
  int16_t question_y = time_y + FF_H_HERO + 8;

  /* The hero time uses the same round inset as the timer screen so
   * BITHAM_42_BOLD "00:00:00" fits inside the round face. On rectangular
   * platforms the timer screen uses a zero hero inset, so match that. */
#ifdef PBL_ROUND
  const int16_t hero_inset = bounds.size.w / 12;
#else
  const int16_t hero_inset = 0;
#endif
  const int16_t hero_w = bounds.size.w - 2 * hero_inset;

  window_set_click_config_provider(window, stop_confirm_click_config_provider);

  s_stop_confirm_title_layer = create_text_layer(GRect(ox, title_y, cw, FF_H_SCREEN_TITLE),
                                                  GTextAlignmentCenter, FF_FONT_SCREEN_TITLE,
                                                  GColorBlack, GColorClear, false);
  text_layer_set_text(s_stop_confirm_title_layer, "STOP FAST?");

  s_stop_confirm_caption_layer = create_text_layer(GRect(ox, caption_y, cw, FF_H_SUB_BOLD),
                                                    GTextAlignmentCenter, FF_FONT_SUB_BOLD,
                                                    GColorBlack, GColorClear, false);
  text_layer_set_text(s_stop_confirm_caption_layer, "Fasted");

  s_stop_confirm_time_layer = create_text_layer(GRect(hero_inset, time_y, hero_w, FF_H_HERO),
                                                 GTextAlignmentCenter, FF_FONT_HERO,
                                                 GColorBlack, GColorClear, false);
  text_layer_set_text(s_stop_confirm_time_layer, s_stop_confirm_time_text);

  s_stop_confirm_body_layer = create_text_layer(GRect(ox, question_y, cw, hint_y - question_y - 4),
                                                 GTextAlignmentCenter, FF_FONT_SUB,
                                                 GColorBlack, GColorClear, true);
  text_layer_set_text(s_stop_confirm_body_layer, "Save to history?");

  s_stop_confirm_hint_layer = create_text_layer(GRect(ox, hint_y, cw, FF_H_HINT),
                                                  GTextAlignmentCenter, FF_FONT_HINT,
                                                  GColorBlack, GColorClear, false);
  text_layer_set_text(s_stop_confirm_hint_layer, "SEL save  DN discard");

  layer_add_child(window_layer, text_layer_get_layer(s_stop_confirm_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_stop_confirm_caption_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_stop_confirm_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_stop_confirm_body_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_stop_confirm_hint_layer));
}

static void stop_confirm_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_stop_confirm_title_layer);
  s_stop_confirm_title_layer = NULL;
  text_layer_destroy(s_stop_confirm_caption_layer);
  s_stop_confirm_caption_layer = NULL;
  text_layer_destroy(s_stop_confirm_time_layer);
  s_stop_confirm_time_layer = NULL;
  text_layer_destroy(s_stop_confirm_body_layer);
  s_stop_confirm_body_layer = NULL;
  text_layer_destroy(s_stop_confirm_hint_layer);
  s_stop_confirm_hint_layer = NULL;
}

static void show_stop_confirmation(void) {
  if (!fast_is_running()) {
    return;
  }
  time_t elapsed = fastforge_now() - current_fast.start_time;
  if (elapsed < 0) {
    elapsed = 0;
  }
  format_hhmmss(elapsed, s_stop_confirm_time_text, sizeof(s_stop_confirm_time_text));
  if (s_stop_confirm_time_layer) {
    text_layer_set_text(s_stop_confirm_time_layer, s_stop_confirm_time_text);
  }
  safe_push_window(s_stop_confirm_window, true);
}

/* Delete-confirmation window (s31). Title + a wrapped body describing the
 * fast being removed + a one-line control hint, mirroring stop_confirm but
 * without the hero time (the body already carries the duration). */
static void delete_confirm_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;
  int16_t title_y = oy + 2;
  int16_t hint_y = bounds.size.h - oy - FF_H_HINT;
  int16_t body_y = title_y + FF_H_SCREEN_TITLE + 12;

  window_set_click_config_provider(window, delete_confirm_click_config_provider);

  s_delete_confirm_title_layer = create_text_layer(GRect(ox, title_y, cw, FF_H_SCREEN_TITLE),
                                                     GTextAlignmentCenter, FF_FONT_SCREEN_TITLE,
                                                     GColorBlack, GColorClear, false);
  text_layer_set_text(s_delete_confirm_title_layer, "DELETE FAST?");

  s_delete_confirm_body_layer = create_text_layer(GRect(ox, body_y, cw, hint_y - body_y - 4),
                                                   GTextAlignmentCenter, FF_FONT_SUB,
                                                   GColorBlack, GColorClear, true);
  text_layer_set_text(s_delete_confirm_body_layer, s_delete_confirm_body_text);

  s_delete_confirm_hint_layer = create_text_layer(GRect(ox, hint_y, cw, FF_H_HINT),
                                                   GTextAlignmentCenter, FF_FONT_HINT,
                                                   GColorBlack, GColorClear, false);
  text_layer_set_text(s_delete_confirm_hint_layer, "SEL delete  DN cancel");

  layer_add_child(window_layer, text_layer_get_layer(s_delete_confirm_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_delete_confirm_body_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_delete_confirm_hint_layer));
}

static void delete_confirm_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_delete_confirm_title_layer);
  s_delete_confirm_title_layer = NULL;
  text_layer_destroy(s_delete_confirm_body_layer);
  s_delete_confirm_body_layer = NULL;
  text_layer_destroy(s_delete_confirm_hint_layer);
  s_delete_confirm_hint_layer = NULL;
}

/* Projects each preset's target duration onto the current wall-clock time, as
 * if the fast were started right now, so the list can be scanned for "which
 * of these actually finishes at a sane hour". Recomputed on every appearance
 * since it is relative to "now"; presets with no fixed duration (Open ended,
 * dev 10s) are left blank. */
static void refresh_presets_menu_end_times(void) {
#if FASTFORGE_SHOW_GOAL_CLOCK
  time_t now = fastforge_now();
  for (int i = 0; i < PRESET_MENU_ITEM_COUNT; i++) {
    if (s_preset_target_hours[i] == 0) {
      s_presets_menu_end_time_lines[i][0] = '\0';
      continue;
    }
    time_t end_time = now + (time_t)s_preset_target_hours[i] * 3600;
    char clock_text[20];
    format_clock_time(end_time, now, clock_text, sizeof(clock_text));
    snprintf(s_presets_menu_end_time_lines[i], sizeof(s_presets_menu_end_time_lines[i]),
             "Ends %s", clock_text);
  }
  if (s_presets_menu_layer) {
    menu_layer_reload_data(s_presets_menu_layer);
  }
#endif
}

static void presets_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  ContentRect cr = content_rect(bounds);
  GRect menu_bounds = GRect(cr.ox, cr.oy, cr.cw, bounds.size.h - 2 * cr.oy);
  s_presets_menu_ctx = (FfMenuCtx){ s_presets_menu_items, PRESET_MENU_ITEM_COUNT,
                                    "Start New Fast", NULL, s_presets_menu_accent_colors,
                                    s_presets_menu_end_time_ptrs };
  s_presets_menu_layer = create_ff_menu_layer(window, menu_bounds, &s_presets_menu_ctx);
  layer_add_child(window_layer, menu_layer_get_layer(s_presets_menu_layer));
}

static void presets_window_appear(Window *window) {
  (void)window;
  refresh_presets_menu_end_times();
}

static void presets_window_unload(Window *window) {
  (void)window;
  menu_layer_destroy(s_presets_menu_layer);
  s_presets_menu_layer = NULL;
}

static void history_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;

  /* Fixed title bar: "HISTORY (N)". Streak counts live on the statistics
   * screen so this title can use the largest font that fits the round face. */
  int16_t title_h = FF_H_SCREEN_TITLE;
  s_history_title_layer = create_text_layer(GRect(ox, oy + 2, cw, title_h),
                                            GTextAlignmentCenter,
                                            FF_FONT_SCREEN_TITLE,
                                            GColorBlack, GColorClear, false);
  add_text_layer(window_layer, s_history_title_layer);
  refresh_history_title();

  /* Menu starts below the title; leave symmetrical vertical inset at the bottom. */
  int16_t title_offset = oy + 2 + title_h + 2;
  GRect menu_bounds = GRect(ox, title_offset, cw, bounds.size.h - title_offset - oy);

  s_history_menu_layer = menu_layer_create(menu_bounds);
  menu_layer_set_normal_colors(s_history_menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_history_menu_layer, GColorBlack, GColorWhite);
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
  text_layer_destroy(s_history_title_layer);
  s_history_title_layer = NULL;
  menu_layer_destroy(s_history_menu_layer);
  s_history_menu_layer = NULL;
}

static void history_window_appear(Window *window) {
  (void)window;
  refresh_history_title();
  history_menu_reload();
}

static void history_edit_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;

  /* Six rows sized to fit the round face at the largest fonts: a bold title,
   * three editable fields (Start/End/Note) at GOTHIC_24_BOLD, then the badge
   * and the controls hint at GOTHIC_18. */
  const int16_t h_title = FF_H_BODY_BOLD;
  const int16_t h_field = FF_H_BODY_BOLD;
  const int16_t h_badge = FF_H_SUB_BOLD;
  const int16_t h_hint  = FF_H_SUB * 2;
  int16_t y = oy + 2;

  s_history_edit_title_layer = create_text_layer(GRect(ox, y, cw, h_title),
                                                 GTextAlignmentCenter,
                                                 FF_FONT_BODY_BOLD,
                                                 GColorBlack, GColorClear, false);
  y += h_title + 4;
  s_history_edit_start_layer = create_text_layer(GRect(ox, y, cw, h_field),
                                                 GTextAlignmentLeft,
                                                 FF_FONT_BODY_BOLD,
                                                 GColorBlack, GColorClear, false);
  y += h_field + 2;
  s_history_edit_end_layer = create_text_layer(GRect(ox, y, cw, h_field),
                                               GTextAlignmentLeft,
                                               FF_FONT_BODY_BOLD,
                                               GColorBlack, GColorClear, false);
  y += h_field + 2;
  s_history_edit_duration_layer = create_text_layer(GRect(ox, y, cw, h_field),
                                                    GTextAlignmentLeft,
                                                    FF_FONT_BODY_BOLD,
                                                    GColorBlack, GColorClear, false);
  y += h_field + 4;
  s_history_edit_stage_layer = create_text_layer(GRect(ox, y, cw, h_badge),
                                                 GTextAlignmentLeft,
                                                 FF_FONT_SUB_BOLD,
                                                 GColorBlack, GColorClear, false);
  y += h_badge + 4;
  s_history_edit_hint_layer = create_text_layer(GRect(ox, y, cw, h_hint),
                                                GTextAlignmentCenter,
                                                FF_FONT_SUB,
                                                GColorBlack, GColorClear, true);

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
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;

  /* Title + three info rows at GOTHIC_24_BOLD, controls hint at GOTHIC_18. */
  const int16_t h_title = FF_H_BODY_BOLD;
  const int16_t h_row   = FF_H_BODY_BOLD;
  const int16_t h_hint  = FF_H_SUB * 2;
  int16_t y = oy + 2;

  s_running_edit_title_layer = create_text_layer(GRect(ox, y, cw, h_title),
                                                 GTextAlignmentCenter,
                                                 FF_FONT_BODY_BOLD,
                                                 GColorBlack, GColorClear, false);
  y += h_title + 6;
  s_running_edit_start_layer = create_text_layer(GRect(ox, y, cw, h_row),
                                                 GTextAlignmentLeft,
                                                 FF_FONT_BODY_BOLD,
                                                 GColorBlack, GColorClear, false);
  y += h_row + 4;
  s_running_edit_elapsed_layer = create_text_layer(GRect(ox, y, cw, h_row),
                                                    GTextAlignmentLeft,
                                                    FF_FONT_BODY_BOLD,
                                                    GColorBlack, GColorClear, false);
  y += h_row + 4;
  s_running_edit_goal_layer = create_text_layer(GRect(ox, y, cw, h_row),
                                                GTextAlignmentLeft,
                                                FF_FONT_BODY_BOLD,
                                                GColorBlack, GColorClear, false);
  y += h_row + 6;
  s_running_edit_hint_layer = create_text_layer(GRect(ox, y, cw, h_hint),
                                                GTextAlignmentCenter,
                                                FF_FONT_SUB,
                                                GColorBlack, GColorClear, true);

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

/* Lay out the stacked title/body text layers inside the detail/about
 * ScrollLayer (configure_scroll_indicator is used by the detail screen). */
/* Enable the ScrollLayer's built-in up/down content arrows so the user can see
 * when there is more content to scroll to with UP/DOWN. */
static void configure_scroll_indicator(ScrollLayer *scroll, GColor background) {
  if (!scroll) return;
  ContentIndicator *ci = scroll_layer_get_content_indicator(scroll);
  if (!ci) return;
  ContentIndicatorConfig config = (ContentIndicatorConfig){
    .layer = scroll_layer_get_layer(scroll),
    .times_out = false,
    .alignment = GAlignTop,
    .colors = { .foreground = GColorBlack, .background = background }
  };
  content_indicator_configure_direction(ci, ContentIndicatorDirectionUp, &config);
  config.alignment = GAlignBottom;
  content_indicator_configure_direction(ci, ContentIndicatorDirectionDown, &config);
}

/* Page indicator for the statistics sub-screens: up/down chevrons (active
 * when a previous/next page exists) and an "n/N" counter. Mirrors the timer
 * indicator; active chevrons are black on the light stats surface. */
static void stats_indicator_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int16_t cx = bounds.size.w / 2;
  int16_t cy = bounds.size.h / 2;
  GColor active = GColorBlack;
  GColor inactive = GColorLightGray;
  bool up = (s_stats_page > 0);
  bool down = (s_stats_page < s_stats_page_count - 1);
  timer_chevron(ctx, cx - 30, cy, true,  up   ? active : inactive);
  timer_chevron(ctx, cx + 30, cy, false, down ? active : inactive);
  char label[8];
  snprintf(label, sizeof(label), "%u/%u", (unsigned)(s_stats_page + 1),
           (unsigned)s_stats_page_count);
  graphics_context_set_text_color(ctx, active);
  graphics_draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(cx - 20, cy - 8, 40, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

/* Build the label / large value / secondary line for the current stats page.
 * Pages: AVERAGE, TOTAL, SUCCESS, LONGEST, STREAK. When no fast has been
 * completed yet, a single placeholder page is shown instead. */
void refresh_stats_window_content(void) {
  if (!s_stats_value_layer) {
    return;
  }

  time_t total_seconds = 0;
  time_t longest_seconds = 0;
  int completed_count = 0;
  int successful_count = 0;
  collect_stats_summary(&total_seconds, &longest_seconds, &completed_count,
                        &successful_count);

  s_stats_page_count = (completed_count == 0) ? 1 : STATS_PAGE_COUNT;
  if (s_stats_page >= s_stats_page_count) {
    s_stats_page = 0;
  }

  static char value_text[24];
  static char sub_text[32];
  const char *label;
  char dur_text[20];

  if (completed_count == 0) {
    label = "STATISTICS";
    snprintf(value_text, sizeof(value_text), "No fasts");
    snprintf(sub_text, sizeof(sub_text), "Start a fast");
  } else {
    int success_rate = (successful_count * 100 + completed_count / 2) / completed_count;
    switch (s_stats_page) {
      case 0: /* AVERAGE */
        label = "AVERAGE";
        format_duration_hours_minutes(total_seconds / completed_count,
                                       dur_text, sizeof(dur_text));
        snprintf(value_text, sizeof(value_text), "%s", dur_text);
        snprintf(sub_text, sizeof(sub_text), "over %d fasts", completed_count);
        break;
      case 1: /* TOTAL */
        label = "TOTAL";
        format_duration_hours_minutes(total_seconds, dur_text, sizeof(dur_text));
        snprintf(value_text, sizeof(value_text), "%s", dur_text);
        snprintf(sub_text, sizeof(sub_text), "%d fasts", completed_count);
        break;
      case 2: /* SUCCESS */
        label = "SUCCESS";
        snprintf(value_text, sizeof(value_text), "%d%%", success_rate);
        snprintf(sub_text, sizeof(sub_text), "%d of %d met goal",
                 successful_count, completed_count);
        break;
      case 3: /* LONGEST */
        label = "LONGEST";
        format_duration_hours_minutes(longest_seconds, dur_text, sizeof(dur_text));
        snprintf(value_text, sizeof(value_text), "%s", dur_text);
        snprintf(sub_text, sizeof(sub_text), "best single fast");
        break;
      default: /* STREAK */
        label = "STREAK";
        snprintf(value_text, sizeof(value_text), "%u / %u",
                 streak_data.current_streak, streak_data.longest_streak);
        snprintf(sub_text, sizeof(sub_text), "current / best");
        break;
    }
  }

  text_layer_set_text(s_stats_title_layer, label);
  /* Give each statistic its own accent colour so the paged stats read as a
   * colour-coded set; the empty placeholder stays neutral. */
  GColor value_color = GColorBlack;
  if (completed_count != 0 && is_color_platform()) {
    switch (s_stats_page) {
      case 0: value_color = GColorDukeBlue; break;            /* AVERAGE */
      case 1: value_color = GColorIndigo; break;              /* TOTAL   */
      case 2: value_color = GColorIslamicGreen; break;        /* SUCCESS */
      case 3: value_color = GColorDarkCandyAppleRed; break;  /* LONGEST */
      default: value_color = GColorOrange; break;             /* STREAK  */
    }
  }
  text_layer_set_text_color(s_stats_value_layer, value_color);
  text_layer_set_text(s_stats_value_layer, value_text);
  text_layer_set_text(s_stats_sub_layer, sub_text);
  if (s_stats_indicator_layer) {
    layer_set_hidden(s_stats_indicator_layer, s_stats_page_count <= 1);
    layer_mark_dirty(s_stats_indicator_layer);
  }
}

static void stats_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;
  int16_t title_y = oy + 2;
  int16_t value_y = title_y + FF_H_SCREEN_TITLE + 4;
  int16_t sub_y    = value_y + FF_H_HERO + 6;
  int16_t hint_y   = bounds.size.h - oy - FF_H_HINT;

  /* The hero value uses the same reduced horizontal inset as the timer hero so
   * BITHAM_42_BOLD fits the round face; the label/sub lines use the content
   * width. */
#ifdef PBL_ROUND
  int16_t hero_inset = bounds.size.w / 12;
#else
  int16_t hero_inset = 0;
#endif
  int16_t hero_w = bounds.size.w - 2 * hero_inset;

  window_set_click_config_provider(window, stats_click_config_provider);

  s_stats_title_layer = create_text_layer(GRect(ox, title_y, cw, FF_H_SCREEN_TITLE),
                                          GTextAlignmentCenter, FF_FONT_SCREEN_TITLE,
                                          GColorBlack, GColorClear, false);
  s_stats_value_layer = create_text_layer(GRect(hero_inset, value_y, hero_w, FF_H_HERO),
                                          GTextAlignmentCenter, FF_FONT_HERO,
                                          GColorBlack, GColorClear, false);
  s_stats_sub_layer = create_text_layer(GRect(ox, sub_y, cw, FF_H_BODY),
                                        GTextAlignmentCenter, FF_FONT_BODY_BOLD,
                                        GColorBlack, GColorClear, false);
  s_stats_hint_layer = create_text_layer(GRect(ox, hint_y, cw, FF_H_HINT),
                                         GTextAlignmentCenter, FF_FONT_HINT,
                                         GColorBlack, GColorClear, false);
  text_layer_set_text(s_stats_hint_layer, "BACK menu");

  layer_add_child(window_layer, text_layer_get_layer(s_stats_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_stats_value_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_stats_sub_layer));
  s_stats_indicator_layer = layer_create(GRect(ox, hint_y - 22, cw, 20));
  layer_set_update_proc(s_stats_indicator_layer, stats_indicator_update_proc);
  layer_add_child(window_layer, s_stats_indicator_layer);
  layer_add_child(window_layer, text_layer_get_layer(s_stats_hint_layer));

  s_stats_page = 0;
  refresh_stats_window_content();
}

static void stats_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_stats_title_layer);
  s_stats_title_layer = NULL;
  text_layer_destroy(s_stats_value_layer);
  s_stats_value_layer = NULL;
  text_layer_destroy(s_stats_sub_layer);
  s_stats_sub_layer = NULL;
  text_layer_destroy(s_stats_hint_layer);
  s_stats_hint_layer = NULL;
  layer_destroy(s_stats_indicator_layer);
  s_stats_indicator_layer = NULL;
}

static void stats_window_appear(Window *window) {
  (void)window;
  refresh_stats_window_content();
}

static void settings_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;

  s_settings_title_layer = create_text_layer(GRect(ox, oy + 20, cw, FF_H_SCREEN_TITLE),
                                             GTextAlignmentCenter,
                                             FF_FONT_SCREEN_TITLE,
                                             GColorBlack, GColorClear, false);
  text_layer_set_text(s_settings_title_layer, "SETTINGS");

  s_settings_target_layer = create_text_layer(GRect(ox, oy + 62, cw, FF_H_BODY_BOLD),
                                              GTextAlignmentCenter,
                                              FF_FONT_BODY_BOLD,
                                              GColorBlack, GColorClear, false);

  s_settings_min_layer = create_text_layer(GRect(ox, oy + 94, cw, FF_H_BODY),
                                          GTextAlignmentCenter,
                                          FF_FONT_BODY,
                                          GColorDarkGray, GColorClear, false);

  s_settings_hint_layer = create_text_layer(GRect(ox, bounds.size.h - oy - (FF_H_HINT * 2) - 4,
                                                  cw, FF_H_HINT * 2),
                                            GTextAlignmentCenter,
                                            FF_FONT_HINT,
                                            GColorBlack, GColorClear, true);

#ifdef DEBUG
  s_settings_dev_layer = create_text_layer(GRect(ox, oy + 126, cw, FF_H_SUB_BOLD),
                                           GTextAlignmentCenter,
                                           FF_FONT_SUB_BOLD,
                                           GColorBlack, GColorClear, false);
#endif

  s_settings_field = SETTINGS_FIELD_TARGET;
  add_text_layer(window_layer, s_settings_title_layer);
  add_text_layer(window_layer, s_settings_target_layer);
  add_text_layer(window_layer, s_settings_min_layer);
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
  text_layer_destroy(s_settings_min_layer);
  s_settings_min_layer = NULL;
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

/* Lay out the stacked title/body/hint inside the detail ScrollLayer and size
 * the scroll content to the wrapped body, so long About/notice text scrolls. */
void fastforge_detail_layout_refresh(void) {
  if (!s_detail_scroll_layer || !s_placeholder_title_layer ||
      !s_placeholder_body_layer || !s_placeholder_hint_layer) {
    return;
  }
  GRect frame = layer_get_frame(scroll_layer_get_layer(s_detail_scroll_layer));
  int16_t cw = frame.size.w;
  int16_t title_h = FF_H_SCREEN_TITLE;
  text_layer_set_size(s_placeholder_title_layer, GSize(cw, title_h));
  GSize body = text_layer_get_content_size(s_placeholder_body_layer);
  if (body.w > cw) body.w = cw;
  text_layer_set_size(s_placeholder_body_layer, body);
  int16_t hint_y = title_h + 8 + body.h + 12;
  layer_set_frame(text_layer_get_layer(s_placeholder_hint_layer),
                  GRect(0, hint_y, cw, FF_H_SUB));
  scroll_layer_set_content_size(s_detail_scroll_layer,
                                GSize(cw, hint_y + FF_H_SUB + 4));
}

static void detail_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;

  GRect frame = GRect(ox, oy, cw, bounds.size.h - 2 * oy);
  s_detail_scroll_layer = scroll_layer_create(frame);
  scroll_layer_set_shadow_hidden(s_detail_scroll_layer, false);
  configure_scroll_indicator(s_detail_scroll_layer, theme_surface_background_color());
  window_set_click_config_provider_with_context(window, detail_click_config_provider,
                                                 s_detail_scroll_layer);

  s_placeholder_title_layer = text_layer_create(GRect(0, 0, cw, FF_H_SCREEN_TITLE));
  text_layer_set_background_color(s_placeholder_title_layer, GColorClear);
  text_layer_set_text_color(s_placeholder_title_layer, GColorBlack);
  text_layer_set_text_alignment(s_placeholder_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_placeholder_title_layer, fonts_get_system_font(FF_FONT_SCREEN_TITLE));

  s_placeholder_body_layer = text_layer_create(GRect(0, FF_H_SCREEN_TITLE + 8, cw, 1000));
  text_layer_set_background_color(s_placeholder_body_layer, GColorClear);
  text_layer_set_text_color(s_placeholder_body_layer, GColorBlack);
  text_layer_set_text_alignment(s_placeholder_body_layer, GTextAlignmentCenter);
  text_layer_set_font(s_placeholder_body_layer, fonts_get_system_font(FF_FONT_BODY));
  text_layer_set_overflow_mode(s_placeholder_body_layer, GTextOverflowModeWordWrap);

  s_placeholder_hint_layer = text_layer_create(GRect(0, 0, cw, FF_H_SUB));
  text_layer_set_background_color(s_placeholder_hint_layer, GColorClear);
  text_layer_set_text_color(s_placeholder_hint_layer, GColorBlack);
  text_layer_set_text_alignment(s_placeholder_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_placeholder_hint_layer, fonts_get_system_font(FF_FONT_HINT));

  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_placeholder_title_layer));
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_placeholder_body_layer));
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_placeholder_hint_layer));
  layer_add_child(window_layer, scroll_layer_get_layer(s_detail_scroll_layer));

  /* Buffers were already filled by show_placeholder_window before the window
   * was pushed.  Set the layer pointers directly to avoid snprintf(buf, "%s",
   * buf) undefined-behaviour (self-copy clears the string on Pebble's libc). */
  text_layer_set_text(s_placeholder_title_layer, s_placeholder_title_text);
  text_layer_set_text(s_placeholder_body_layer, s_placeholder_body_text);
  text_layer_set_text(s_placeholder_hint_layer, s_placeholder_hint_text);
  fastforge_detail_layout_refresh();
}

static void detail_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_placeholder_title_layer);
  s_placeholder_title_layer = NULL;
  text_layer_destroy(s_placeholder_body_layer);
  s_placeholder_body_layer = NULL;
  text_layer_destroy(s_placeholder_hint_layer);
  s_placeholder_hint_layer = NULL;
  scroll_layer_destroy(s_detail_scroll_layer);
  s_detail_scroll_layer = NULL;
}

/* The About screen uses a smaller body font than the shared placeholder so the
 * source URL fits as a clean host/path split (github.com/ on one line,
 * snonux/fastforge on the next) instead of wrapping into a broken three-line
 * jumble at the large body font. Content is short enough that it never needs
 * to scroll. */
#define ABOUT_BODY_TEXT \
  "By Paul Buetow\n\nSource code:\ngithub.com/\nsnonux/fastforge"

static void about_dismiss_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  window_stack_remove(s_about_window, true);
}

static void about_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, about_dismiss_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, about_dismiss_handler);
}

static void about_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  ContentRect cr = content_rect(bounds);
  int16_t ox = cr.ox, oy = cr.oy, cw = cr.cw;
  int16_t title_y = oy + 2;
  int16_t hint_y = bounds.size.h - oy - FF_H_HINT;
  int16_t body_y = title_y + FF_H_SCREEN_TITLE + 8;

  window_set_click_config_provider(window, about_click_config_provider);

  s_about_title_layer = create_text_layer(GRect(ox, title_y, cw, FF_H_SCREEN_TITLE),
                                          GTextAlignmentCenter, FF_FONT_SCREEN_TITLE,
                                          GColorBlack, GColorClear, false);
  text_layer_set_text(s_about_title_layer, "ABOUT");

  s_about_body_layer = create_text_layer(GRect(ox, body_y, cw, hint_y - body_y - 4),
                                         GTextAlignmentCenter, FF_FONT_SUB,
                                         GColorBlack, GColorClear, true);
  text_layer_set_text(s_about_body_layer, ABOUT_BODY_TEXT);

  s_about_hint_layer = create_text_layer(GRect(ox, hint_y, cw, FF_H_HINT),
                                          GTextAlignmentCenter, FF_FONT_HINT,
                                          GColorBlack, GColorClear, false);
  text_layer_set_text(s_about_hint_layer, "BACK menu");

  layer_add_child(window_layer, text_layer_get_layer(s_about_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_about_body_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_about_hint_layer));
}

static void about_window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_about_title_layer);
  s_about_title_layer = NULL;
  text_layer_destroy(s_about_body_layer);
  s_about_body_layer = NULL;
  text_layer_destroy(s_about_hint_layer);
  s_about_hint_layer = NULL;
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
  s_main_menu_items[MAIN_MENU_INDEX_RESUME_LAST] = (SimpleMenuItem) {
    .title = "Resume Last Fast",
    .subtitle = s_menu_resume_subtitle,  /* set dynamically by sync_main_menu_state */
    .callback = menu_resume_last_callback
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

  for (int i = 0; i < MAIN_MENU_ITEM_COUNT; i++) {
    s_main_menu_accent_colors[i] = main_menu_accent_color_for_index(i);
  }
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
  /* Open-ended mode: no goal, no alarm, timer counts up until stopped. */
  s_presets_menu_items[PRESET_MENU_INDEX_OPEN] = (SimpleMenuItem) {
    .title = "Open ended",
    .subtitle = "No goal, count up",
    .callback = preset_open_callback
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

  for (int i = 0; i < PRESET_MENU_ITEM_COUNT; i++) {
    s_presets_menu_accent_colors[i] = preset_menu_accent_color_for_index(i);
    s_presets_menu_end_time_ptrs[i] = s_presets_menu_end_time_lines[i];
  }
}

static void init_primary_windows(void) {
  s_menu_window = create_window_with_handlers((WindowHandlers) {
    .load = menu_window_load,
    .appear = menu_window_appear,
    .unload = menu_window_unload
  }, NULL);
  /* On round displays the menu is inset within the circle; use white so the
   * background outside the menu layer matches the menu's own white cells. */
  window_set_background_color(s_menu_window,
                              PBL_IF_ROUND_ELSE(GColorWhite, theme_surface_background_color()));

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

  s_stop_confirm_window = create_window_with_handlers((WindowHandlers) {
    .load = stop_confirm_window_load,
    .unload = stop_confirm_window_unload
  }, NULL);
  window_set_background_color(s_stop_confirm_window, theme_surface_background_color());

  s_presets_window = create_window_with_handlers((WindowHandlers) {
    .load = presets_window_load,
    .appear = presets_window_appear,
    .unload = presets_window_unload
  }, NULL);
  window_set_background_color(s_presets_window,
                              PBL_IF_ROUND_ELSE(GColorWhite, theme_surface_background_color()));
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

  s_delete_confirm_window = create_window_with_handlers((WindowHandlers) {
    .load = delete_confirm_window_load,
    .unload = delete_confirm_window_unload
  }, NULL);
  window_set_background_color(s_delete_confirm_window, theme_surface_background_color());

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
  }, NULL);
  window_set_background_color(s_detail_window, theme_surface_background_color());

  s_about_window = create_window_with_handlers((WindowHandlers) {
    .load = about_window_load,
    .unload = about_window_unload
  }, NULL);
  window_set_background_color(s_about_window, theme_surface_background_color());
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
  window_destroy(s_about_window);
#ifdef DEBUG
  window_destroy(s_debug_window);
#endif
  window_destroy(s_settings_window);
  window_destroy(s_stats_window);
  window_destroy(s_running_edit_window);
  window_destroy(s_history_edit_window);
  window_destroy(s_delete_confirm_window);
  window_destroy(s_history_window);
  window_destroy(s_presets_window);
  window_destroy(s_goal_window);
  window_destroy(s_stop_confirm_window);
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
  /* If a fast is already running, show the timer immediately on launch
   * so the user lands on the live countdown rather than the main menu. */
  if (fast_is_running()) {
    window_stack_push(s_timer_window, false);
  }
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
