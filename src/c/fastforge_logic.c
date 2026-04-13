#include "fastforge_logic.h"

#ifdef time_t
#undef time_t
#endif
#undef _TIME_H_
#include <time.h>
typedef time_t ff_sys_time_t;
#define time_t long

#include <stdio.h>
#include <stdint.h>


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



/* Convert a Unix timestamp to a UTC day number (seconds since epoch / 86400).
 * Avoids localtime()/mktime() calls that consume significant stack depth. */
static time_t utc_day_number(time_t t) {
  return (t > 0) ? (t / 86400) : -1;
}

/* Recompute streak data from a history array sorted by end_time ascending
 * (guaranteed by sort_history_by_end_time()).
 *
 * Uses UTC day numbers via integer division to avoid localtime()/mktime()
 * calls — those functions burn stack in the already-deep Pebble call chain
 * (app task stack ≈2 KB, typically 1.5 KB consumed by firmware before our
 * click handler fires). */
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

  time_t prev_day = -1;
  uint16_t run_length = 0;
  uint16_t longest = 0;

  for (int i = 0; i < count; i++) {
    const FastEntry *entry = &entries[i];
    if (entry_duration_seconds(entry) <= 0 || entry->end_time <= 0) {
      continue;
    }

    if (entry->end_time > out->last_completed_fast_end) {
      out->last_completed_fast_end = entry->end_time;
    }

    time_t day = utc_day_number(entry->end_time);
    if (day < 0) {
      continue;
    }

    if (day == prev_day) {
      /* Multiple fasts on the same UTC day — count the day only once. */
      continue;
    }

    if (prev_day < 0 || day == prev_day + 1) {
      run_length++;
    } else {
      run_length = 1;
    }

    if (run_length > longest) {
      longest = run_length;
    }
    prev_day = day;
  }

  if (run_length == 0) {
    return; /* no valid completions */
  }

  if (longest == 0) {
    longest = run_length;
  }

  /* Current streak is live only if the last completion was today or yesterday. */
  time_t today = utc_day_number(now);
  if (prev_day == today || prev_day == today - 1) {
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
