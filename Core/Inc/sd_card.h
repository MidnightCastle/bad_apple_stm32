/**
 * @file    sd_card.h
 * @brief   SD card driver (SPI mode) for STM32L4
 * @author  David Leathers
 * @date    November 2025
 * 
 * Features:
 *   - SPI mode initialization (CMD0, CMD8, ACMD41, etc.)
 *   - Single and multi-block reads
 *   - DMA transfers for block data (frees CPU for audio interrupts)
 *   - SDHC/SDXC support (block addressing)
 * 
 * Hardware Requirements:
 *   - SPI peripheral configured as master, 8-bit, Mode 0
 *   - GPIO for chip select (directly controlled)
 *   - DMA channels for SPI TX and RX (optional but recommended)
 * 
 * Initialization Sequence:
 *   1. Set SPI to slow speed (≤400kHz) for init
 *   2. Call SD_Init()
 *   3. Set SPI to fast speed (≤25MHz for standard, ≤50MHz for high-speed)
 *   4. Use SD_ReadBlock() / SD_ReadMultipleBlocks()
 * 
 * Note: This driver uses DMA for data transfers. Call SD_DMA_RxComplete()
 * from HAL_SPI_TxRxCpltCallback() to signal transfer completion.
 */

#ifndef SD_CARD_H
#define SD_CARD_H

#include "stm32l4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================== Configuration ========================== */

#define SD_BLOCK_SIZE       512     // SD block size in bytes
#define SD_SPI_TIMEOUT      100     // HAL timeout for polling operations (ms)
#define SD_DUMMY_BYTE       0xFF    // Dummy byte to send while receiving

/* ========================== SD Commands ========================== */

#define SD_CMD0     0       // GO_IDLE_STATE
#define SD_CMD8     8       // SEND_IF_COND
#define SD_CMD9     9       // SEND_CSD
#define SD_CMD12    12      // STOP_TRANSMISSION
#define SD_CMD17    17      // READ_SINGLE_BLOCK
#define SD_CMD18    18      // READ_MULTIPLE_BLOCK
#define SD_CMD55    55      // APP_CMD
#define SD_CMD58    58      // READ_OCR
#define SD_ACMD41   41      // SD_SEND_OP_COND

// Response flags
#define SD_R1_IDLE_STATE    0x01
#define SD_R1_READY         0x00
#define SD_START_TOKEN      0xFE

/* ========================== Types ========================== */

typedef enum {
    SD_OK = 0,
    SD_ERROR,
    SD_ERROR_TIMEOUT,
    SD_ERROR_NO_CARD
} SD_Status;

typedef enum {
    SD_TYPE_UNKNOWN = 0,
    SD_TYPE_V1,         // SD Version 1.x
    SD_TYPE_V2,         // SD Version 2.0 (standard capacity)
    SD_TYPE_V2HC        // SDHC/SDXC (high capacity, block addressing)
} SD_Type;

typedef struct {
    SD_Type type;
    bool high_capacity;     // true for SDHC/SDXC (block addressing)
    uint32_t capacity;      // Capacity in blocks
    uint32_t block_size;    // Always 512
    uint8_t csd[16];        // Card Specific Data register
} SD_CardInfo;

typedef struct {
    // HAL handles (not owned)
    SPI_HandleTypeDef *hspi;
    
    // Chip select GPIO
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    
    // Card information
    SD_CardInfo info;
    
    // DMA state
    volatile bool dma_busy;
    volatile bool dma_error;
    
    // Init flag
    bool initialized;
} SD_Handle;

/* ========================== Core API ========================== */

/**
 * @brief Initialize SD card
 * @param hsd     Handle to initialize
 * @param hspi    HAL SPI handle (must be pre-configured)
 * @param cs_port GPIO port for chip select
 * @param cs_pin  GPIO pin for chip select
 * @return SD_OK on success, error code otherwise
 * @note  SPI should be at slow speed (≤400kHz) when calling this
 */
SD_Status SD_Init(SD_Handle *hsd, SPI_HandleTypeDef *hspi,
                  GPIO_TypeDef *cs_port, uint16_t cs_pin);

/**
 * @brief Read single 512-byte block
 * @param hsd    Handle
 * @param buffer Destination buffer (must be at least 512 bytes)
 * @param block  Block number (LBA)
 * @return SD_OK on success
 */
SD_Status SD_ReadBlock(SD_Handle *hsd, uint8_t *buffer, uint32_t block);

/**
 * @brief Read multiple consecutive blocks (optimized)
 * @param hsd         Handle
 * @param buffer      Destination buffer (must be at least count*512 bytes)
 * @param start_block Starting block number (LBA)
 * @param count       Number of blocks to read
 * @return SD_OK on success
 * @note  Uses CMD18 + CMD12 for efficient multi-block transfer
 */
SD_Status SD_ReadMultipleBlocks(SD_Handle *hsd, uint8_t *buffer,
                                 uint32_t start_block, uint32_t count);

/* ========================== DMA Callback ========================== */

/**
 * @brief Signal DMA transfer complete
 * @param hsd Handle
 * @note  Call this from HAL_SPI_TxRxCpltCallback() when hspi matches
 */
void SD_DMA_RxComplete(SD_Handle *hsd);

/**
 * @brief Signal DMA error
 * @param hsd Handle
 * @note  Call this from HAL_SPI_ErrorCallback() when hspi matches
 */
void SD_DMA_Error(SD_Handle *hsd);

#endif // SD_CARD_H
