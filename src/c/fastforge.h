#ifndef FASTFORGE_H
#define FASTFORGE_H

#include <pebble.h>

typedef struct {
  time_t start_time;
  time_t end_time;           // 0 = currently running
  uint16_t target_minutes;
  char note[32];
  uint8_t max_stage_reached; // 0=none, 1=12h, 2=18h, 3=24h+
} FastEntry;

typedef struct {
  uint16_t current_streak;
  uint16_t longest_streak;
  time_t last_completed_fast_end;
} StreakData;

#define MAX_FASTS 64
#define DEFAULT_TARGET_MINUTES (16 * 60)

#define KEY_HISTORY_COUNT 1
#define KEY_HISTORY_DATA 2
#define KEY_CURRENT_FAST 3
#define KEY_TARGET_MIN 4
#define KEY_STREAK_DATA 5
#define KEY_DEV_MODE 6

extern FastEntry history[MAX_FASTS];
extern int history_count;
extern FastEntry current_fast;
extern uint16_t global_target_minutes;
extern bool developer_mode_enabled;
extern StreakData streak_data;
extern AppTimer *alarm_timer;
extern time_t target_time;

void save_all_data(void);
void load_all_data(void);
bool fast_is_running(void);
bool fast_start(uint16_t preset_target_minutes);
bool fast_stop(void);

#endif
