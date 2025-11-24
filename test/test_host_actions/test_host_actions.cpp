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
 * test_host_actions.cpp - Unit tests for host action notification caching
 *
 * Tests the logic that prevents redundant host notifications from being
 * repeatedly emitted when the same notification text is sent multiple times.
 *
 * Tests cover:
 * - Notification caching and deduplication
 * - M117 suppression detection for host-derived notifications
 * - Cache clearing and edge cases
 */

#include <unity.h>
#include <cstring>
#include <cstdio>

// Unity test registration macros for standalone tests
#define TEST_CASE(suite, name) void test_##suite##_##name(void)

// Constants matching the host_actions implementation
#define HOSTUI_NOTIFICATION_CACHE_SIZE 64

// Test implementation of notification caching logic
// This replicates the logic added to host_actions.cpp
static char test_last_notification[HOSTUI_NOTIFICATION_CACHE_SIZE] = "";

static void test_hostui_store_notification(const char* text) {
  if (!text) return;
  strncpy(test_last_notification, text, HOSTUI_NOTIFICATION_CACHE_SIZE - 1);
  test_last_notification[HOSTUI_NOTIFICATION_CACHE_SIZE - 1] = '\0';
}

static bool test_hostui_is_duplicate_notification(const char* text) {
  if (!text) return true;
  return strcmp(test_last_notification, text) == 0;
}

static void test_hostui_clear_notification_cache() {
  test_last_notification[0] = '\0';
}

// Test implementation of M117 suppression detection
// This checks if a notification was recently sent and should suppress M117
static bool test_hostui_should_suppress_m117(const char* text) {
  if (!text || !*text) return false;
  return test_hostui_is_duplicate_notification(text);
}

// Test: First notification is not a duplicate
TEST_CASE(host_actions, first_notification_not_duplicate) {
  test_hostui_clear_notification_cache();
  
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification("Print started"));
}

// Test: Storing and detecting duplicate notification
TEST_CASE(host_actions, detects_duplicate_notification) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification("Print paused");
  
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification("Print paused"));
}

// Test: Different notification is not a duplicate
TEST_CASE(host_actions, different_notification_not_duplicate) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification("Print paused");
  
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification("Print resumed"));
}

// Test: Cache can be updated with new notification
TEST_CASE(host_actions, cache_updates_with_new_notification) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification("First message");
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification("First message"));
  
  test_hostui_store_notification("Second message");
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification("Second message"));
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification("First message"));
}

// Test: Empty string handling
TEST_CASE(host_actions, handles_empty_string) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification("");
  
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification(""));
}

// Test: Null pointer handling
TEST_CASE(host_actions, handles_null_pointer) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification(nullptr);
  
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification(nullptr));
}

// Test: Cache clearing works
TEST_CASE(host_actions, cache_clearing_works) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification("Cached message");
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification("Cached message"));
  
  test_hostui_clear_notification_cache();
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification("Cached message"));
}

// Test: Long messages are truncated properly
TEST_CASE(host_actions, long_message_truncation) {
  test_hostui_clear_notification_cache();
  
  char long_message[HOSTUI_NOTIFICATION_CACHE_SIZE + 20];
  memset(long_message, 'A', sizeof(long_message) - 1);
  long_message[sizeof(long_message) - 1] = '\0';
  
  test_hostui_store_notification(long_message);
  
  // Should match the truncated version
  char expected[HOSTUI_NOTIFICATION_CACHE_SIZE];
  memset(expected, 'A', HOSTUI_NOTIFICATION_CACHE_SIZE - 1);
  expected[HOSTUI_NOTIFICATION_CACHE_SIZE - 1] = '\0';
  
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification(expected));
}

// Test: Case sensitivity
TEST_CASE(host_actions, case_sensitive_comparison) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification("Print Paused");
  
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification("Print Paused"));
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification("print paused"));
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification("PRINT PAUSED"));
}

// Test: Whitespace matters
TEST_CASE(host_actions, whitespace_matters) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification("Print paused");
  
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification("Print paused"));
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification("Print  paused"));
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification(" Print paused"));
}

// Test: M117 suppression for duplicate notification
TEST_CASE(host_actions, m117_suppressed_for_duplicate) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification("Layer 5/100");
  
  TEST_ASSERT_TRUE(test_hostui_should_suppress_m117("Layer 5/100"));
}

// Test: M117 not suppressed for new notification
TEST_CASE(host_actions, m117_not_suppressed_for_new) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification("Layer 5/100");
  
  TEST_ASSERT_FALSE(test_hostui_should_suppress_m117("Layer 6/100"));
}

// Test: M117 not suppressed for empty cache
TEST_CASE(host_actions, m117_not_suppressed_when_cache_empty) {
  test_hostui_clear_notification_cache();
  
  TEST_ASSERT_FALSE(test_hostui_should_suppress_m117("Some message"));
}

// Test: Special characters in notifications
TEST_CASE(host_actions, handles_special_characters) {
  test_hostui_clear_notification_cache();
  
  const char* special_msg = "Print: 50% @ 200°C";
  test_hostui_store_notification(special_msg);
  
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification(special_msg));
}

// Test: Sequential different notifications
TEST_CASE(host_actions, sequential_different_notifications) {
  test_hostui_clear_notification_cache();
  
  test_hostui_store_notification("Message 1");
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification("Message 1"));
  
  test_hostui_store_notification("Message 2");
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification("Message 2"));
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification("Message 1"));
  
  test_hostui_store_notification("Message 3");
  TEST_ASSERT_TRUE(test_hostui_is_duplicate_notification("Message 3"));
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification("Message 2"));
  TEST_ASSERT_FALSE(test_hostui_is_duplicate_notification("Message 1"));
}

// Test: Rapid duplicate suppression scenario
TEST_CASE(host_actions, rapid_duplicate_suppression) {
  test_hostui_clear_notification_cache();
  
  // Simulate a notification being sent multiple times (e.g., from slicer progress updates)
  test_hostui_store_notification("Layer 10");
  
  // First duplicate check - should suppress
  TEST_ASSERT_TRUE(test_hostui_should_suppress_m117("Layer 10"));
  
  // Second duplicate check - still should suppress
  TEST_ASSERT_TRUE(test_hostui_should_suppress_m117("Layer 10"));
  
  // Different layer - should not suppress
  TEST_ASSERT_FALSE(test_hostui_should_suppress_m117("Layer 11"));
}

void setUp(void) {
  test_hostui_clear_notification_cache();
}

void tearDown(void) {}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  
  RUN_TEST(test_host_actions_first_notification_not_duplicate);
  RUN_TEST(test_host_actions_detects_duplicate_notification);
  RUN_TEST(test_host_actions_different_notification_not_duplicate);
  RUN_TEST(test_host_actions_cache_updates_with_new_notification);
  RUN_TEST(test_host_actions_handles_empty_string);
  RUN_TEST(test_host_actions_handles_null_pointer);
  RUN_TEST(test_host_actions_cache_clearing_works);
  RUN_TEST(test_host_actions_long_message_truncation);
  RUN_TEST(test_host_actions_case_sensitive_comparison);
  RUN_TEST(test_host_actions_whitespace_matters);
  RUN_TEST(test_host_actions_m117_suppressed_for_duplicate);
  RUN_TEST(test_host_actions_m117_not_suppressed_for_new);
  RUN_TEST(test_host_actions_m117_not_suppressed_when_cache_empty);
  RUN_TEST(test_host_actions_handles_special_characters);
  RUN_TEST(test_host_actions_sequential_different_notifications);
  RUN_TEST(test_host_actions_rapid_duplicate_suppression);
  
  return UNITY_END();
}
