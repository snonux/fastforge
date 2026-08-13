#ifndef FASTFORGE_INTERNAL_H
#define FASTFORGE_INTERNAL_H

#include "fastforge.h"
#include "fastforge_logic.h"

extern bool s_fake_time_enabled;
extern int32_t s_fake_time_offset_seconds;
extern int32_t s_current_fast_origin_offset_seconds;
extern MenuLayer *s_history_menu_layer;

time_t fastforge_now(void);
void recompute_streak_data_for_today(void);
bool refresh_streak_if_day_changed(void);
void update_max_stage_if_needed(time_t elapsed_seconds);
void schedule_alarm_if_needed(void);
bool running_current_fast_is_at_target(time_t now);
void format_entry_datetime(time_t timestamp, char *buffer, size_t size);
#if FASTFORGE_SHOW_GOAL_CLOCK
void format_clock_time(time_t timestamp, time_t reference, char *buffer, size_t size);
#endif
void sort_history_by_end_time(void);
int history_index_for_row(int row);
const char *milestone_badge_label_for_level(uint8_t stage_level);
const char *history_entry_badge_label(const FastEntry *entry);
void format_optional_tag_text(const char *prefix, const char *value, char *buffer, size_t size);
int note_tag_index_for_entry(const FastEntry *entry);
void set_entry_note_from_tag_index(FastEntry *entry, int tag_index);
int history_note_tag_count(void);
void format_history_row(int row, char *title, size_t title_size, char *subtitle, size_t subtitle_size);
void collect_stats_summary(time_t *total_seconds, time_t *longest_seconds,
                           int *completed_count, int *successful_count);
void refresh_stats_window_content(void);
void fastforge_detail_layout_refresh(void);
void history_menu_reload(void);
void refresh_all_ui_state(void);
void show_goal_reached_window(void);
void show_placeholder_window(const char *title, const char *body, const char *hint);
void fastforge_force_goal_alarm(void);
void fastforge_reschedule_alarm_for_seconds(uint32_t seconds);

#endif
