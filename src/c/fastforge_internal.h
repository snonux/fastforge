#ifndef FASTFORGE_INTERNAL_H
#define FASTFORGE_INTERNAL_H

#include "fastforge.h"

extern bool s_fake_time_enabled;
extern int32_t s_fake_time_offset_seconds;
extern int32_t s_current_fast_origin_offset_seconds;
extern MenuLayer *s_history_menu_layer;
extern TextLayer *s_stats_body_layer;

time_t fastforge_now(void);
void recompute_streak_data_for_today(void);
bool refresh_streak_if_day_changed(void);
void update_max_stage_if_needed(time_t elapsed_seconds);
void schedule_alarm_if_needed(void);
time_t entry_duration_seconds(const FastEntry *entry);
void format_hhmmss(time_t seconds, char *buffer, size_t size);
void format_duration_hours_minutes(time_t seconds, char *buffer, size_t size);
void format_entry_datetime(time_t timestamp, char *buffer, size_t size);
uint8_t stage_level_for_elapsed(time_t elapsed_seconds);
const char *stage_text_for_elapsed(time_t elapsed_seconds);
bool running_fast_is_at_target(time_t now);
void sort_history_by_end_time(void);
int history_index_for_row(int row);
const char *milestone_badge_label_for_level(uint8_t stage_level);
const char *history_entry_badge_label(const FastEntry *entry);
void format_optional_tag_text(const char *prefix, const char *value, char *buffer, size_t size);
int note_tag_index_for_entry(const FastEntry *entry);
void set_entry_note_from_tag_index(FastEntry *entry, int tag_index);
int history_note_tag_count(void);
void format_history_row(int row, char *title, size_t title_size, char *subtitle, size_t subtitle_size);
void refresh_stats_window_content(void);
void history_menu_reload(void);
void request_history_export(void);
void refresh_all_ui_state(void);
void show_goal_reached_window(void);
void show_placeholder_window(const char *title, const char *body, const char *hint);
void fastforge_history_register_app_message_handlers(void);
void fastforge_history_stop_export(void);
void fastforge_force_goal_alarm(void);

#endif
