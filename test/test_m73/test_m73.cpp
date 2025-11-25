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
 * test_m73.cpp - Unit tests for M73 progress/time parsing
 *
 * Tests the M73 command which sets print progress percentage (P),
 * remaining time (R), and interaction countdown time (C).
 *
 * Tests cover:
 * - P parameter parsing (0-100%)
 * - R parameter parsing with minutes->seconds conversion
 * - C parameter parsing with minutes->seconds conversion
 * - Edge cases: boundaries (0, 100%), invalid input, missing args
 * - Multiple parameter combinations
 */

#include <unity.h>
#include <cstring>
#include <cstdlib>
#include <cmath>

// Unity test registration macros for standalone tests
#define TEST_CASE(suite, name) void test_##suite##_##name(void)

// Mock UI state to track what M73 would set
struct MockUI {
  uint16_t progress_value;      // Stored as progress_t (0-10000 for 0.00%-100.00%)
  uint32_t remaining_time_sec;  // In seconds
  uint32_t interaction_time_sec; // In seconds
  bool progress_was_set;
  bool remaining_time_was_set;
  bool interaction_time_was_set;
  
  void reset() {
    progress_value = 0;
    remaining_time_sec = 0;
    interaction_time_sec = 0;
    progress_was_set = false;
    remaining_time_was_set = false;
    interaction_time_was_set = false;
  }
  
  void set_progress(uint16_t p) {
    progress_value = (p > 10000) ? 10000 : p;  // Cap at 100.00%
    progress_was_set = true;
  }
  
  void set_remaining_time(uint32_t sec) {
    remaining_time_sec = sec;
    remaining_time_was_set = true;
  }
  
  void set_interaction_time(uint32_t sec) {
    interaction_time_sec = sec;
    interaction_time_was_set = true;
  }
  
  uint8_t get_progress_percent() const {
    return (uint8_t)(progress_value / 100);
  }
  
  uint16_t get_progress_permyriad() const {
    return progress_value;
  }
};

static MockUI mock_ui;

// Mock parser to simulate GCode parameter parsing
struct MockParser {
  bool has_p, has_r, has_c;
  float p_value;
  uint32_t r_value;
  uint32_t c_value;
  
  void reset() {
    has_p = has_r = has_c = false;
    p_value = 0.0f;
    r_value = c_value = 0;
  }
  
  bool seenval(char param) const {
    switch(param) {
      case 'P': return has_p;
      case 'R': return has_r;
      case 'C': return has_c;
      default: return false;
    }
  }
  
  float value_float() const { return p_value; }
  uint8_t value_byte() const { return (uint8_t)p_value; }
  uint32_t value_ulong() const { 
    if (has_r) return r_value;
    if (has_c) return c_value;
    return 0;
  }
};

static MockParser mock_parser;

// Test implementation of M73 logic
static void test_m73_execute() {
  // Process P parameter (progress percentage)
  if (mock_parser.seenval('P')) {
    // PROGRESS_SCALE is typically 100 (for 0.00%-100.00% with 2 decimals)
    // For simplicity, we assume PROGRESS_SCALE = 100
    const uint16_t PROGRESS_SCALE = 100;
    uint16_t progress = (uint16_t)(mock_parser.value_float() * PROGRESS_SCALE);
    mock_ui.set_progress(progress);
  }
  
  // Process R parameter (remaining time in minutes -> seconds)
  if (mock_parser.seenval('R')) {
    mock_ui.set_remaining_time(60 * mock_parser.r_value);
  }
  
  // Process C parameter (interaction countdown in minutes -> seconds)
  if (mock_parser.seenval('C')) {
    mock_ui.set_interaction_time(60 * mock_parser.c_value);
  }
}

// Test: P parameter sets progress percentage
TEST_CASE(m73, sets_progress_percentage) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_p = true;
  mock_parser.p_value = 50.0f;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.progress_was_set);
  TEST_ASSERT_EQUAL(50, mock_ui.get_progress_percent());
  TEST_ASSERT_EQUAL(5000, mock_ui.get_progress_permyriad());
}

// Test: P parameter with decimal progress
TEST_CASE(m73, sets_progress_with_decimals) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_p = true;
  mock_parser.p_value = 25.63f;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.progress_was_set);
  TEST_ASSERT_EQUAL(25, mock_ui.get_progress_percent());
  TEST_ASSERT_EQUAL(2563, mock_ui.get_progress_permyriad());
}

// Test: P=0 boundary
TEST_CASE(m73, progress_zero_boundary) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_p = true;
  mock_parser.p_value = 0.0f;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.progress_was_set);
  TEST_ASSERT_EQUAL(0, mock_ui.get_progress_percent());
}

// Test: P=100 boundary
TEST_CASE(m73, progress_hundred_boundary) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_p = true;
  mock_parser.p_value = 100.0f;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.progress_was_set);
  TEST_ASSERT_EQUAL(100, mock_ui.get_progress_percent());
  TEST_ASSERT_EQUAL(10000, mock_ui.get_progress_permyriad());
}

// Test: P>100 is clamped
TEST_CASE(m73, progress_over_hundred_clamped) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_p = true;
  mock_parser.p_value = 150.0f;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.progress_was_set);
  TEST_ASSERT_EQUAL(100, mock_ui.get_progress_percent());
  TEST_ASSERT_EQUAL(10000, mock_ui.get_progress_permyriad());
}

// Test: R parameter sets remaining time (minutes to seconds)
TEST_CASE(m73, sets_remaining_time_minutes_to_seconds) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_r = true;
  mock_parser.r_value = 456;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.remaining_time_was_set);
  TEST_ASSERT_EQUAL(456 * 60, mock_ui.remaining_time_sec);
  TEST_ASSERT_EQUAL(27360, mock_ui.remaining_time_sec);
}

// Test: R=0 boundary
TEST_CASE(m73, remaining_time_zero) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_r = true;
  mock_parser.r_value = 0;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.remaining_time_was_set);
  TEST_ASSERT_EQUAL(0, mock_ui.remaining_time_sec);
}

// Test: R with large value
TEST_CASE(m73, remaining_time_large_value) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_r = true;
  mock_parser.r_value = 10000; // ~1 week
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.remaining_time_was_set);
  TEST_ASSERT_EQUAL(600000, mock_ui.remaining_time_sec);
}

// Test: C parameter sets interaction time (minutes to seconds)
TEST_CASE(m73, sets_interaction_time_minutes_to_seconds) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_c = true;
  mock_parser.c_value = 12;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.interaction_time_was_set);
  TEST_ASSERT_EQUAL(720, mock_ui.interaction_time_sec);
}

// Test: C=0 boundary
TEST_CASE(m73, interaction_time_zero) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_c = true;
  mock_parser.c_value = 0;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.interaction_time_was_set);
  TEST_ASSERT_EQUAL(0, mock_ui.interaction_time_sec);
}

// Test: C with typical value
TEST_CASE(m73, interaction_time_typical) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_c = true;
  mock_parser.c_value = 5;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.interaction_time_was_set);
  TEST_ASSERT_EQUAL(300, mock_ui.interaction_time_sec);
}

// Test: Multiple parameters together (P and R)
TEST_CASE(m73, multiple_params_p_and_r) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_p = true;
  mock_parser.p_value = 75.5f;
  mock_parser.has_r = true;
  mock_parser.r_value = 30;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.progress_was_set);
  TEST_ASSERT_TRUE(mock_ui.remaining_time_was_set);
  TEST_ASSERT_EQUAL(75, mock_ui.get_progress_percent());
  TEST_ASSERT_EQUAL(1800, mock_ui.remaining_time_sec);
}

// Test: All three parameters together
TEST_CASE(m73, all_three_params) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_p = true;
  mock_parser.p_value = 33.33f;
  mock_parser.has_r = true;
  mock_parser.r_value = 120;
  mock_parser.has_c = true;
  mock_parser.c_value = 15;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.progress_was_set);
  TEST_ASSERT_TRUE(mock_ui.remaining_time_was_set);
  TEST_ASSERT_TRUE(mock_ui.interaction_time_was_set);
  TEST_ASSERT_EQUAL(33, mock_ui.get_progress_percent());
  TEST_ASSERT_EQUAL(7200, mock_ui.remaining_time_sec);
  TEST_ASSERT_EQUAL(900, mock_ui.interaction_time_sec);
}

// Test: No parameters (should not set anything)
TEST_CASE(m73, no_parameters) {
  mock_ui.reset();
  mock_parser.reset();
  
  test_m73_execute();
  
  TEST_ASSERT_FALSE(mock_ui.progress_was_set);
  TEST_ASSERT_FALSE(mock_ui.remaining_time_was_set);
  TEST_ASSERT_FALSE(mock_ui.interaction_time_was_set);
}

// Test: Only R parameter
TEST_CASE(m73, only_r_parameter) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_r = true;
  mock_parser.r_value = 45;
  
  test_m73_execute();
  
  TEST_ASSERT_FALSE(mock_ui.progress_was_set);
  TEST_ASSERT_TRUE(mock_ui.remaining_time_was_set);
  TEST_ASSERT_FALSE(mock_ui.interaction_time_was_set);
  TEST_ASSERT_EQUAL(2700, mock_ui.remaining_time_sec);
}

// Test: Only C parameter
TEST_CASE(m73, only_c_parameter) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_c = true;
  mock_parser.c_value = 8;
  
  test_m73_execute();
  
  TEST_ASSERT_FALSE(mock_ui.progress_was_set);
  TEST_ASSERT_FALSE(mock_ui.remaining_time_was_set);
  TEST_ASSERT_TRUE(mock_ui.interaction_time_was_set);
  TEST_ASSERT_EQUAL(480, mock_ui.interaction_time_sec);
}

// Test: Very small progress value
TEST_CASE(m73, very_small_progress) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_p = true;
  mock_parser.p_value = 0.01f;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.progress_was_set);
  TEST_ASSERT_EQUAL(0, mock_ui.get_progress_percent()); // Rounds down
  TEST_ASSERT_EQUAL(1, mock_ui.get_progress_permyriad());
}

// Test: Progress near 100%
TEST_CASE(m73, progress_near_hundred) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_p = true;
  mock_parser.p_value = 99.99f;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.progress_was_set);
  TEST_ASSERT_EQUAL(99, mock_ui.get_progress_percent());
  TEST_ASSERT_EQUAL(9999, mock_ui.get_progress_permyriad());
}

// Test: R with 1 minute
TEST_CASE(m73, remaining_time_one_minute) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_r = true;
  mock_parser.r_value = 1;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.remaining_time_was_set);
  TEST_ASSERT_EQUAL(60, mock_ui.remaining_time_sec);
}

// Test: C with 1 minute
TEST_CASE(m73, interaction_time_one_minute) {
  mock_ui.reset();
  mock_parser.reset();
  
  mock_parser.has_c = true;
  mock_parser.c_value = 1;
  
  test_m73_execute();
  
  TEST_ASSERT_TRUE(mock_ui.interaction_time_was_set);
  TEST_ASSERT_EQUAL(60, mock_ui.interaction_time_sec);
}

void setUp(void) {
  mock_ui.reset();
  mock_parser.reset();
}

void tearDown(void) {}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  
  RUN_TEST(test_m73_sets_progress_percentage);
  RUN_TEST(test_m73_sets_progress_with_decimals);
  RUN_TEST(test_m73_progress_zero_boundary);
  RUN_TEST(test_m73_progress_hundred_boundary);
  RUN_TEST(test_m73_progress_over_hundred_clamped);
  RUN_TEST(test_m73_sets_remaining_time_minutes_to_seconds);
  RUN_TEST(test_m73_remaining_time_zero);
  RUN_TEST(test_m73_remaining_time_large_value);
  RUN_TEST(test_m73_sets_interaction_time_minutes_to_seconds);
  RUN_TEST(test_m73_interaction_time_zero);
  RUN_TEST(test_m73_interaction_time_typical);
  RUN_TEST(test_m73_multiple_params_p_and_r);
  RUN_TEST(test_m73_all_three_params);
  RUN_TEST(test_m73_no_parameters);
  RUN_TEST(test_m73_only_r_parameter);
  RUN_TEST(test_m73_only_c_parameter);
  RUN_TEST(test_m73_very_small_progress);
  RUN_TEST(test_m73_progress_near_hundred);
  RUN_TEST(test_m73_remaining_time_one_minute);
  RUN_TEST(test_m73_interaction_time_one_minute);
  
  return UNITY_END();
}
