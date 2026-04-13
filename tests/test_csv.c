#include "vendor/unity/src/unity.h"

#include "../src/c/fastforge_csv.h"

#include <string.h>

void test_csv_append_text_plain_and_escaped_values(void) {
  char buffer[128];
  size_t offset;

  buffer[0] = '\0';
  offset = csv_append_text(buffer, sizeof(buffer), 0, "plain");
  TEST_ASSERT_EQUAL_size_t(strlen("plain"), offset);
  TEST_ASSERT_EQUAL_STRING("plain", buffer);

  buffer[0] = '\0';
  offset = csv_append_text(buffer, sizeof(buffer), 0, "alpha,beta");
  TEST_ASSERT_EQUAL_STRING("\"alpha,beta\"", buffer);
  TEST_ASSERT_EQUAL_size_t(strlen("\"alpha,beta\""), offset);

  buffer[0] = '\0';
  offset = csv_append_text(buffer, sizeof(buffer), 0, "say \"hi\"");
  TEST_ASSERT_EQUAL_STRING("\"say \"\"hi\"\"\"", buffer);
  TEST_ASSERT_EQUAL_size_t(strlen("\"say \"\"hi\"\"\""), offset);

  strcpy(buffer, "seed");
  offset = csv_append_text(buffer, sizeof(buffer), 4, NULL);
  TEST_ASSERT_EQUAL_size_t(4, offset);
  TEST_ASSERT_EQUAL_STRING("seed", buffer);
}

void test_csv_append_int_appends_number_text(void) {
  char buffer[64];
  size_t offset;

  buffer[0] = '\0';
  offset = csv_append_int(buffer, sizeof(buffer), 0, -42);
  TEST_ASSERT_EQUAL_size_t(strlen("-42"), offset);
  TEST_ASSERT_EQUAL_STRING("-42", buffer);
}

void test_format_history_csv_header_and_row_with_escaping(void) {
  FastEntry entry;
  char row[256];
  char header[128];

  memset(&entry, 0, sizeof(entry));
  entry.start_time = 100;
  entry.end_time = 200;
  entry.target_minutes = 960;
  entry.max_stage_reached = 2;
  snprintf(entry.note, sizeof(entry.note), "meal, \"keto\"");

  format_history_csv_header(header, sizeof(header));
  TEST_ASSERT_EQUAL_STRING(
      "start_time,end_time,target_minutes,note,max_stage_reached",
      header);

  format_history_csv_row(&entry, row, sizeof(row));
  TEST_ASSERT_EQUAL_STRING("100,200,960,\"meal, \"\"keto\"\"\",2", row);
}

void test_format_history_csv_row_null_entry_returns_empty(void) {
  char row[32];
  strcpy(row, "not-empty");

  format_history_csv_row(NULL, row, sizeof(row));

  TEST_ASSERT_EQUAL_STRING("", row);
}
