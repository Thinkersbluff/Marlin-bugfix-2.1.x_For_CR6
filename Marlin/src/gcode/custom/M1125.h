// Small helper API to allow other modules to detect when M1125 wants
// to suppress auto-starting the print job timer while a deterministic
// pause is active.
#pragma once

#include "../../inc/MarlinConfig.h"

// Enable suppression while M1125 owns the paused state.
void M1125_SuppressAutoJobTimer();

// Disable suppression when M1125 no longer needs it.
void M1125_ClearAutoJobTimerSuppress();

// Query whether suppression is active.
bool M1125_IsAutoJobTimerSuppressed();

// Query whether M1125 currently owns the paused state. Primarily intended
// for debug instrumentation. Define DEBUG_M1125_PAUSE_MOVE_LOGGING to enable
// runtime logging of blocking moves that occur while this pause is active.
bool M1125_IsPauseActive();

// Abort/clear any M1125 pause state (used when a print is cancelled/aborted)
// Clears timers, pending resume state and suppression flags so leftover
// M1125 timeout UI does not appear after a job cancel.
void M1125_AbortPause();
