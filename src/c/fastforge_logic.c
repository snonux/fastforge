#include "fastforge_logic.h"

#ifdef time_t
#undef time_t
#endif
#undef _TIME_H_
#include <time.h>
typedef time_t ff_sys_time_t;
#define time_t long

#include <stdio.h>
#include <stdlib.h>


time_t entry_duration_seconds(const FastEntry *entry) {
  if (!entry || entry->start_time == 0 || entry->end_time <= entry->start_time) {
    return 0;
  }
  return entry->end_time - entry->start_time;
}

uint8_t stage_level_for_elapsed(time_t elapsed_seconds) {
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

const char *stage_text_for_elapsed(time_t elapsed_seconds) {
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

void format_hhmmss(time_t seconds, char *buffer, size_t size) {
  if (seconds < 0) {
    seconds = 0;
  }
  snprintf(buffer, size, "%02d:%02d:%02d",
           (int)(seconds / 3600),
           (int)((seconds % 3600) / 60),
           (int)(seconds % 60));
}

void format_duration_hours_minutes(time_t seconds, char *buffer, size_t size) {
  if (seconds < 0) {
    seconds = 0;
  }
  int hours = (int)(seconds / 3600);
  int minutes = (int)((seconds % 3600) / 60);
  snprintf(buffer, size, "%dh %02dm", hours, minutes);
}

time_t local_day_start(time_t timestamp) {
  if (timestamp <= 0) {
    return 0;
  }

  ff_sys_time_t sys_timestamp = (ff_sys_time_t)timestamp;
  struct tm *tm_info = localtime(&sys_timestamp);
  if (!tm_info) {
    return 0;
  }

  struct tm tm_copy = *tm_info;
  tm_copy.tm_hour = 0;
  tm_copy.tm_min = 0;
  tm_copy.tm_sec = 0;
  tm_copy.tm_isdst = -1;
  return (time_t)mktime(&tm_copy);
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
  if (first_day <= 0 || second_day <= 0 || second_day < first_day) {
    return false;
  }

  ff_sys_time_t sys_first_day = (ff_sys_time_t)first_day;
  struct tm *tm_info = localtime(&sys_first_day);
  if (!tm_info) {
    return false;
  }

  struct tm next_day_tm = *tm_info;
  next_day_tm.tm_mday += 1;
  next_day_tm.tm_hour = 0;
  next_day_tm.tm_min = 0;
  next_day_tm.tm_sec = 0;
  next_day_tm.tm_isdst = -1;
  return (time_t)mktime(&next_day_tm) == second_day;
}

void fastforge_streak_recompute(const FastEntry *entries, int count, time_t now, StreakData *out) {
  if (!out) {
    return;
  }

  out->current_streak = 0;
  out->longest_streak = 0;
  out->last_completed_fast_end = 0;

  if (!entries || count <= 0) {
    return;
  }

  time_t completion_days[MAX_FASTS];
  int completion_day_count = 0;

  for (int i = 0; i < count && completion_day_count < MAX_FASTS; i++) {
    const FastEntry *entry = &entries[i];
    time_t duration = entry_duration_seconds(entry);
    if (duration <= 0 || entry->end_time <= 0) {
      continue;
    }

    if (entry->end_time > out->last_completed_fast_end) {
      out->last_completed_fast_end = entry->end_time;
    }

    time_t day_start = local_day_start(entry->end_time);
    if (day_start <= 0) {
      continue;
    }

    completion_days[completion_day_count++] = day_start;
  }

  if (completion_day_count <= 0) {
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

  time_t today_day_start = local_day_start(now);
  time_t last_completion_day = completion_days[completion_day_count - 1];
  if (last_completion_day == today_day_start ||
      is_next_local_day(last_completion_day, today_day_start)) {
    out->current_streak = run_length;
  }
  out->longest_streak = longest;
}

bool running_fast_is_at_target(const FastEntry *entry, time_t now) {
  if (!entry || entry->start_time == 0 || entry->end_time != 0 || entry->target_minutes == 0) {
    return false;
  }
  return now >= entry->start_time + (time_t)entry->target_minutes * 60;
}
