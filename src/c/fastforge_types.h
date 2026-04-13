#ifndef FASTFORGE_TYPES_H
#define FASTFORGE_TYPES_H

#include <stdint.h>
#include <time.h>

/* Persisted fast record shared by the running state and history list. */
typedef struct {
  time_t start_time;
  time_t end_time;           // 0 = currently running
  uint16_t target_minutes;
  char note[32];
  uint8_t max_stage_reached; // 0=none, 1=12h, 2=18h, 3=24h+
} FastEntry;

/* Derived streak counters rebuilt from completed fast end dates. */
typedef struct {
  uint16_t current_streak;
  uint16_t longest_streak;
  time_t last_completed_fast_end;
} StreakData;

#define MAX_FASTS 64
#define DEFAULT_TARGET_MINUTES (16 * 60)

#endif
