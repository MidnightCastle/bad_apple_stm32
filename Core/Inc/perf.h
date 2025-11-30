/**
 * @file    perf.h
 * @brief   DWT cycle counter utilities for STM32L476RG
 * @author  David Leathers
 * @date    November 2025
 * 
 * Provides microsecond-accurate timing using the ARM DWT cycle counter.
 * Must call Perf_Init() before using any other functions.
 */

#ifndef PERF_H
#define PERF_H

#include "stm32l4xx.h"
#include <stdint.h>
#include <stdbool.h>

// CPU frequency - must match SystemClock_Config()
#define PERF_CPU_FREQ_MHZ   80
#define PERF_CPU_FREQ_KHZ   (PERF_CPU_FREQ_MHZ * 1000)

/**
 * @brief Initialize DWT cycle counter
 * @note  Safe to call multiple times - subsequent calls are no-ops
 */
void Perf_Init(void);

/**
 * @brief Check if performance counter is initialized
 * @return true if Perf_Init() has been called
 */
bool Perf_IsInitialized(void);

/**
 * @brief Get current cycle count
 * @return 32-bit cycle counter value (wraps every ~53 seconds at 80MHz)
 */
static inline uint32_t Perf_GetCycles(void) {
    return DWT->CYCCNT;
}

/**
 * @brief Convert cycles to microseconds
 * @param cycles Cycle count
 * @return Time in microseconds
 */
static inline uint32_t Perf_CyclesToMicros(uint32_t cycles) {
    return cycles / PERF_CPU_FREQ_MHZ;
}

/**
 * @brief Convert cycles to milliseconds
 * @param cycles Cycle count
 * @return Time in milliseconds
 */
static inline uint32_t Perf_CyclesToMillis(uint32_t cycles) {
    return cycles / PERF_CPU_FREQ_KHZ;
}

/**
 * @brief Busy-wait delay in microseconds
 * @param us Microseconds to wait
 * @note  Blocks CPU - use only for short delays
 */
static inline void Perf_DelayMicros(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t target = us * PERF_CPU_FREQ_MHZ;
    while ((DWT->CYCCNT - start) < target);
}

#endif // PERF_H
