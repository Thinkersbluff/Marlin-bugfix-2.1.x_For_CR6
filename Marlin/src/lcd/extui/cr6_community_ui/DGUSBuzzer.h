/**
 * Lightweight adapter header to expose a simple DGUS buzzer function
 * without pulling in the full DGUS screen handler headers.
 */
#pragma once

#include "../../../inc/MarlinConfig.h"

#if ENABLED(DGUS_LCD_UI_CR6_COMM)
// Simple forward declaration; implementation is provided in DGUSScreenHandler.cpp
void DGUS_Buzzer(const uint16_t duration, const uint16_t frequency = 0);
#endif
