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

/* history[] is static app RAM: MAX_FASTS * sizeof(FastEntry) (44 bytes/entry
 * on 32-bit time_t platforms). FastForge targets only the 128 KB app-memory
 * platforms (Pebble Time 2 / emery and Pebble Round 2 / gabbro), so the full
 * 64-entry history always fits. */
#define MAX_FASTS 64
#define DEFAULT_TARGET_MINUTES (16 * 60)

/* Pebble persistent storage stores at most 256 bytes per key. This mirrors
 * PERSIST_DATA_MAX_LENGTH from pebble.h, which the host unit tests cannot
 * include; fastforge.h static-asserts that the two values still agree.
 *
 * The history array is much larger than one persist value, so it is written as
 * a sequence of chunks (one persist key each) that each stay under the limit. */
#define FASTFORGE_PERSIST_MAX_BYTES 256
#define HISTORY_ENTRIES_PER_CHUNK ((int)(FASTFORGE_PERSIST_MAX_BYTES / sizeof(FastEntry)))
#define HISTORY_CHUNK_COUNT \
  ((MAX_FASTS + HISTORY_ENTRIES_PER_CHUNK - 1) / HISTORY_ENTRIES_PER_CHUNK)

#endif
