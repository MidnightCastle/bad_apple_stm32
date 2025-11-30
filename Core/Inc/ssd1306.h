/**
 * @file    ssd1306.h
 * @brief   SSD1306 OLED driver for Bad Apple player
 * @author  David Leathers
 * @date    November 2025
 * 
 * Features:
 *   - 128x64 monochrome OLED via I2C
 *   - DMA transfers for video playback (non-blocking)
 *   - Polling transfers for init/debug (blocking)
 *   - 5x7 font for text/stats display
 *   - Integration with triple-buffer system
 * 
 * Hardware:
 *   - I2C address: 0x3C (7-bit) / 0x78 (8-bit with R/W)
 *   - Typical I2C speed: 400kHz (Fast Mode)
 *   - DMA channel required for non-blocking updates
 * 
 * Usage (Playback):
 *   1. SSD1306_Init() with triple buffer from Display_GetRenderBuffer()
 *   2. In main loop: render to buffer, call Display_SwapBuffers()
 *   3. When Display_HasFrame(): call SSD1306_UpdateScreen_DMA()
 *   4. DMA callbacks update triple-buffer state automatically
 * 
 * Usage (Debug/Stats):
 *   1. SSD1306_SetCursor() to position
 *   2. SSD1306_WriteString() to draw text
 *   3. SSD1306_UpdateScreen() to send (blocking)
 */

#ifndef SSD1306_H
#define SSD1306_H

#include "stm32l4xx_hal.h"
#include "buffers.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================== Configuration ========================== */

// Use display dimensions from buffers.h
#define SSD1306_WIDTH       DISPLAY_WIDTH       // 128
#define SSD1306_HEIGHT      DISPLAY_HEIGHT      // 64
#define SSD1306_BUFFER_SIZE FRAMEBUFFER_SIZE    // 1024

// I2C configuration
#define SSD1306_I2C_ADDR    0x78    // 8-bit address (0x3C << 1)
#define SSD1306_TIMEOUT     100     // HAL timeout for polling ops (ms)

// Chunked transfer size for polling mode
#define SSD1306_CHUNK_SIZE  128     // Bytes per I2C transaction

/* ========================== Types ========================== */

typedef enum {
    SSD1306_OK = 0,
    SSD1306_ERROR,
    SSD1306_ERROR_I2C,
    SSD1306_ERROR_BUSY
} SSD1306_Status;

typedef enum {
    SSD1306_COLOR_BLACK = 0,
    SSD1306_COLOR_WHITE = 1
} SSD1306_Color;

// Font descriptor
typedef struct {
    uint8_t width;          // Glyph width in pixels
    uint8_t height;         // Glyph height in pixels
    const uint8_t *data;    // Font bitmap data (column-major)
} SSD1306_Font;

// Driver handle
typedef struct {
    // HAL handle (not owned)
    I2C_HandleTypeDef *hi2c;
    
    // Framebuffer pointer (external or internal)
    uint8_t *framebuffer;
    
    // Text cursor position
    uint8_t cursor_x;
    uint8_t cursor_y;
    
    // DMA state
    volatile bool dma_busy;
    
    // Chunk buffer for polling mode transfers
    uint8_t chunk_buffer[SSD1306_CHUNK_SIZE + 1];
    
    // Error tracking
    SSD1306_Status last_error;
    
    // Init flag
    bool initialized;
} SSD1306_Handle;

/* ========================== Built-in Font ========================== */

/**
 * @brief 5x7 pixel font covering ASCII 32-126
 * 
 * Compact font suitable for status display.
 * Each character is 5 pixels wide, 7 pixels tall.
 * With 1-pixel spacing, fits 21 characters per line.
 */
extern const SSD1306_Font Font_5x7;

/* ========================== Core API ========================== */

/**
 * @brief Initialize display
 * @param hdisplay Handle to initialize
 * @param hi2c     HAL I2C handle
 * @param buffer   Framebuffer (1024 bytes) or NULL for internal buffer
 * @return SSD1306_OK on success
 * 
 * Sends initialization sequence, clears display.
 * If buffer is NULL, uses internal static buffer (not for triple-buffering).
 */
SSD1306_Status SSD1306_Init(SSD1306_Handle *hdisplay, I2C_HandleTypeDef *hi2c, uint8_t *buffer);

/**
 * @brief Clear framebuffer to black
 * @param hdisplay Handle
 */
void SSD1306_Clear(SSD1306_Handle *hdisplay);

/**
 * @brief Set display contrast/brightness
 * @param hdisplay Handle
 * @param contrast Contrast value 0-255 (default 0x7F)
 * @return SSD1306_OK on success
 */
SSD1306_Status SSD1306_SetContrast(SSD1306_Handle *hdisplay, uint8_t contrast);

/* ========================== Text API ========================== */

/**
 * @brief Set text cursor position
 * @param hdisplay Handle
 * @param x        X position in pixels (0-127)
 * @param y        Y position in pixels (0-63)
 */
void SSD1306_SetCursor(SSD1306_Handle *hdisplay, uint8_t x, uint8_t y);

/**
 * @brief Write string to framebuffer
 * @param hdisplay Handle
 * @param str      Null-terminated string
 * @param font     Font to use (e.g., &Font_5x7)
 * @param color    SSD1306_COLOR_WHITE or SSD1306_COLOR_BLACK
 * 
 * Draws text at current cursor position, advances cursor.
 * Wraps to next line when reaching right edge.
 */
void SSD1306_WriteString(SSD1306_Handle *hdisplay, const char *str, 
                          const SSD1306_Font *font, SSD1306_Color color);

/* ========================== Screen Update (Polling) ========================== */

/**
 * @brief Send framebuffer to display (blocking)
 * @param hdisplay Handle
 * @return SSD1306_OK on success
 * 
 * Transfers entire 1024-byte framebuffer via I2C polling.
 * Blocks until complete (~20ms at 400kHz I2C).
 * Use for init, debug, or when DMA unavailable.
 */
SSD1306_Status SSD1306_UpdateScreen(SSD1306_Handle *hdisplay);

/* ========================== Screen Update (DMA) ========================== */

/**
 * @brief Send framebuffer via DMA (non-blocking)
 * @param hdisplay Handle
 * @return SSD1306_OK if transfer started, SSD1306_ERROR_BUSY if DMA in progress
 * 
 * Uses triple-buffer system from buffers.h:
 *   - Calls Display_StartTransfer() to get ready buffer
 *   - Starts I2C DMA transfer
 *   - Returns immediately
 * 
 * Caller must configure HAL_I2C_MemTxCpltCallback() to call
 * SSD1306_DMA_CompleteCallback(), and HAL_I2C_ErrorCallback()
 * to call SSD1306_DMA_ErrorCallback().
 */
SSD1306_Status SSD1306_UpdateScreen_DMA(SSD1306_Handle *hdisplay);

/**
 * @brief Check if DMA transfer is in progress
 * @param hdisplay Handle
 * @return true if DMA busy
 */
bool SSD1306_IsDMABusy(SSD1306_Handle *hdisplay);

/**
 * @brief DMA transfer complete callback
 * @param hdisplay Handle
 * @param hi2c     I2C handle (for verification, can be NULL)
 * @note  Call from HAL_I2C_MemTxCpltCallback()
 */
void SSD1306_DMA_CompleteCallback(SSD1306_Handle *hdisplay, I2C_HandleTypeDef *hi2c);

/**
 * @brief DMA error callback
 * @param hdisplay Handle
 * @param hi2c     I2C handle (for verification, can be NULL)
 * @note  Call from HAL_I2C_ErrorCallback()
 */
void SSD1306_DMA_ErrorCallback(SSD1306_Handle *hdisplay, I2C_HandleTypeDef *hi2c);

#endif // SSD1306_H
