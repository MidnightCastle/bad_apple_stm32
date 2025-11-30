/**
 * @file    buffers.h
 * @brief   Static buffer management for Bad Apple player
 * @author  David Leathers
 * @date    November 2025
 * 
 * Provides triple-buffered display system and shared constants.
 * All buffers are statically allocated and DMA-aligned.
 * 
 * Triple Buffer Operation:
 *   - render:   Main loop draws next frame here
 *   - ready:    Completed frame waiting for transfer
 *   - transfer: Currently being DMA'd to display
 * 
 * Flow: render → ready (on SwapBuffers) → transfer (on StartTransfer)
 */

#ifndef BUFFERS_H
#define BUFFERS_H

#include "stm32l4xx.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ========================== Display Configuration ========================== */

#define DISPLAY_WIDTH           128
#define DISPLAY_HEIGHT          64
#define FRAMEBUFFER_SIZE        (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)  // 1024 bytes
#define FRAMEBUFFER_COUNT       3   // Triple buffer

/* ========================== Audio Configuration ========================== */

#define AUDIO_SAMPLE_RATE       32000   // Hz
#define AUDIO_BUFFER_SAMPLES    2048    // Samples per half-buffer

/* ========================== Display Framebuffers ========================== */

extern uint8_t g_framebuffer[FRAMEBUFFER_COUNT][FRAMEBUFFER_SIZE];

/* ========================== Triple Buffer State ========================== */

typedef struct {
    volatile uint8_t render;            // Index: main loop renders here
    volatile uint8_t ready;             // Index: completed, awaiting transfer
    volatile uint8_t transfer;          // Index: currently transferring via DMA
    volatile bool transfer_busy;        // DMA transfer in progress
    volatile uint32_t frames_rendered;  // Total frames drawn
    volatile uint32_t frames_transferred; // Total frames sent to display
} TripleBuffer_State;

extern TripleBuffer_State g_display_buffers;

/* ========================== Initialization ========================== */

/**
 * @brief Initialize all buffers to zero
 * @note  Call once at startup before using any buffer functions
 */
void Buffers_Init(void);

/* ========================== Display Buffer API ========================== */

/**
 * @brief Get pointer to current render buffer
 * @return Pointer to 1024-byte framebuffer for drawing
 */
static inline uint8_t* Display_GetRenderBuffer(void) {
    return g_framebuffer[g_display_buffers.render];
}

/**
 * @brief Swap render buffer to ready queue
 * @note  Call after frame is fully rendered
 * 
 * Atomically swaps render and ready indices. The just-rendered
 * frame becomes available for transfer to the display.
 */
static inline void Display_SwapBuffers(void) {
    __disable_irq();
    uint8_t old_ready = g_display_buffers.ready;
    g_display_buffers.ready = g_display_buffers.render;
    g_display_buffers.render = old_ready;
    g_display_buffers.frames_rendered++;
    __enable_irq();
}

/**
 * @brief Check if a new frame is ready for transfer
 * @return true if frames_rendered > frames_transferred
 */
static inline bool Display_HasFrame(void) {
    return g_display_buffers.frames_rendered > g_display_buffers.frames_transferred;
}

/**
 * @brief Get pointer to current transfer buffer
 * @return Pointer to buffer being sent to display
 * @note   Only valid while transfer_busy is true
 */
static inline uint8_t* Display_GetTransferBuffer(void) {
    return g_framebuffer[g_display_buffers.transfer];
}

/**
 * @brief Begin DMA transfer of ready frame
 * @return true if transfer started, false if busy or no frame ready
 * 
 * Atomically swaps ready and transfer indices. Caller must then
 * initiate actual DMA transfer of Display_GetTransferBuffer().
 */
static inline bool Display_StartTransfer(void) {
    if (g_display_buffers.transfer_busy) return false;
    if (!Display_HasFrame()) return false;
    
    __disable_irq();
    uint8_t old_transfer = g_display_buffers.transfer;
    g_display_buffers.transfer = g_display_buffers.ready;
    g_display_buffers.ready = old_transfer;
    g_display_buffers.transfer_busy = true;
    __enable_irq();
    
    return true;
}

/**
 * @brief Mark DMA transfer as complete
 * @note  Call from DMA complete callback
 */
static inline void Display_TransferComplete(void) {
    g_display_buffers.transfer_busy = false;
    g_display_buffers.frames_transferred++;
}

#endif // BUFFERS_H
