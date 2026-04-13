#include "fastforge_logic.h"

#ifdef time_t
#undef time_t
#endif
#undef _TIME_H_
#include <time.h>
typedef time_t ff_sys_time_t;
#define time_t long

#include <stdio.h>


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

bool running_fast_is_at_target(const FastEntry *entry, time_t now) {
  if (!entry || entry->start_time == 0 || entry->end_time != 0 || entry->target_minutes == 0) {
    return false;
  }
  return now >= entry->start_time + (time_t)entry->target_minutes * 60;
}
