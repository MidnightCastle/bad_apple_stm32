/**
 * @file    buffers.c
 * @brief   Static buffer definitions
 * @author  David Leathers
 * @date    November 2025
 */

#include "buffers.h"

/* ========================== Display Framebuffers ========================== */

// Triple buffer for display - 32-byte aligned for DMA
uint8_t g_framebuffer[FRAMEBUFFER_COUNT][FRAMEBUFFER_SIZE]
    __attribute__((aligned(32)));

/* ========================== Triple Buffer State ========================== */

TripleBuffer_State g_display_buffers = {
    .render = 0,
    .ready = 2,
    .transfer = 1,
    .transfer_busy = false,
    .frames_rendered = 0,
    .frames_transferred = 0
};

/* ========================== Initialization ========================== */

void Buffers_Init(void) {
    // Clear framebuffers
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
    
    // Reset triple buffer state
    g_display_buffers.render = 0;
    g_display_buffers.ready = 2;
    g_display_buffers.transfer = 1;
    g_display_buffers.transfer_busy = false;
    g_display_buffers.frames_rendered = 0;
    g_display_buffers.frames_transferred = 0;
}
