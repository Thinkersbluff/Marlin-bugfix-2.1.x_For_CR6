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
 * test_queue.cpp - Unit tests for the G-code command queue (ring buffer)
 *
 * Tests cover:
 * - Basic enqueue/dequeue operations
 * - Ring buffer wraparound
 * - Full and empty state detection
 * - Command preservation across wraparound
 */

#include <unity.h>
#include <cstring>
#include <cstdio>

// Define minimal configuration needed for queue tests
#define BUFSIZE 8
#define MAX_CMD_SIZE 96
#define HAS_MULTI_SERIAL 0

// Forward declare the types we need from queue.h without including full Marlin config
namespace GCodeQueue_Test {
  struct CommandLine {
    char buffer[MAX_CMD_SIZE];
    bool skip_ok;
  };

  struct RingBuffer {
    uint8_t length, index_r, index_w;
    CommandLine commands[BUFSIZE];

    inline void clear() { length = index_r = index_w = 0; }

    void advance_pos(uint8_t &p, const int inc) { 
      if (++p >= BUFSIZE) p = 0; 
      length += inc; 
    }
    
    inline void advance_w() { advance_pos(index_w, 1); }
    inline void advance_r() { if (length) advance_pos(index_r, -1); }

    void commit_command(const bool skip_ok) {
      commands[index_w].skip_ok = skip_ok;
      advance_w();
    }

    bool enqueue(const char *cmd, const bool skip_ok=true) {
      if (*cmd == ';' || length >= BUFSIZE) return false;
      strcpy(commands[index_w].buffer, cmd);
      commit_command(skip_ok);
      return true;
    }

    inline bool full(uint8_t cmdCount=1) const { return length > (BUFSIZE - cmdCount); }
    inline bool occupied() const { return length != 0; }
    inline bool empty() const { return !occupied(); }
    inline CommandLine& peek_next_command() { return commands[index_r]; }
    inline char* peek_next_command_string() { return peek_next_command().buffer; }
  };
}

using namespace GCodeQueue_Test;

// Unity test registration macros for standalone tests
#define TEST_CASE(suite, name) void test_##suite##_##name(void)
#define RUN_TEST_CASE(func) RUN_TEST(func)

// Test: Queue starts empty
TEST_CASE(queue, starts_empty) {
  RingBuffer rb;
  rb.clear();
  TEST_ASSERT_TRUE(rb.empty());
  TEST_ASSERT_FALSE(rb.occupied());
  TEST_ASSERT_EQUAL(0, rb.length);
}

// Test: Can enqueue a single command
TEST_CASE(queue, enqueue_single_command) {
  RingBuffer rb;
  rb.clear();
  
  bool result = rb.enqueue("G28", false);
  
  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_TRUE(rb.occupied());
  TEST_ASSERT_FALSE(rb.empty());
  TEST_ASSERT_EQUAL(1, rb.length);
  TEST_ASSERT_EQUAL_STRING("G28", rb.peek_next_command_string());
}

// Test: FIFO order is preserved
TEST_CASE(queue, fifo_order) {
  RingBuffer rb;
  rb.clear();
  
  rb.enqueue("G28", false);
  rb.enqueue("G1 X10", false);
  rb.enqueue("M104 S200", false);
  
  TEST_ASSERT_EQUAL(3, rb.length);
  TEST_ASSERT_EQUAL_STRING("G28", rb.peek_next_command_string());
  
  rb.advance_r();
  TEST_ASSERT_EQUAL_STRING("G1 X10", rb.peek_next_command_string());
  
  rb.advance_r();
  TEST_ASSERT_EQUAL_STRING("M104 S200", rb.peek_next_command_string());
}

// Test: Queue detects full condition
TEST_CASE(queue, detects_full) {
  RingBuffer rb;
  rb.clear();
  
  // Fill the buffer
  for (int i = 0; i < BUFSIZE; i++) {
    char cmd[20];
    snprintf(cmd, sizeof(cmd), "G1 X%d", i);
    bool result = rb.enqueue(cmd, false);
    TEST_ASSERT_TRUE(result);
  }
  
  TEST_ASSERT_EQUAL(BUFSIZE, rb.length);
  TEST_ASSERT_TRUE(rb.full());
  
  // Try to enqueue one more - should fail
  bool overflow = rb.enqueue("G28", false);
  TEST_ASSERT_FALSE(overflow);
}

// Test: Ring buffer wraps around correctly
TEST_CASE(queue, wraparound) {
  RingBuffer rb;
  rb.clear();
  
  // Fill buffer completely
  for (int i = 0; i < BUFSIZE; i++) {
    char cmd[20];
    snprintf(cmd, sizeof(cmd), "CMD_%d", i);
    rb.enqueue(cmd, false);
  }
  
  // Remove half the commands
  const int half = BUFSIZE / 2;
  for (int i = 0; i < half; i++) {
    rb.advance_r();
  }
  
  TEST_ASSERT_EQUAL(BUFSIZE - half, rb.length);
  
  // Add new commands (should wrap around)
  for (int i = 0; i < half; i++) {
    char cmd[20];
    snprintf(cmd, sizeof(cmd), "NEW_%d", i);
    bool result = rb.enqueue(cmd, false);
    TEST_ASSERT_TRUE(result);
  }
  
  TEST_ASSERT_EQUAL(BUFSIZE, rb.length);
  TEST_ASSERT_TRUE(rb.full());
  
  // Verify order: should see remaining original commands first
  for (int i = half; i < BUFSIZE; i++) {
    char expected[20];
    snprintf(expected, sizeof(expected), "CMD_%d", i);
    TEST_ASSERT_EQUAL_STRING(expected, rb.peek_next_command_string());
    rb.advance_r();
  }
  
  // Then the wrapped-around new commands
  for (int i = 0; i < half; i++) {
    char expected[20];
    snprintf(expected, sizeof(expected), "NEW_%d", i);
    TEST_ASSERT_EQUAL_STRING(expected, rb.peek_next_command_string());
    rb.advance_r();
  }
  
  TEST_ASSERT_TRUE(rb.empty());
}

// Test: Clear empties the queue
TEST_CASE(queue, clear_empties) {
  RingBuffer rb;
  rb.clear();
  
  rb.enqueue("G28", false);
  rb.enqueue("G1 X10", false);
  rb.enqueue("M104 S200", false);
  
  TEST_ASSERT_EQUAL(3, rb.length);
  
  rb.clear();
  
  TEST_ASSERT_TRUE(rb.empty());
  TEST_ASSERT_EQUAL(0, rb.length);
  TEST_ASSERT_EQUAL(0, rb.index_r);
  TEST_ASSERT_EQUAL(0, rb.index_w);
}

// Test: Empty queue advance_r doesn't underflow
TEST_CASE(queue, advance_r_on_empty) {
  RingBuffer rb;
  rb.clear();
  
  TEST_ASSERT_TRUE(rb.empty());
  
  // advance_r on empty should be safe (no-op)
  rb.advance_r();
  
  TEST_ASSERT_EQUAL(0, rb.length);
  TEST_ASSERT_TRUE(rb.empty());
}

// Test: Commands can contain special characters
TEST_CASE(queue, special_characters) {
  RingBuffer rb;
  rb.clear();
  
  const char *cmd_with_comment = "G28 ; home all axes";
  const char *cmd_with_string = "M117 Hello World!";
  
  rb.enqueue(cmd_with_comment, false);
  rb.enqueue(cmd_with_string, false);
  
  TEST_ASSERT_EQUAL_STRING(cmd_with_comment, rb.peek_next_command_string());
  rb.advance_r();
  TEST_ASSERT_EQUAL_STRING(cmd_with_string, rb.peek_next_command_string());
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_queue_starts_empty);
  RUN_TEST(test_queue_enqueue_single_command);
  RUN_TEST(test_queue_fifo_order);
  RUN_TEST(test_queue_detects_full);
  RUN_TEST(test_queue_wraparound);
  RUN_TEST(test_queue_clear_empties);
  RUN_TEST(test_queue_advance_r_on_empty);
  RUN_TEST(test_queue_special_characters);
  return UNITY_END();
}
