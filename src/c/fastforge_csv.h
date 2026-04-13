#ifndef FASTFORGE_CSV_H
#define FASTFORGE_CSV_H

#include <stddef.h>

#include "fastforge_types.h"

size_t csv_append_text(char *buffer, size_t size, size_t offset,
                       const char *value);
size_t csv_append_int(char *buffer, size_t size, size_t offset, long value);

void format_history_csv_row(const FastEntry *entry, char *buffer, size_t size);
void format_history_csv_header(char *buffer, size_t size);

#endif
