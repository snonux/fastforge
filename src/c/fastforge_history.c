#include "fastforge_internal.h"

#include <stdlib.h>
#include <string.h>

static const char *const s_note_tags[] = {
  "",
  "felt amazing",
  "workout day",
  "travel day",
  "social day",
  "busy day",
  "reset day",
  "late meal",
  "high energy",
  "rough day"
};

static char s_stats_body_text[160];

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

void sort_history_by_end_time(void) {
  if (history_count <= 1) {
    return;
  }
  qsort(history, (size_t)history_count, sizeof(FastEntry), compare_history_entries_by_end_time);
}

const char *milestone_badge_label_for_level(uint8_t stage_level) {
  if (stage_level >= 3) {
    return "Deep Ketosis";
  }
  if (stage_level == 2) {
    return "Ketosis";
  }
  if (stage_level == 1) {
    return "Fat Burn";
  }
  return NULL;
}

void format_optional_tag_text(const char *prefix, const char *value, char *buffer, size_t size) {
  if (!buffer || size == 0) {
    return;
  }

  if (!value || value[0] == '\0') {
    snprintf(buffer, size, "%s--", prefix ? prefix : "");
    return;
  }

  snprintf(buffer, size, "%s%s", prefix ? prefix : "", value);
}

const char *history_entry_badge_label(const FastEntry *entry) {
  if (!entry) {
    return NULL;
  }
  return milestone_badge_label_for_level(entry->max_stage_reached);
}

int note_tag_index_for_entry(const FastEntry *entry) {
  if (!entry || entry->note[0] == '\0') {
    return 0;
  }

  for (int i = 1; i < (int)ARRAY_LENGTH(s_note_tags); i++) {
    if (strncmp(entry->note, s_note_tags[i], sizeof(entry->note)) == 0) {
      return i;
    }
  }

  return 0;
}

int history_note_tag_count(void) {
  return (int)ARRAY_LENGTH(s_note_tags);
}

void set_entry_note_from_tag_index(FastEntry *entry, int tag_index) {
  if (!entry) {
    return;
  }

  if (tag_index <= 0 || tag_index >= (int)ARRAY_LENGTH(s_note_tags)) {
    memset(entry->note, 0, sizeof(entry->note));
    return;
  }

  snprintf(entry->note, sizeof(entry->note), "%s", s_note_tags[tag_index]);
}

int history_index_for_row(int row) {
  if (row < 0 || row >= history_count) {
    return -1;
  }
  return history_count - 1 - row;
}

static void format_history_row_impl(int row, char *title, size_t title_size, char *subtitle, size_t subtitle_size) {
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
  char badge_text[24];
  char note_text[32];
  const char *badge_label = history_entry_badge_label(entry);
  format_entry_datetime(entry->end_time, date_text, sizeof(date_text));
  format_duration_hours_minutes(entry_duration_seconds(entry), duration_text, sizeof(duration_text));
  snprintf(title, title_size, "%s", date_text);
  note_text[0] = '\0';
  if (entry->note[0] != '\0') {
    snprintf(note_text, sizeof(note_text), " | %s", entry->note);
  }
  badge_text[0] = '\0';
  if (badge_label) {
    snprintf(badge_text, sizeof(badge_text), " | %s", badge_label);
  }
  snprintf(subtitle, subtitle_size, "%s%s%s", duration_text, badge_text, note_text);
}

void format_history_row(int row, char *title, size_t title_size, char *subtitle, size_t subtitle_size) {
  format_history_row_impl(row, title, title_size, subtitle, subtitle_size);
}

static void collect_stats_summary(time_t *total_seconds, time_t *longest_seconds,
                                  int *completed_count, int *successful_count) {
  time_t total_seconds_value = 0;
  time_t longest_seconds_value = 0;
  int completed_count_value = 0;
  int successful_count_value = 0;

  for (int i = 0; i < history_count; i++) {
    const FastEntry *entry = &history[i];
    time_t duration = entry_duration_seconds(entry);
    if (duration <= 0) {
      continue;
    }

    completed_count_value++;
    total_seconds_value += duration;
    if (duration > longest_seconds_value) {
      longest_seconds_value = duration;
    }
    if (entry->target_minutes > 0 && duration >= (time_t)entry->target_minutes * 60) {
      successful_count_value++;
    }
  }

  if (total_seconds) {
    *total_seconds = total_seconds_value;
  }
  if (longest_seconds) {
    *longest_seconds = longest_seconds_value;
  }
  if (completed_count) {
    *completed_count = completed_count_value;
  }
  if (successful_count) {
    *successful_count = successful_count_value;
  }
}

static void format_stats_window_body(time_t total_seconds, time_t longest_seconds,
                                     int completed_count, int successful_count) {
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
}

void refresh_stats_window_content(void) {
  if (!s_stats_body_layer) {
    return;
  }

  time_t total_seconds = 0;
  time_t longest_seconds = 0;
  int completed_count = 0;
  int successful_count = 0;
  collect_stats_summary(&total_seconds, &longest_seconds, &completed_count, &successful_count);
  format_stats_window_body(total_seconds, longest_seconds, completed_count, successful_count);
  text_layer_set_text(s_stats_body_layer, s_stats_body_text);
}

void history_menu_reload(void) {
  if (s_history_menu_layer) {
    menu_layer_reload_data(s_history_menu_layer);
  }
}
