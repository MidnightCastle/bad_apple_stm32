/**
 * @file    av_sync.c
 * @brief   Audio-Video synchronization implementation
 * @author  David Leathers
 * @date    November 2025
 */

#include "av_sync.h"
#include <string.h>

void AVSync_Init(AVSync_Handle *sync, uint32_t sample_rate, 
                 uint32_t video_fps, uint32_t max_drift) {
    if (!sync || sample_rate == 0 || video_fps == 0) return;
    
    // Clear everything
    memset(sync, 0, sizeof(AVSync_Handle));
    
    // Configuration
    sync->audio_sample_rate = sample_rate;
    sync->video_fps = video_fps;
    sync->samples_per_frame = sample_rate / video_fps;
    sync->max_drift_frames = (max_drift > 0) ? max_drift : AVSYNC_DEFAULT_MAX_DRIFT;
    
    // Initial state
    sync->state = AVSYNC_STATE_READY;
    sync->initialized = true;
}

void AVSync_Start(AVSync_Handle *sync) {
    if (!sync || !sync->initialized) return;
    
    // Reset playback counters
    sync->audio_samples_played = 0;
    sync->video_frames_rendered = 0;
    
    // Reset statistics
    memset(&sync->stats, 0, sizeof(AVSync_Stats));
    
    sync->state = AVSYNC_STATE_RUNNING;
}

void AVSync_Stop(AVSync_Handle *sync) {
    if (!sync) return;
    sync->state = AVSYNC_STATE_STOPPED;
}

void AVSync_AudioTick(AVSync_Handle *sync, uint32_t samples) {
    if (!sync || sync->state != AVSYNC_STATE_RUNNING) return;
    sync->audio_samples_played += samples;
}

AVSync_Decision AVSync_GetFrameDecision(AVSync_Handle *sync) {
    if (!sync || sync->state != AVSYNC_STATE_RUNNING) {
        return AVSYNC_NOT_STARTED;
    }
    
    // Calculate expected video frame from audio position
    uint32_t audio_frame = sync->audio_samples_played / sync->samples_per_frame;
    uint32_t video_frame = sync->video_frames_rendered;
    
    // Drift: positive = video ahead, negative = video behind
    int32_t drift = (int32_t)video_frame - (int32_t)audio_frame;
    
    // Update drift statistics
    if (drift > sync->stats.max_drift) sync->stats.max_drift = drift;
    if (drift < sync->stats.min_drift) sync->stats.min_drift = drift;
    
    // Decide action based on drift
    if (drift < -(int32_t)sync->max_drift_frames) {
        // Video behind audio - skip frame to catch up
        return AVSYNC_SKIP_FRAME;
    } else if (drift > (int32_t)sync->max_drift_frames) {
        // Video ahead of audio - wait
        return AVSYNC_REPEAT_FRAME;
    } else {
        // In sync - render normally
        return AVSYNC_RENDER_FRAME;
    }
}

void AVSync_FrameRendered(AVSync_Handle *sync) {
    if (!sync) return;
    sync->video_frames_rendered++;
}

void AVSync_FrameSkipped(AVSync_Handle *sync) {
    if (!sync) return;
    sync->video_frames_rendered++;
    sync->stats.frames_skipped++;
}

uint32_t AVSync_GetCurrentFrame(const AVSync_Handle *sync) {
    if (!sync || sync->samples_per_frame == 0) return 0;
    return sync->audio_samples_played / sync->samples_per_frame;
}

int32_t AVSync_GetCurrentDrift(const AVSync_Handle *sync) {
    if (!sync || sync->samples_per_frame == 0) return 0;
    
    uint32_t audio_frame = sync->audio_samples_played / sync->samples_per_frame;
    return (int32_t)sync->video_frames_rendered - (int32_t)audio_frame;
}
