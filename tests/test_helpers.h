#ifndef FASTFORGE_TEST_HELPERS_H
#define FASTFORGE_TEST_HELPERS_H

#include <time.h>

static inline time_t make_utc_time(int year, int month, int day,
                                   int hour, int minute, int second) {
  struct tm tm_value;
  tm_value.tm_year = year - 1900;
  tm_value.tm_mon = month - 1;
  tm_value.tm_mday = day;
  tm_value.tm_hour = hour;
  tm_value.tm_min = minute;
  tm_value.tm_sec = second;
  tm_value.tm_wday = 0;
  tm_value.tm_yday = 0;
  tm_value.tm_isdst = -1;
  return mktime(&tm_value);
}

#endif
