#ifndef FASTFORGE_LOGIC_H
#define FASTFORGE_LOGIC_H

#include "fastforge_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

time_t entry_duration_seconds(const FastEntry *entry);
uint8_t stage_level_for_elapsed(time_t elapsed_seconds);
const char *stage_text_for_elapsed(time_t elapsed_seconds);
void format_hhmmss(time_t seconds, char *buffer, size_t size);
void format_duration_hours_minutes(time_t seconds, char *buffer, size_t size);
time_t local_day_start(time_t timestamp);
bool running_fast_is_at_target(const FastEntry *entry, time_t now);

#endif
