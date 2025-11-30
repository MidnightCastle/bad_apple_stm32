/**
 * @file    perf.c
 * @brief   DWT cycle counter implementation
 * @author  David Leathers
 * @date    November 2025
 */

#include "perf.h"

// Internal state - not exposed in header
static volatile bool s_initialized = false;

void Perf_Init(void) {
    if (s_initialized) return;
    
    // Enable DWT access
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    
    // Reset and enable cycle counter
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    
    s_initialized = true;
}

bool Perf_IsInitialized(void) {
    return s_initialized;
}
