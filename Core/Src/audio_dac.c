/**
 * @file    audio_dac.c
 * @brief   Stereo DAC audio driver implementation
 * @author  David Leathers
 * @date    November 2025
 */

#include "audio_dac.h"
#include "av_sync.h"
#include <string.h>

/* ========================== Private Data ========================== */

// Global handle for HAL callbacks (HAL doesn't provide context pointer)
static Audio_Handle *s_audio_handle = NULL;

// DMA buffers - 32-byte aligned for optimal DMA performance
static uint16_t s_dma_buffer_left[AUDIO_FULL_BUFFER_SAMPLES] __attribute__((aligned(32)));
static uint16_t s_dma_buffer_right[AUDIO_FULL_BUFFER_SAMPLES] __attribute__((aligned(32)));

/* ========================== Private Functions ========================== */

/**
 * @brief Fill buffer region with silence (DAC midpoint)
 */
static void audio_FillSilence(uint16_t *buffer, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        buffer[i] = AUDIO_DAC_SILENCE;
    }
}

/**
 * @brief Handle DMA half-transfer or transfer-complete interrupt
 * @param is_half_transfer true for half-transfer, false for transfer-complete
 * 
 * Called from LEFT channel callbacks only. RIGHT channel follows LEFT timing
 * since both channels receive the same stereo audio data.
 */
static void audio_HandleDMA(Audio_Handle *audio, bool is_half_transfer) {
    if (!audio || !audio->initialized) return;
    
    // Determine which half just finished playing (opposite of what we fill)
    audio->fill_half = is_half_transfer ? AUDIO_BUFFER_FIRST_HALF : AUDIO_BUFFER_SECOND_HALF;
    audio->needs_refill = true;
    
    // Update A/V sync with samples played
    if (audio->avsync) {
        AVSync_AudioTick(audio->avsync, AUDIO_HALF_BUFFER_SAMPLES);
    }
    
    // Update statistics
    audio->stats.samples_played += AUDIO_HALF_BUFFER_SAMPLES;
}

/* ========================== Public API ========================== */

Audio_Status audio_Init(Audio_Handle *audio, DAC_HandleTypeDef *hdac, TIM_HandleTypeDef *htim) {
    if (!audio || !hdac || !htim) return AUDIO_ERROR;
    
    // Clear handle
    memset(audio, 0, sizeof(Audio_Handle));
    
    // Store HAL handles
    audio->hdac = hdac;
    audio->htim = htim;
    audio->avsync = NULL;
    
    // Initialize DMA buffers with silence
    audio_FillSilence(s_dma_buffer_left, AUDIO_FULL_BUFFER_SAMPLES);
    audio_FillSilence(s_dma_buffer_right, AUDIO_FULL_BUFFER_SAMPLES);
    
    // Initialize state
    audio->needs_refill = false;
    audio->fill_half = AUDIO_BUFFER_FIRST_HALF;
    audio->state = AUDIO_STATE_READY;
    audio->initialized = true;
    
    // Set global for HAL callbacks
    s_audio_handle = audio;
    
    return AUDIO_OK;
}

void audio_SetAVSync(Audio_Handle *audio, struct AVSync_Handle *sync) {
    if (audio) {
        audio->avsync = sync;
    }
}

Audio_Status audio_Start(Audio_Handle *audio) {
    if (!audio || !audio->initialized) return AUDIO_ERROR;
    
    // Start timer for DAC triggering
    HAL_TIM_Base_Start(audio->htim);
    
    // Start LEFT channel (DAC_CHANNEL_1) with circular DMA
    HAL_DAC_Start_DMA(audio->hdac, DAC_CHANNEL_1,
                      (uint32_t*)s_dma_buffer_left,
                      AUDIO_FULL_BUFFER_SAMPLES,
                      DAC_ALIGN_12B_R);
    
    // Start RIGHT channel (DAC_CHANNEL_2) with circular DMA
    HAL_DAC_Start_DMA(audio->hdac, DAC_CHANNEL_2,
                      (uint32_t*)s_dma_buffer_right,
                      AUDIO_FULL_BUFFER_SAMPLES,
                      DAC_ALIGN_12B_R);
    
    audio->state = AUDIO_STATE_PLAYING;
    return AUDIO_OK;
}

void audio_Stop(Audio_Handle *audio) {
    if (!audio) return;
    
    // Stop DMA on both channels
    HAL_DAC_Stop_DMA(audio->hdac, DAC_CHANNEL_1);
    HAL_DAC_Stop_DMA(audio->hdac, DAC_CHANNEL_2);
    
    // Stop timer
    HAL_TIM_Base_Stop(audio->htim);
    
    audio->state = AUDIO_STATE_READY;
}

bool audio_NeedsRefill(Audio_Handle *audio) {
    if (!audio) return false;
    return audio->needs_refill;
}

Audio_BufferHalf audio_GetFillHalf(Audio_Handle *audio) {
    if (!audio) return AUDIO_BUFFER_FIRST_HALF;
    return audio->fill_half;
}

uint16_t* audio_GetLeftBuffer(Audio_Handle *audio) {
    (void)audio;  // Buffer is static, but keep param for API consistency
    return s_dma_buffer_left;
}

uint16_t* audio_GetRightBuffer(Audio_Handle *audio) {
    (void)audio;
    return s_dma_buffer_right;
}

void audio_BufferFilled(Audio_Handle *audio) {
    if (!audio) return;
    audio->needs_refill = false;
    audio->stats.refill_count++;
}

/* ========================== HAL Callbacks ========================== */

/*
 * These override the weak default implementations in the HAL.
 * Only LEFT channel (Ch1) callbacks do real work - LEFT is the master.
 * RIGHT channel callbacks are no-ops since both channels are filled together.
 */

// LEFT channel half-transfer complete (first half finished playing)
void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef *hdac) {
    (void)hdac;
    if (s_audio_handle) {
        audio_HandleDMA(s_audio_handle, true);
    }
}

// LEFT channel transfer complete (second half finished playing)
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac) {
    (void)hdac;
    if (s_audio_handle) {
        audio_HandleDMA(s_audio_handle, false);
    }
}

// RIGHT channel callbacks - no-ops, LEFT channel handles timing for both
void HAL_DAC_ConvHalfCpltCallbackCh2(DAC_HandleTypeDef *hdac) {
    (void)hdac;
    // No action - LEFT channel is master
}

void HAL_DAC_ConvCpltCallbackCh2(DAC_HandleTypeDef *hdac) {
    (void)hdac;
    // No action - LEFT channel is master
}
