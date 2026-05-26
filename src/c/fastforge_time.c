#include "fastforge_logic.h"

#ifdef PBL_SDK_3
#include <pebble.h>
#else
#include <time.h>
#endif

time_t local_day_start(time_t timestamp) {
  if (timestamp <= 0) {
    return 0;
  }

  struct tm *tm_info = localtime(&timestamp);
  if (!tm_info) {
    return 0;
  }

  struct tm tm_copy = *tm_info;
  tm_copy.tm_hour = 0;
  tm_copy.tm_min = 0;
  tm_copy.tm_sec = 0;
  tm_copy.tm_isdst = -1;
  return mktime(&tm_copy);
}
