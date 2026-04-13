#include "fastforge_csv.h"

#include <stdbool.h>
#include <stdio.h>

size_t csv_append_text(char *buffer, size_t size, size_t offset,
                       const char *value) {
  if (!buffer || size == 0 || offset >= size) {
    return offset;
  }

  bool needs_quotes = false;
  if (value) {
    for (const char *cursor = value; *cursor; cursor++) {
      if (*cursor == ',' || *cursor == '"' || *cursor == '\n' ||
          *cursor == '\r') {
        needs_quotes = true;
        break;
      }
    }
  }

  if (needs_quotes) {
    if (offset + 1 < size) {
      buffer[offset++] = '"';
    }
    if (value) {
      for (const char *cursor = value; *cursor && offset + 1 < size; cursor++) {
        if (*cursor == '"') {
          if (offset + 2 >= size) {
            break;
          }
          buffer[offset++] = '"';
          buffer[offset++] = '"';
        } else {
          buffer[offset++] = *cursor;
        }
      }
    }
    if (offset + 1 < size) {
      buffer[offset++] = '"';
    }
  } else if (value) {
    for (const char *cursor = value; *cursor && offset + 1 < size; cursor++) {
      buffer[offset++] = *cursor;
    }
  }

  if (offset < size) {
    buffer[offset] = '\0';
  }
  return offset;
}

size_t csv_append_int(char *buffer, size_t size, size_t offset, long value) {
  char number_text[16];
  snprintf(number_text, sizeof(number_text), "%ld", value);
  return csv_append_text(buffer, size, offset, number_text);
}

void format_history_csv_row(const FastEntry *entry, char *buffer, size_t size) {
  if (!buffer || size == 0) {
    return;
  }

  buffer[0] = '\0';
  if (!entry) {
    return;
  }

  size_t offset = 0;
  offset = csv_append_int(buffer, size, offset, (long)entry->start_time);
  if (offset + 1 < size) {
    buffer[offset++] = ',';
    buffer[offset] = '\0';
  }
  offset = csv_append_int(buffer, size, offset, (long)entry->end_time);
  if (offset + 1 < size) {
    buffer[offset++] = ',';
    buffer[offset] = '\0';
  }
  offset = csv_append_int(buffer, size, offset, (long)entry->target_minutes);
  if (offset + 1 < size) {
    buffer[offset++] = ',';
    buffer[offset] = '\0';
  }
  offset = csv_append_text(buffer, size, offset, entry->note);
  if (offset + 1 < size) {
    buffer[offset++] = ',';
    buffer[offset] = '\0';
  }
  (void)csv_append_int(buffer, size, offset, (long)entry->max_stage_reached);
}

void format_history_csv_header(char *buffer, size_t size) {
  if (!buffer || size == 0) {
    return;
  }

  snprintf(buffer, size,
           "start_time,end_time,target_minutes,note,max_stage_reached");
}
