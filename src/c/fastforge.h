#ifndef FASTFORGE_H
#define FASTFORGE_H

#include "fastforge_types.h"

#include <pebble.h>

#define KEY_HISTORY_COUNT 1
/* Legacy single-blob history key (app <= 1.1). Only kept so that histories
 * written by an older build can still be migrated; nothing writes it anymore. */
#define KEY_HISTORY_DATA 2
#define KEY_CURRENT_FAST 3
#define KEY_TARGET_MIN 4
#define KEY_STREAK_DATA 5
#define KEY_DEV_MODE 6
#define KEY_DEBUG_FAKE_OFFSET 7
#define KEY_DEBUG_FAST_ORIGIN 8
/* History chunks occupy KEY_HISTORY_CHUNK_BASE .. +HISTORY_CHUNK_COUNT-1.
 * Start well above the scalar keys so new scalars can be added without
 * colliding with the chunk range. */
#define KEY_HISTORY_CHUNK_BASE 16

_Static_assert(FASTFORGE_PERSIST_MAX_BYTES == PERSIST_DATA_MAX_LENGTH,
               "FASTFORGE_PERSIST_MAX_BYTES must match the SDK persist limit");
_Static_assert(sizeof(FastEntry) * HISTORY_ENTRIES_PER_CHUNK <= PERSIST_DATA_MAX_LENGTH,
               "one history chunk must fit into a single persist value");

/* The countdown screen's goal wall-clock row ("Ends 14:30"). FastForge now
 * targets only the large-memory platforms (Pebble Time 2 / emery and Pebble
 * Round 2 / gabbro), so the row is always compiled in. */
#define FASTFORGE_SHOW_GOAL_CLOCK 1

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
bool fast_start_open_ended(void);
bool fast_stop(void);
bool fast_cancel(void);
bool fast_resume_last(void);
bool history_delete_entry(int index);

#endif
