#ifndef FASTFORGE_H
#define FASTFORGE_H

#include "fastforge_types.h"

#include <pebble.h>

#define KEY_HISTORY_COUNT 1
#define KEY_HISTORY_DATA 2
#define KEY_CURRENT_FAST 3
#define KEY_TARGET_MIN 4
#define KEY_STREAK_DATA 5
#define KEY_DEV_MODE 6
#define KEY_DEBUG_FAKE_OFFSET 7
#define KEY_DEBUG_FAST_ORIGIN 8

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
bool fast_cancel(void);
bool history_delete_entry(int index);

#endif
