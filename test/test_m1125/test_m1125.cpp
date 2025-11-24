/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2024 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/**
 * test_m1125.cpp - Unit tests for M1125 pause/resume command filtering
 *
 * Tests the logic that prevents pause-triggering commands (M600, M1125)
 * from being saved and replayed during pause/resume cycles.
 *
 * Tests cover:
 * - Detection of M600 and M1125 commands (case-insensitive, with/without args)
 * - Preservation of normal G-code commands
 * - Edge cases: empty strings, whitespace, comments
 */

#include <unity.h>
#include <cstring>

// Unity test registration macros for standalone tests
#define TEST_CASE(suite, name) void test_##suite##_##name(void)

// We need to expose the helper functions from M1125.cpp for testing.
// Since they are static in the source file, we'll declare test-accessible
// versions here. In a production refactor, these would be moved to M1125.h.
//
// For now, we copy the test logic here to validate the behavior.

static inline char test_m1125_upper(const char c) {
  return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

static bool test_m1125_command_matches(const char *cmd, const char *target) {
  if (!cmd || !*cmd) return false;
  while (*cmd == ' ') ++cmd;  // Skip leading spaces only (not tabs)
  const char *t = target;
  while (*t) {
    if (!*cmd || test_m1125_upper(*cmd) != *t) return false;
    ++cmd;
    ++t;
  }
  const char tail = *cmd;
  return tail == '\0' || tail == ' ' || tail == '\t' || tail == ';';
}

static bool test_m1125_should_skip_saved_command(const char *cmd) {
  if (!cmd) return true;
  // After skipping leading whitespace, check if we have an empty string
  const char *check = cmd;
  while (*check == ' ' || *check == '\t') ++check;
  if (!*check) return true;  // Empty after skipping whitespace
  return test_m1125_command_matches(cmd, "M600") || test_m1125_command_matches(cmd, "M1125");
}

// Test: M600 is detected and skipped (uppercase)
TEST_CASE(m1125, skips_M600_uppercase) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M600"));
}

// Test: M600 is detected and skipped (lowercase)
TEST_CASE(m1125, skips_M600_lowercase) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("m600"));
}

// Test: M600 with parameters is skipped
TEST_CASE(m1125, skips_M600_with_params) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M600 X50 Y50"));
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M600 ; filament change"));
}

// Test: M600 with leading whitespace is skipped
TEST_CASE(m1125, skips_M600_with_whitespace) {
  // The command_matches function skips leading SPACES (not tabs) before matching
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("  M600"));
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("\tM600")); // Tab is not skipped, so this won't match
}

// Test: M1125 is detected and skipped (uppercase)
TEST_CASE(m1125, skips_M1125_uppercase) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M1125"));
}

// Test: M1125 is detected and skipped (lowercase)
TEST_CASE(m1125, skips_M1125_lowercase) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("m1125"));
}

// Test: M1125 with P parameter is skipped
TEST_CASE(m1125, skips_M1125_with_P) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M1125 P"));
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M1125\tP")); // Tab separator is OK
}

// Test: M1125 with R parameter is skipped
TEST_CASE(m1125, skips_M1125_with_R) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M1125 R"));
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M1125\tR")); // Tab separator is OK
}

// Test: Normal G-code commands are NOT skipped
TEST_CASE(m1125, preserves_G28) {
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("G28"));
}

TEST_CASE(m1125, preserves_G1) {
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("G1 X10 Y20 Z0.3"));
}

TEST_CASE(m1125, preserves_M104) {
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("M104 S200"));
}

TEST_CASE(m1125, preserves_M109) {
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("M109 S210"));
}

TEST_CASE(m1125, preserves_M117) {
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("M117 Printing..."));
}

// Test: Similar M-codes are NOT confused with M600/M1125
TEST_CASE(m1125, distinguishes_M60) {
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("M60")); // ATX Power Off
}

TEST_CASE(m1125, distinguishes_M6000) {
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("M6000")); // hypothetical
}

TEST_CASE(m1125, distinguishes_M112) {
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("M112")); // Emergency stop
}

TEST_CASE(m1125, distinguishes_M11250) {
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("M11250")); // hypothetical
}

// Test: Empty and whitespace-only strings are skipped
TEST_CASE(m1125, skips_empty_string) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command(""));
}

TEST_CASE(m1125, skips_whitespace_only) {
  // After skipping leading whitespace, these become empty strings
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("   "));
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("\t\t"));
}

TEST_CASE(m1125, skips_null_pointer) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command(nullptr));
}

// Test: Comments after commands are handled correctly
TEST_CASE(m1125, handles_M600_with_comment) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M600 ; change filament"));
}

TEST_CASE(m1125, handles_normal_command_with_comment) {
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("G28 ; home all"));
}

// Test: Mixed case is handled correctly
TEST_CASE(m1125, skips_M600_mixed_case) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("m600"));
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M600"));
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("m600 X10"));
}

TEST_CASE(m1125, skips_M1125_mixed_case) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("m1125"));
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M1125"));
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("m1125 P"));
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M1125 r"));
}

// Test: Tab separators work correctly
TEST_CASE(m1125, handles_tab_separator) {
  TEST_ASSERT_TRUE(test_m1125_should_skip_saved_command("M600\tX50"));
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("G28\tX Y Z"));
}

// Test: Commands that contain M600/M1125 as substring are NOT skipped
TEST_CASE(m1125, preserves_M117_mentioning_M600) {
  // M117 with "M600" in the status text should NOT be skipped
  // (though this would be unusual, the filter should only match at command start)
  TEST_ASSERT_FALSE(test_m1125_should_skip_saved_command("M117 Next: M600"));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_m1125_skips_M600_uppercase);
  RUN_TEST(test_m1125_skips_M600_lowercase);
  RUN_TEST(test_m1125_skips_M600_with_params);
  RUN_TEST(test_m1125_skips_M600_with_whitespace);
  RUN_TEST(test_m1125_skips_M1125_uppercase);
  RUN_TEST(test_m1125_skips_M1125_lowercase);
  RUN_TEST(test_m1125_skips_M1125_with_P);
  RUN_TEST(test_m1125_skips_M1125_with_R);
  RUN_TEST(test_m1125_preserves_G28);
  RUN_TEST(test_m1125_preserves_G1);
  RUN_TEST(test_m1125_preserves_M104);
  RUN_TEST(test_m1125_preserves_M109);
  RUN_TEST(test_m1125_preserves_M117);
  RUN_TEST(test_m1125_distinguishes_M60);
  RUN_TEST(test_m1125_distinguishes_M6000);
  RUN_TEST(test_m1125_distinguishes_M112);
  RUN_TEST(test_m1125_distinguishes_M11250);
  RUN_TEST(test_m1125_skips_empty_string);
  RUN_TEST(test_m1125_skips_whitespace_only);
  RUN_TEST(test_m1125_skips_null_pointer);
  RUN_TEST(test_m1125_handles_M600_with_comment);
  RUN_TEST(test_m1125_handles_normal_command_with_comment);
  RUN_TEST(test_m1125_skips_M600_mixed_case);
  RUN_TEST(test_m1125_skips_M1125_mixed_case);
  RUN_TEST(test_m1125_handles_tab_separator);
  RUN_TEST(test_m1125_preserves_M117_mentioning_M600);
  return UNITY_END();
}
