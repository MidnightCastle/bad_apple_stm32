/**
 * @file    audio_dac.h
 * @brief   Stereo DAC audio driver with circular DMA
 * @author  David Leathers
 * @date    November 2025
 * 
 * Architecture:
 *   - Dual DAC channels (PA4 = left, PA5 = right) driven by TIM6 trigger
 *   - Circular DMA with half-transfer interrupts for double buffering
 *   - LEFT channel is master for timing (triggers refill requests, updates sync)
 *   - RIGHT channel follows LEFT (filled at same time from same source)
 * 
 * Buffer Layout (per channel):
 *   [---- First Half (2048 samples) ----][---- Second Half (2048 samples) ----]
 *   
 *   While DMA plays first half, main loop fills second half, and vice versa.
 * 
 * Usage:
 *   1. audio_Init() with DAC and timer handles
 *   2. audio_SetAVSync() to link synchronization
 *   3. Pre-fill both buffer halves with audio_GetBuffer() + your fill function
 *   4. audio_Start() to begin playback
 *   5. In main loop: check audio_NeedsRefill(), fill buffer, call audio_BufferFilled()
 *   6. audio_Stop() when done
 */

#ifndef AUDIO_DAC_H
#define AUDIO_DAC_H

#include "stm32l4xx_hal.h"
#include "buffers.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================== Configuration ========================== */

// Buffer sizes derived from buffers.h
#define AUDIO_HALF_BUFFER_SAMPLES   AUDIO_BUFFER_SAMPLES        // 2048 - samples per half
#define AUDIO_FULL_BUFFER_SAMPLES   (AUDIO_BUFFER_SAMPLES * 2)  // 4096 - total circular buffer

// DAC output for silence (12-bit midpoint)
#define AUDIO_DAC_SILENCE           2048

/* ========================== Types ========================== */

typedef enum {
    AUDIO_STATE_RESET = 0,  // Not initialized
    AUDIO_STATE_READY,      // Initialized, not playing
    AUDIO_STATE_PLAYING,    // DMA active, audio playing
    AUDIO_STATE_ERROR       // Error occurred
} Audio_State;

typedef enum {
    AUDIO_OK = 0,
    AUDIO_ERROR
} Audio_Status;

typedef enum {
    AUDIO_BUFFER_FIRST_HALF = 0,    // Fill samples 0 to HALF_BUFFER_SAMPLES-1
    AUDIO_BUFFER_SECOND_HALF        // Fill samples HALF_BUFFER_SAMPLES to end
} Audio_BufferHalf;

// Audio statistics
typedef struct {
    uint32_t samples_played;    // Total samples output
    uint32_t refill_count;      // Times buffer was refilled
    uint32_t underrun_count;    // Times buffer wasn't ready in time
} Audio_Stats;

// Forward declaration
struct AVSync_Handle;

typedef struct {
    // HAL handles (not owned, must outlive this struct)
    DAC_HandleTypeDef *hdac;
    TIM_HandleTypeDef *htim;
    
    // A/V sync handle (optional)
    struct AVSync_Handle *avsync;
    
    // Buffer state - LEFT channel is master, RIGHT follows
    volatile bool needs_refill;             // Set by ISR when buffer half consumed
    volatile Audio_BufferHalf fill_half;    // Which half needs filling
    
    // Playback state
    Audio_State state;
    Audio_Stats stats;
    
    // Init flag
    bool initialized;
} Audio_Handle;

/* ========================== Core API ========================== */

/**
 * @brief Initialize audio driver
 * @param audio Handle to initialize
 * @param hdac  HAL DAC handle (must be configured for dual channel)
 * @param htim  HAL timer handle (must be configured for DAC trigger)
 * @return AUDIO_OK on success
 */
Audio_Status audio_Init(Audio_Handle *audio, DAC_HandleTypeDef *hdac, TIM_HandleTypeDef *htim);

/**
 * @brief Link A/V synchronization
 * @param audio Handle
 * @param sync  AVSync handle to update from audio ISR
 */
void audio_SetAVSync(Audio_Handle *audio, struct AVSync_Handle *sync);

/**
 * @brief Start audio playback
 * @param audio Handle
 * @return AUDIO_OK on success
 * @note  Pre-fill both buffer halves before calling this!
 */
Audio_Status audio_Start(Audio_Handle *audio);

/**
 * @brief Stop audio playback
 * @param audio Handle
 */
void audio_Stop(Audio_Handle *audio);

/* ========================== Buffer Management ========================== */

/**
 * @brief Check if audio buffer needs refilling
 * @param audio Handle
 * @return true if main loop should fill buffer
 */
bool audio_NeedsRefill(Audio_Handle *audio);

/**
 * @brief Get which buffer half needs filling
 * @param audio Handle
 * @return AUDIO_BUFFER_FIRST_HALF or AUDIO_BUFFER_SECOND_HALF
 */
Audio_BufferHalf audio_GetFillHalf(Audio_Handle *audio);

/**
 * @brief Get pointer to left channel DMA buffer
 * @param audio Handle
 * @return Pointer to start of left channel buffer (4096 samples)
 */
uint16_t* audio_GetLeftBuffer(Audio_Handle *audio);

/**
 * @brief Get pointer to right channel DMA buffer
 * @param audio Handle
 * @return Pointer to start of right channel buffer (4096 samples)
 */
uint16_t* audio_GetRightBuffer(Audio_Handle *audio);

/**
 * @brief Mark buffer as filled (clear refill flag)
 * @param audio Handle
 * @note  Call after filling BOTH left and right channel halves
 */
void audio_BufferFilled(Audio_Handle *audio);

/* ========================== Statistics ========================== */

/**
 * @brief Get audio statistics
 * @param audio Handle
 * @return Pointer to stats (valid while handle exists)
 */
static inline const Audio_Stats* audio_GetStats(const Audio_Handle *audio) {
    return audio ? &audio->stats : NULL;
}

#endif // AUDIO_DAC_H
