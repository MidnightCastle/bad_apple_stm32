/**
 * @file    av_sync.h
 * @brief   Audio-Video synchronization (audio-master clock)
 * @author  David Leathers
 * @date    November 2025
 * 
 * Synchronization Strategy:
 *   Audio playback is the master clock. Audio samples are counted as they
 *   play through DMA, and video frames are rendered/skipped/repeated to
 *   match the audio position.
 * 
 * Why Audio-Master:
 *   - Audio glitches are more perceptible than video frame drops
 *   - DAC DMA runs at fixed rate, can't speed up or slow down
 *   - Video can drop/repeat frames without major artifacts
 * 
 * Usage:
 *   1. AVSync_Init() with sample rate and FPS
 *   2. AVSync_Start() when playback begins
 *   3. AVSync_AudioTick() from audio DMA half-complete ISR
 *   4. AVSync_GetFrameDecision() in main loop to decide what to do
 *   5. AVSync_FrameRendered()/FrameSkipped() after handling frame
 */

#ifndef AV_SYNC_H
#define AV_SYNC_H

#include "stm32l4xx.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================== Configuration ========================== */

// Default maximum drift before corrective action (in frames)
#define AVSYNC_DEFAULT_MAX_DRIFT    2

/* ========================== Types ========================== */

typedef enum {
    AVSYNC_STATE_RESET = 0,     // Not initialized
    AVSYNC_STATE_READY,         // Initialized, not started
    AVSYNC_STATE_RUNNING,       // Actively syncing
    AVSYNC_STATE_STOPPED        // Playback ended
} AVSync_State;

typedef enum {
    AVSYNC_NOT_STARTED = 0,     // Sync not running, do nothing
    AVSYNC_RENDER_FRAME,        // Render the next frame normally
    AVSYNC_SKIP_FRAME,          // Video behind - skip frame to catch up
    AVSYNC_REPEAT_FRAME         // Video ahead - wait, don't render new frame
} AVSync_Decision;

// Synchronization statistics
typedef struct {
    uint32_t frames_skipped;    // Frames skipped (video was behind)
    uint32_t frames_repeated;   // Frames repeated (video was ahead)
    int32_t max_drift;          // Maximum observed drift (frames)
    int32_t min_drift;          // Minimum observed drift (frames)
} AVSync_Stats;

typedef struct AVSync_Handle {
    // Configuration (set at init, don't modify)
    uint32_t audio_sample_rate;     // e.g., 32000 Hz
    uint32_t video_fps;             // e.g., 30 FPS
    uint32_t samples_per_frame;     // Calculated: sample_rate / fps
    uint32_t max_drift_frames;      // Threshold for skip/repeat
    
    // Playback state
    AVSync_State state;
    volatile uint32_t audio_samples_played;  // Updated from ISR
    uint32_t video_frames_rendered;          // Includes skipped frames
    
    // Statistics
    AVSync_Stats stats;
    
    // Init flag
    bool initialized;
} AVSync_Handle;

/* ========================== Core API ========================== */

/**
 * @brief Initialize A/V sync with given parameters
 * @param sync       Handle to initialize
 * @param sample_rate Audio sample rate in Hz (e.g., 32000)
 * @param video_fps   Video frame rate (e.g., 30)
 * @param max_drift   Max drift in frames before correction (0 = use default)
 */
void AVSync_Init(AVSync_Handle *sync, uint32_t sample_rate, 
                 uint32_t video_fps, uint32_t max_drift);

/**
 * @brief Start synchronization (call when playback begins)
 * @param sync Handle
 */
void AVSync_Start(AVSync_Handle *sync);

/**
 * @brief Stop synchronization (call when playback ends)
 * @param sync Handle
 */
void AVSync_Stop(AVSync_Handle *sync);

/* ========================== ISR Interface ========================== */

/**
 * @brief Update audio sample count (call from audio DMA ISR)
 * @param sync    Handle
 * @param samples Number of samples just played
 * @note  Called from ISR context - keep fast!
 */
void AVSync_AudioTick(AVSync_Handle *sync, uint32_t samples);

/* ========================== Main Loop Interface ========================== */

/**
 * @brief Get sync decision for next frame
 * @param sync Handle
 * @return Decision: render, skip, repeat, or not started
 * 
 * Call this in main loop. Based on return value:
 *   AVSYNC_RENDER_FRAME  - Render next frame, call FrameRendered()
 *   AVSYNC_SKIP_FRAME    - Don't render, call FrameSkipped()
 *   AVSYNC_REPEAT_FRAME  - Show current frame again, wait
 *   AVSYNC_NOT_STARTED   - Sync not running
 */
AVSync_Decision AVSync_GetFrameDecision(AVSync_Handle *sync);

/**
 * @brief Mark that a frame was rendered
 * @param sync Handle
 */
void AVSync_FrameRendered(AVSync_Handle *sync);

/**
 * @brief Mark that a frame was skipped (to catch up)
 * @param sync Handle
 * @note  Functionally identical to FrameRendered - both advance frame count
 */
void AVSync_FrameSkipped(AVSync_Handle *sync);

/* ========================== Query Functions ========================== */

/**
 * @brief Get current frame number based on audio position
 * @param sync Handle
 * @return Frame number audio is currently at
 */
uint32_t AVSync_GetCurrentFrame(const AVSync_Handle *sync);

/**
 * @brief Get synchronization statistics
 * @param sync Handle
 * @return Pointer to stats structure (valid while sync handle exists)
 */
static inline const AVSync_Stats* AVSync_GetStats(const AVSync_Handle *sync) {
    return sync ? &sync->stats : NULL;
}

/**
 * @brief Get current drift (video position - audio position)
 * @param sync Handle
 * @return Drift in frames (positive = video ahead, negative = video behind)
 */
int32_t AVSync_GetCurrentDrift(const AVSync_Handle *sync);

#endif // AV_SYNC_H
