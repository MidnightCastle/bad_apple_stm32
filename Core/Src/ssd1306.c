/**
 * @file    ssd1306.c
 * @brief   SSD1306 OLED driver implementation
 * @author  David Leathers
 * @date    November 2025
 */

#include "ssd1306.h"
#include <string.h>

/* ========================== SSD1306 Commands ========================== */

#define SSD1306_DISPLAYOFF          0xAE
#define SSD1306_DISPLAYON           0xAF
#define SSD1306_SETDISPLAYCLOCKDIV  0xD5
#define SSD1306_SETMULTIPLEX        0xA8
#define SSD1306_SETDISPLAYOFFSET    0xD3
#define SSD1306_SETSTARTLINE        0x40
#define SSD1306_CHARGEPUMP          0x8D
#define SSD1306_MEMORYMODE          0x20
#define SSD1306_SEGREMAP            0xA0
#define SSD1306_COMSCANDEC          0xC8
#define SSD1306_SETCOMPINS          0xDA
#define SSD1306_SETCONTRAST         0x81
#define SSD1306_SETPRECHARGE        0xD9
#define SSD1306_SETVCOMDETECT       0xDB
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_NORMALDISPLAY       0xA6
#define SSD1306_COLUMNADDR          0x21
#define SSD1306_PAGEADDR            0x22

/* ========================== Private Data ========================== */

// Internal framebuffer for standalone use (not triple-buffered)
static uint8_t s_internal_buffer[SSD1306_BUFFER_SIZE];

/* ========================== Private Functions ========================== */

/**
 * @brief Write single command byte
 */
static SSD1306_Status SSD1306_WriteCommand(SSD1306_Handle *hd, uint8_t cmd) {
    uint8_t data[2] = {0x00, cmd};  /* 0x00 = command mode */
    
    if (HAL_I2C_Master_Transmit(hd->hi2c, SSD1306_I2C_ADDR, data, 2, SSD1306_TIMEOUT) != HAL_OK) {
        hd->last_error = SSD1306_ERROR_I2C;
        return SSD1306_ERROR_I2C;
    }
    return SSD1306_OK;
}

/**
 * @brief Set address window for full-screen write
 */
static SSD1306_Status SSD1306_SetAddressWindow(SSD1306_Handle *hd) {
    // Column address: 0 to 127
    if (SSD1306_WriteCommand(hd, SSD1306_COLUMNADDR) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, 0x00) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, SSD1306_WIDTH - 1) != SSD1306_OK) return SSD1306_ERROR;
    
    // Page address: 0 to 7 (64 pixels / 8 pixels per page)
    if (SSD1306_WriteCommand(hd, SSD1306_PAGEADDR) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, 0x00) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, (SSD1306_HEIGHT / 8) - 1) != SSD1306_OK) return SSD1306_ERROR;
    
    return SSD1306_OK;
}

/* ========================== Core API ========================== */

SSD1306_Status SSD1306_Init(SSD1306_Handle *hd, I2C_HandleTypeDef *hi2c, uint8_t *buffer) {
    if (!hd || !hi2c) return SSD1306_ERROR;
    
    // Clear handle
    memset(hd, 0, sizeof(SSD1306_Handle));
    hd->hi2c = hi2c;
    hd->framebuffer = buffer ? buffer : s_internal_buffer;
    
    // Power-on delay
    HAL_Delay(100);
    
    // Initialization sequence for 128x64 OLED
    if (SSD1306_WriteCommand(hd, SSD1306_DISPLAYOFF) != SSD1306_OK) return SSD1306_ERROR;
    
    // Timing & clock
    if (SSD1306_WriteCommand(hd, SSD1306_SETDISPLAYCLOCKDIV) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, 0x80) != SSD1306_OK) return SSD1306_ERROR;  /* Default ratio */
    
    // Multiplex ratio (height - 1)
    if (SSD1306_WriteCommand(hd, SSD1306_SETMULTIPLEX) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, SSD1306_HEIGHT - 1) != SSD1306_OK) return SSD1306_ERROR;
    
    // Display offset
    if (SSD1306_WriteCommand(hd, SSD1306_SETDISPLAYOFFSET) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, 0x00) != SSD1306_OK) return SSD1306_ERROR;
    
    // Start line
    if (SSD1306_WriteCommand(hd, SSD1306_SETSTARTLINE | 0x00) != SSD1306_OK) return SSD1306_ERROR;
    
    // Charge pump (required for most modules)
    if (SSD1306_WriteCommand(hd, SSD1306_CHARGEPUMP) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, 0x14) != SSD1306_OK) return SSD1306_ERROR;  // Enable
    
    // Memory mode: horizontal addressing
    if (SSD1306_WriteCommand(hd, SSD1306_MEMORYMODE) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, 0x00) != SSD1306_OK) return SSD1306_ERROR;
    
    // Segment remap (flip horizontally for correct orientation)
    if (SSD1306_WriteCommand(hd, SSD1306_SEGREMAP | 0x01) != SSD1306_OK) return SSD1306_ERROR;
    
    // COM scan direction (flip vertically for correct orientation)
    if (SSD1306_WriteCommand(hd, SSD1306_COMSCANDEC) != SSD1306_OK) return SSD1306_ERROR;
    
    // COM pins configuration (for 128x64)
    if (SSD1306_WriteCommand(hd, SSD1306_SETCOMPINS) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, 0x12) != SSD1306_OK) return SSD1306_ERROR;
    
    // Contrast
    if (SSD1306_WriteCommand(hd, SSD1306_SETCONTRAST) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, 0x7F) != SSD1306_OK) return SSD1306_ERROR;  // Medium
    
    // Pre-charge period
    if (SSD1306_WriteCommand(hd, SSD1306_SETPRECHARGE) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, 0xF1) != SSD1306_OK) return SSD1306_ERROR;
    
    // VCOMH deselect level
    if (SSD1306_WriteCommand(hd, SSD1306_SETVCOMDETECT) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, 0x40) != SSD1306_OK) return SSD1306_ERROR;
    
    // Resume from GDRAM content
    if (SSD1306_WriteCommand(hd, SSD1306_DISPLAYALLON_RESUME) != SSD1306_OK) return SSD1306_ERROR;
    
    // Normal display (not inverted)
    if (SSD1306_WriteCommand(hd, SSD1306_NORMALDISPLAY) != SSD1306_OK) return SSD1306_ERROR;
    
    // Display ON
    if (SSD1306_WriteCommand(hd, SSD1306_DISPLAYON) != SSD1306_OK) return SSD1306_ERROR;
    
    hd->initialized = true;
    SSD1306_Clear(hd);
    
    return SSD1306_OK;
}

void SSD1306_Clear(SSD1306_Handle *hd) {
    if (!hd || !hd->framebuffer) return;
    memset(hd->framebuffer, 0, SSD1306_BUFFER_SIZE);
}

SSD1306_Status SSD1306_SetContrast(SSD1306_Handle *hd, uint8_t contrast) {
    if (!hd || !hd->initialized) return SSD1306_ERROR;
    
    if (SSD1306_WriteCommand(hd, SSD1306_SETCONTRAST) != SSD1306_OK) return SSD1306_ERROR;
    if (SSD1306_WriteCommand(hd, contrast) != SSD1306_OK) return SSD1306_ERROR;
    
    return SSD1306_OK;
}

/* ========================== Text API ========================== */

void SSD1306_SetCursor(SSD1306_Handle *hd, uint8_t x, uint8_t y) {
    if (!hd) return;
    hd->cursor_x = x;
    hd->cursor_y = y;
}

void SSD1306_WriteString(SSD1306_Handle *hd, const char *str, 
                          const SSD1306_Font *font, SSD1306_Color color) {
    if (!hd || !hd->framebuffer || !str || !font) return;
    
    while (*str) {
        char c = *str++;
        
        // Map unprintable characters to '?'
        if (c < 32 || c > 126) c = '?';
        
        // Get glyph data
        const uint8_t *glyph = &font->data[(c - 32) * font->width];
        
        // Render glyph column by column
        for (uint8_t col = 0; col < font->width; col++) {
            uint8_t line = glyph[col];
            
            for (uint8_t row = 0; row < font->height; row++) {
                uint8_t px = hd->cursor_x + col;
                uint8_t py = hd->cursor_y + row;
                
                // Bounds check
                if (px >= SSD1306_WIDTH || py >= SSD1306_HEIGHT) continue;
                
                // Calculate buffer position
                uint16_t idx = px + (py / 8) * SSD1306_WIDTH;
                uint8_t bit = 1 << (py % 8);
                
                // Set or clear pixel
                if ((line & (1 << row)) && color == SSD1306_COLOR_WHITE) {
                    hd->framebuffer[idx] |= bit;
                } else {
                    hd->framebuffer[idx] &= ~bit;
                }
            }
        }
        
        // Advance cursor
        hd->cursor_x += font->width + 1;
        
        // Wrap to next line if needed
        if (hd->cursor_x >= SSD1306_WIDTH - font->width) {
            hd->cursor_x = 0;
            hd->cursor_y += font->height + 1;
        }
    }
}

/* ========================== Screen Update (Polling) ========================== */

SSD1306_Status SSD1306_UpdateScreen(SSD1306_Handle *hd) {
    if (!hd || !hd->initialized || !hd->framebuffer) return SSD1306_ERROR;
    
    // Set address window for full screen
    if (SSD1306_SetAddressWindow(hd) != SSD1306_OK) return SSD1306_ERROR;
    
    // Send framebuffer in chunks
    for (uint16_t offset = 0; offset < SSD1306_BUFFER_SIZE; offset += SSD1306_CHUNK_SIZE) {
        uint16_t len = SSD1306_CHUNK_SIZE;
        if (offset + len > SSD1306_BUFFER_SIZE) {
            len = SSD1306_BUFFER_SIZE - offset;
        }
        
        // Prepare chunk with data mode prefix
        hd->chunk_buffer[0] = 0x40;  // Data mode
        memcpy(&hd->chunk_buffer[1], &hd->framebuffer[offset], len);
        
        // Transmit
        if (HAL_I2C_Master_Transmit(hd->hi2c, SSD1306_I2C_ADDR, 
                                     hd->chunk_buffer, len + 1, 
                                     SSD1306_TIMEOUT) != HAL_OK) {
            hd->last_error = SSD1306_ERROR_I2C;
            return SSD1306_ERROR_I2C;
        }
    }
    
    return SSD1306_OK;
}

/* ========================== Screen Update (DMA) ========================== */

SSD1306_Status SSD1306_UpdateScreen_DMA(SSD1306_Handle *hd) {
    if (!hd || !hd->initialized) return SSD1306_ERROR;
    if (hd->dma_busy) return SSD1306_ERROR_BUSY;
    
    // Get buffer from triple-buffer system
    if (!Display_StartTransfer()) {
        return SSD1306_ERROR;  // No frame ready
    }
    
    // Set address window (blocking, but fast)
    if (SSD1306_SetAddressWindow(hd) != SSD1306_OK) {
        Display_TransferComplete();
        return SSD1306_ERROR;
    }
    
    hd->dma_busy = true;
    
    // Start DMA transfer using HAL memory write
    HAL_StatusTypeDef result = HAL_I2C_Mem_Write_DMA(
        hd->hi2c,
        SSD1306_I2C_ADDR,
        0x40,                       // Data mode register
        I2C_MEMADD_SIZE_8BIT,
        Display_GetTransferBuffer(),
        SSD1306_BUFFER_SIZE
    );
    
    if (result != HAL_OK) {
        hd->dma_busy = false;
        hd->last_error = SSD1306_ERROR_I2C;
        Display_TransferComplete();
        return SSD1306_ERROR_I2C;
    }
    
    return SSD1306_OK;
}

bool SSD1306_IsDMABusy(SSD1306_Handle *hd) {
    if (!hd) return false;
    return hd->dma_busy;
}

void SSD1306_DMA_CompleteCallback(SSD1306_Handle *hd, I2C_HandleTypeDef *hi2c) {
    (void)hi2c;  // Unused - could verify handle match if needed
    if (!hd) return;
    
    hd->dma_busy = false;
    Display_TransferComplete();
}

void SSD1306_DMA_ErrorCallback(SSD1306_Handle *hd, I2C_HandleTypeDef *hi2c) {
    (void)hi2c;
    if (!hd) return;
    
    hd->dma_busy = false;
    hd->last_error = SSD1306_ERROR_I2C;
    Display_TransferComplete();
}

/* ========================== Font Data ========================== */

/**
 * 5x7 Font Data - ASCII 32-126
 * 
 * Each character is 5 bytes (columns), each byte is 7 bits (rows).
 * Bit 0 is top row, bit 6 is bottom row.
 * Column-major ordering for efficient vertical scanning.
 */
static const uint8_t Font5x7_Data[] = {
    0x00, 0x00, 0x00, 0x00, 0x00,  // Space
    0x00, 0x00, 0x5F, 0x00, 0x00,  // !
    0x00, 0x07, 0x00, 0x07, 0x00,  // "
    0x14, 0x7F, 0x14, 0x7F, 0x14,  // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12,  // $
    0x23, 0x13, 0x08, 0x64, 0x62,  // %
    0x36, 0x49, 0x55, 0x22, 0x50,  // &
    0x00, 0x05, 0x03, 0x00, 0x00,  // '
    0x00, 0x1C, 0x22, 0x41, 0x00,  // (
    0x00, 0x41, 0x22, 0x1C, 0x00,  // )
    0x14, 0x08, 0x3E, 0x08, 0x14,  // *
    0x08, 0x08, 0x3E, 0x08, 0x08,  // +
    0x00, 0x50, 0x30, 0x00, 0x00,  // ,
    0x08, 0x08, 0x08, 0x08, 0x08,  // -
    0x00, 0x60, 0x60, 0x00, 0x00,  // .
    0x20, 0x10, 0x08, 0x04, 0x02,  // /
    0x3E, 0x51, 0x49, 0x45, 0x3E,  // 0
    0x00, 0x42, 0x7F, 0x40, 0x00,  // 1
    0x42, 0x61, 0x51, 0x49, 0x46,  // 2
    0x21, 0x41, 0x45, 0x4B, 0x31,  // 3
    0x18, 0x14, 0x12, 0x7F, 0x10,  // 4
    0x27, 0x45, 0x45, 0x45, 0x39,  // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30,  // 6
    0x01, 0x71, 0x09, 0x05, 0x03,  // 7
    0x36, 0x49, 0x49, 0x49, 0x36,  // 8
    0x06, 0x49, 0x49, 0x29, 0x1E,  // 9
    0x00, 0x36, 0x36, 0x00, 0x00,  // :
    0x00, 0x56, 0x36, 0x00, 0x00,  // ;
    0x08, 0x14, 0x22, 0x41, 0x00,  // <
    0x14, 0x14, 0x14, 0x14, 0x14,  // =
    0x00, 0x41, 0x22, 0x14, 0x08,  // >
    0x02, 0x01, 0x51, 0x09, 0x06,  // ?
    0x32, 0x49, 0x79, 0x41, 0x3E,  // @
    0x7E, 0x11, 0x11, 0x11, 0x7E,  // A
    0x7F, 0x49, 0x49, 0x49, 0x36,  // B
    0x3E, 0x41, 0x41, 0x41, 0x22,  // C
    0x7F, 0x41, 0x41, 0x22, 0x1C,  // D
    0x7F, 0x49, 0x49, 0x49, 0x41,  // E
    0x7F, 0x09, 0x09, 0x09, 0x01,  // F
    0x3E, 0x41, 0x49, 0x49, 0x7A,  // G
    0x7F, 0x08, 0x08, 0x08, 0x7F,  // H
    0x00, 0x41, 0x7F, 0x41, 0x00,  // I
    0x20, 0x40, 0x41, 0x3F, 0x01,  // J
    0x7F, 0x08, 0x14, 0x22, 0x41,  // K
    0x7F, 0x40, 0x40, 0x40, 0x40,  // L
    0x7F, 0x02, 0x0C, 0x02, 0x7F,  // M
    0x7F, 0x04, 0x08, 0x10, 0x7F,  // N
    0x3E, 0x41, 0x41, 0x41, 0x3E,  // O
    0x7F, 0x09, 0x09, 0x09, 0x06,  // P
    0x3E, 0x41, 0x51, 0x21, 0x5E,  // Q
    0x7F, 0x09, 0x19, 0x29, 0x46,  // R
    0x46, 0x49, 0x49, 0x49, 0x31,  // S
    0x01, 0x01, 0x7F, 0x01, 0x01,  // T
    0x3F, 0x40, 0x40, 0x40, 0x3F,  // U
    0x1F, 0x20, 0x40, 0x20, 0x1F,  // V
    0x3F, 0x40, 0x38, 0x40, 0x3F,  // W
    0x63, 0x14, 0x08, 0x14, 0x63,  // X
    0x07, 0x08, 0x70, 0x08, 0x07,  // Y
    0x61, 0x51, 0x49, 0x45, 0x43,  // Z
    0x00, 0x7F, 0x41, 0x41, 0x00,  // [
    0x02, 0x04, 0x08, 0x10, 0x20,  // \
    0x00, 0x41, 0x41, 0x7F, 0x00,  // ]
    0x04, 0x02, 0x01, 0x02, 0x04,  // ^
    0x40, 0x40, 0x40, 0x40, 0x40,  // _
    0x00, 0x01, 0x02, 0x04, 0x00,  // `
    0x20, 0x54, 0x54, 0x54, 0x78,  // a
    0x7F, 0x48, 0x44, 0x44, 0x38,  // b
    0x38, 0x44, 0x44, 0x44, 0x20,  // c
    0x38, 0x44, 0x44, 0x48, 0x7F,  // d
    0x38, 0x54, 0x54, 0x54, 0x18,  // e
    0x08, 0x7E, 0x09, 0x01, 0x02,  // f
    0x0C, 0x52, 0x52, 0x52, 0x3E,  // g
    0x7F, 0x08, 0x04, 0x04, 0x78,  // h
    0x00, 0x44, 0x7D, 0x40, 0x00,  // i
    0x20, 0x40, 0x44, 0x3D, 0x00,  // j
    0x7F, 0x10, 0x28, 0x44, 0x00,  // k
    0x00, 0x41, 0x7F, 0x40, 0x00,  // l
    0x7C, 0x04, 0x18, 0x04, 0x78,  // m
    0x7C, 0x08, 0x04, 0x04, 0x78,  // n
    0x38, 0x44, 0x44, 0x44, 0x38,  // o
    0x7C, 0x14, 0x14, 0x14, 0x08,  // p
    0x08, 0x14, 0x14, 0x18, 0x7C,  // q
    0x7C, 0x08, 0x04, 0x04, 0x08,  // r
    0x48, 0x54, 0x54, 0x54, 0x20,  // s
    0x04, 0x3F, 0x44, 0x40, 0x20,  // t
    0x3C, 0x40, 0x40, 0x20, 0x7C,  // u
    0x1C, 0x20, 0x40, 0x20, 0x1C,  // v
    0x3C, 0x40, 0x30, 0x40, 0x3C,  // w
    0x44, 0x28, 0x10, 0x28, 0x44,  // x
    0x0C, 0x50, 0x50, 0x50, 0x3C,  // y
    0x44, 0x64, 0x54, 0x4C, 0x44,  // z
    0x00, 0x08, 0x36, 0x41, 0x00,  // {
    0x00, 0x00, 0x7F, 0x00, 0x00,  // |
    0x00, 0x41, 0x36, 0x08, 0x00,  // }
    0x10, 0x08, 0x08, 0x10, 0x08,  // ~
};

const SSD1306_Font Font_5x7 = {
    .width = 5,
    .height = 7,
    .data = Font5x7_Data
};
