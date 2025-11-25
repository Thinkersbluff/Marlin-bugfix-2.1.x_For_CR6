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
 * test_sd_mount.cpp - Unit tests for SD card mount/detect logic
 *
 * Tests the SD card state machine that handles:
 * - Media insertion detection
 * - Mount/unmount operations
 * - State transitions (boot -> inserted -> mounted -> removed)
 * - Edge cases: repeated removal messages, UI state tracking
 *
 * Key functionality tested:
 * - manage_media() state detection (MediaPresence enum)
 * - mount() operation success/failure
 * - isMounted() vs isInserted() distinction
 * - Media removed during print (abort handling)
 * - Repeated "Media Removed" dialog edge case
 */

#include <unity.h>
#include <cstring>
#include <cstdint>

// Unity test registration macros for standalone tests
#define TEST_CASE(suite, name) void test_##suite##_##name(void)

// MediaPresence enum from cardreader.h
enum MediaPresence : int8_t {
  MEDIA_BOOT    = -1,   // At boot we don't know if media is present
  INSERT_NONE   = 0x00, // No media detected
  INSERT_MEDIA  = 0x01, // Generic media detected (single volume)
  INSERT_SD     = 0x02, // SD card detected (multi-volume)
  INSERT_USB    = 0x04  // USB flash drive detected (multi-volume)
};

// Card flags structure
struct CardFlags {
  bool mounted;           // Media is mounted and ready
  bool sdprinting;        // Actively printing from media
  bool abort_sd_printing; // Abort flag set
  bool pending_print_start; // Print start requested but not begun
  
  void reset() {
    mounted = false;
    sdprinting = false;
    abort_sd_printing = false;
    pending_print_start = false;
  }
};

// Mock card reader state
struct MockCardReader {
  CardFlags flags;
  MediaPresence insertion_state;  // Physical detection state
  bool mount_will_succeed;        // Controls whether mount() succeeds
  bool ui_detected;               // Whether UI is available
  int mount_call_count;
  int release_call_count;
  int abort_call_count;
  
  void reset() {
    flags.reset();
    insertion_state = INSERT_NONE;
    mount_will_succeed = true;
    ui_detected = true;
    mount_call_count = 0;
    release_call_count = 0;
    abort_call_count = 0;
  }
  
  // Core detection function
  bool isInserted() const {
    return insertion_state != INSERT_NONE;
  }
  
  bool isMounted() const {
    return flags.mounted;
  }
  
  // Mount operation
  void mount() {
    mount_call_count++;
    if (mount_will_succeed) {
      flags.mounted = true;
    }
    else {
      flags.mounted = false;
    }
  }
  
  // Release operation
  void release() {
    release_call_count++;
    
    // If printing or pending, abort first
    if (flags.sdprinting || flags.pending_print_start) {
      abortFilePrint();
    }
    
    flags.mounted = false;
  }
  
  void abortFilePrint() {
    abort_call_count++;
    flags.abort_sd_printing = true;
    flags.sdprinting = false;
    flags.pending_print_start = false;
  }
};

static MockCardReader mock_card;

// Mock UI state tracker
struct MockUI {
  MediaPresence last_old_status;
  MediaPresence last_new_status;
  int media_changed_calls;
  int message_count;
  bool last_message_was_removed;
  
  void reset() {
    last_old_status = MEDIA_BOOT;
    last_new_status = MEDIA_BOOT;
    media_changed_calls = 0;
    message_count = 0;
    last_message_was_removed = false;
  }
  
  void media_changed(MediaPresence old_status, MediaPresence new_status) {
    media_changed_calls++;
    last_old_status = old_status;
    last_new_status = new_status;
    
    // Track if "Media Removed" message would be shown
    if (old_status > MEDIA_BOOT && new_status < old_status) {
      message_count++;
      last_message_was_removed = true;
    }
    else {
      last_message_was_removed = false;
    }
  }
  
  bool detected() const {
    return mock_card.ui_detected;
  }
};

static MockUI mock_ui;

// Simplified manage_media() logic focusing on state transitions
static MediaPresence prev_stat = MEDIA_BOOT;

static void manage_media_simplified() {
  // Get current insertion state
  MediaPresence stat = mock_card.insertion_state;
  
  // No change? Nothing to do
  if (stat == prev_stat) return;
  
  // UI not detected? Skip
  if (!mock_ui.detected()) return;
  
  MediaPresence old_stat = prev_stat;
  MediaPresence old_real = (old_stat == MEDIA_BOOT) ? INSERT_NONE : old_stat;
  prev_stat = stat;  // Update before operations
  
  bool did_insert = (stat != INSERT_NONE) && (stat > old_real);
  
  if (did_insert) {
    // Media inserted
    if (!mock_card.isMounted() && old_stat > MEDIA_BOOT) {
      mock_card.mount();
    }
    
    // If mount failed, revert stat
    if (!mock_card.isMounted()) {
      stat = old_real;
    }
  }
  else if (stat < old_real) {
    // Media removed
    mock_card.release();
  }
  
  // Notify UI
  mock_ui.media_changed(old_stat, stat);
}

// Reset function for each test
void setUp(void) {
  mock_card.reset();
  mock_ui.reset();
  prev_stat = MEDIA_BOOT;
}

void tearDown(void) {}

//
// State Transition Tests
//

// Test: Boot state with no media
TEST_CASE(sd_mount, boot_no_media) {
  mock_card.insertion_state = INSERT_NONE;
  
  manage_media_simplified();
  
  TEST_ASSERT_FALSE(mock_card.isMounted());
  TEST_ASSERT_EQUAL(0, mock_card.mount_call_count);
  TEST_ASSERT_EQUAL(1, mock_ui.media_changed_calls);
}

// Test: Boot state with media already inserted
TEST_CASE(sd_mount, boot_with_media_inserted) {
  mock_card.insertion_state = INSERT_MEDIA;
  
  manage_media_simplified();
  
  // At boot (MEDIA_BOOT -> INSERT_MEDIA), mount should NOT be called
  TEST_ASSERT_FALSE(mock_card.isMounted());
  TEST_ASSERT_EQUAL(0, mock_card.mount_call_count);
}

// Test: Media insertion after boot
TEST_CASE(sd_mount, insert_after_boot) {
  // First call: boot with no media
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  // Second call: media inserted
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  TEST_ASSERT_TRUE(mock_card.isMounted());
  TEST_ASSERT_EQUAL(1, mock_card.mount_call_count);
  TEST_ASSERT_EQUAL(2, mock_ui.media_changed_calls);
}

// Test: Successful mount sets mounted flag
TEST_CASE(sd_mount, successful_mount_sets_flag) {
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  mock_card.mount_will_succeed = true;
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  TEST_ASSERT_TRUE(mock_card.isMounted());
  TEST_ASSERT_TRUE(mock_card.flags.mounted);
}

// Test: Failed mount does not set mounted flag
TEST_CASE(sd_mount, failed_mount_no_flag) {
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  mock_card.mount_will_succeed = false;
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  TEST_ASSERT_FALSE(mock_card.isMounted());
  TEST_ASSERT_FALSE(mock_card.flags.mounted);
  TEST_ASSERT_EQUAL(1, mock_card.mount_call_count);
}

// Test: isInserted() true does not mean isMounted() true
TEST_CASE(sd_mount, inserted_not_same_as_mounted) {
  mock_card.insertion_state = INSERT_MEDIA;
  mock_card.mount_will_succeed = false;
  
  manage_media_simplified(); // Boot
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified(); // Try to mount
  
  TEST_ASSERT_TRUE(mock_card.isInserted());
  TEST_ASSERT_FALSE(mock_card.isMounted());
}

//
// Media Removal Tests
//

// Test: Media removed calls release()
TEST_CASE(sd_mount, removal_calls_release) {
  // Insert and mount
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  TEST_ASSERT_TRUE(mock_card.isMounted());
  
  // Remove media
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  TEST_ASSERT_FALSE(mock_card.isMounted());
  TEST_ASSERT_EQUAL(1, mock_card.release_call_count);
}

// Test: Media removed triggers UI notification
TEST_CASE(sd_mount, removal_triggers_ui_message) {
  // Insert and mount
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  mock_ui.reset();
  
  // Remove media
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  TEST_ASSERT_EQUAL(1, mock_ui.media_changed_calls);
  TEST_ASSERT_TRUE(mock_ui.last_message_was_removed);
}

// Test: Repeated removal does not trigger duplicate messages
TEST_CASE(sd_mount, repeated_removal_no_duplicate) {
  // Insert and mount
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  // Remove media
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  int first_message_count = mock_ui.message_count;
  
  // Call again with media still removed
  manage_media_simplified();
  manage_media_simplified();
  
  // Should not generate additional messages
  TEST_ASSERT_EQUAL(first_message_count, mock_ui.message_count);
}

//
// Print Abort Tests
//

// Test: Media removed during print aborts
TEST_CASE(sd_mount, removal_during_print_aborts) {
  // Insert, mount, start "printing"
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  mock_card.flags.sdprinting = true;
  
  // Remove media
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  TEST_ASSERT_EQUAL(1, mock_card.abort_call_count);
  TEST_ASSERT_TRUE(mock_card.flags.abort_sd_printing);
  TEST_ASSERT_FALSE(mock_card.flags.sdprinting);
}

// Test: Media removed with pending print start aborts
TEST_CASE(sd_mount, removal_with_pending_start_aborts) {
  // Insert, mount, pending start
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  mock_card.flags.pending_print_start = true;
  
  // Remove media
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  TEST_ASSERT_EQUAL(1, mock_card.abort_call_count);
  TEST_ASSERT_FALSE(mock_card.flags.pending_print_start);
}

// Test: Media removed when not printing does not abort
TEST_CASE(sd_mount, removal_idle_no_abort) {
  // Insert and mount (not printing)
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  // Remove media
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  TEST_ASSERT_EQUAL(0, mock_card.abort_call_count);
}

//
// No UI Tests
//

// Test: No UI detected skips all operations
TEST_CASE(sd_mount, no_ui_skips_operations) {
  mock_card.ui_detected = false;
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  // Should not mount or call UI
  TEST_ASSERT_FALSE(mock_card.isMounted());
  TEST_ASSERT_EQUAL(0, mock_card.mount_call_count);
  TEST_ASSERT_EQUAL(0, mock_ui.media_changed_calls);
}

//
// State Consistency Tests
//

// Test: Multiple inserts only mount once
TEST_CASE(sd_mount, multiple_inserts_mount_once) {
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  // Try "inserting" again (shouldn't happen, but test robustness)
  manage_media_simplified();
  manage_media_simplified();
  
  TEST_ASSERT_EQUAL(1, mock_card.mount_call_count);
}

// Test: Mount after failed mount on retry
TEST_CASE(sd_mount, retry_mount_after_failure) {
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  // First attempt fails
  mock_card.mount_will_succeed = false;
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  TEST_ASSERT_FALSE(mock_card.isMounted());
  
  // Remove and reinsert with success
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  mock_card.mount_will_succeed = true;
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  TEST_ASSERT_TRUE(mock_card.isMounted());
  TEST_ASSERT_EQUAL(2, mock_card.mount_call_count);
}

// Test: State remains consistent after release
TEST_CASE(sd_mount, state_consistent_after_release) {
  // Insert and mount
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  // Remove
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  // Verify clean state
  TEST_ASSERT_FALSE(mock_card.isMounted());
  TEST_ASSERT_FALSE(mock_card.isInserted());
  TEST_ASSERT_FALSE(mock_card.flags.sdprinting);
  TEST_ASSERT_FALSE(mock_card.flags.pending_print_start);
}

//
// Edge Case Tests
//

// Test: Boot -> Insert -> Remove -> Insert sequence
TEST_CASE(sd_mount, boot_insert_remove_insert_sequence) {
  // Boot with no media
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  // Insert
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  TEST_ASSERT_TRUE(mock_card.isMounted());
  
  // Remove
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  TEST_ASSERT_FALSE(mock_card.isMounted());
  
  // Insert again
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  TEST_ASSERT_TRUE(mock_card.isMounted());
  
  TEST_ASSERT_EQUAL(2, mock_card.mount_call_count);
  TEST_ASSERT_EQUAL(1, mock_card.release_call_count);
}

// Test: Rapid insert/remove cycles
TEST_CASE(sd_mount, rapid_insert_remove_cycles) {
  mock_card.insertion_state = INSERT_NONE;
  manage_media_simplified();
  
  for (int i = 0; i < 5; i++) {
    mock_card.insertion_state = INSERT_MEDIA;
    manage_media_simplified();
    TEST_ASSERT_TRUE(mock_card.isMounted());
    
    mock_card.insertion_state = INSERT_NONE;
    manage_media_simplified();
    TEST_ASSERT_FALSE(mock_card.isMounted());
  }
  
  TEST_ASSERT_EQUAL(5, mock_card.mount_call_count);
  TEST_ASSERT_EQUAL(5, mock_card.release_call_count);
}

// Test: UI media_changed not called for no-op transitions
TEST_CASE(sd_mount, ui_not_called_for_no_change) {
  mock_card.insertion_state = INSERT_MEDIA;
  manage_media_simplified();
  
  int initial_calls = mock_ui.media_changed_calls;
  
  // Call again with same state
  manage_media_simplified();
  manage_media_simplified();
  
  // Should not trigger additional UI calls
  TEST_ASSERT_EQUAL(initial_calls, mock_ui.media_changed_calls);
}

// Test: MediaPresence enum values are distinct
TEST_CASE(sd_mount, media_presence_values_distinct) {
  TEST_ASSERT_NOT_EQUAL(MEDIA_BOOT, INSERT_NONE);
  TEST_ASSERT_NOT_EQUAL(INSERT_NONE, INSERT_MEDIA);
  TEST_ASSERT_NOT_EQUAL(INSERT_MEDIA, INSERT_SD);
  TEST_ASSERT_NOT_EQUAL(INSERT_SD, INSERT_USB);
}

// Test: Abort clears all print flags
TEST_CASE(sd_mount, abort_clears_all_flags) {
  mock_card.flags.sdprinting = true;
  mock_card.flags.pending_print_start = true;
  mock_card.flags.abort_sd_printing = false;
  
  mock_card.abortFilePrint();
  
  TEST_ASSERT_FALSE(mock_card.flags.sdprinting);
  TEST_ASSERT_FALSE(mock_card.flags.pending_print_start);
  TEST_ASSERT_TRUE(mock_card.flags.abort_sd_printing);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  
  // State transition tests
  RUN_TEST(test_sd_mount_boot_no_media);
  RUN_TEST(test_sd_mount_boot_with_media_inserted);
  RUN_TEST(test_sd_mount_insert_after_boot);
  RUN_TEST(test_sd_mount_successful_mount_sets_flag);
  RUN_TEST(test_sd_mount_failed_mount_no_flag);
  RUN_TEST(test_sd_mount_inserted_not_same_as_mounted);
  
  // Media removal tests
  RUN_TEST(test_sd_mount_removal_calls_release);
  RUN_TEST(test_sd_mount_removal_triggers_ui_message);
  RUN_TEST(test_sd_mount_repeated_removal_no_duplicate);
  
  // Print abort tests
  RUN_TEST(test_sd_mount_removal_during_print_aborts);
  RUN_TEST(test_sd_mount_removal_with_pending_start_aborts);
  RUN_TEST(test_sd_mount_removal_idle_no_abort);
  
  // No UI tests
  RUN_TEST(test_sd_mount_no_ui_skips_operations);
  
  // State consistency tests
  RUN_TEST(test_sd_mount_multiple_inserts_mount_once);
  RUN_TEST(test_sd_mount_retry_mount_after_failure);
  RUN_TEST(test_sd_mount_state_consistent_after_release);
  
  // Edge case tests
  RUN_TEST(test_sd_mount_boot_insert_remove_insert_sequence);
  RUN_TEST(test_sd_mount_rapid_insert_remove_cycles);
  RUN_TEST(test_sd_mount_ui_not_called_for_no_change);
  RUN_TEST(test_sd_mount_media_presence_values_distinct);
  RUN_TEST(test_sd_mount_abort_clears_all_flags);
  
  return UNITY_END();
}
