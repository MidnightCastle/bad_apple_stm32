/**
 * @file    sd_card.c
 * @brief   SD card driver implementation with DMA support
 * @author  David Leathers
 * @date    November 2025
 */

#include "sd_card.h"
#include "perf.h"
#include <string.h>

/* ========================== Private Constants ========================== */

// Timeouts in microseconds
#define SD_RESPONSE_TIMEOUT_US      100000      // 100ms for command response
#define SD_READY_TIMEOUT_US         500000      // 500ms for card ready
#define SD_DATA_TIMEOUT_US          250000      // 250ms for data token
#define SD_DMA_TIMEOUT_US           100000      // 100ms for DMA transfer

/* ========================== Private Data ========================== */

// 0xFF buffer for SPI receive - MOSI must stay HIGH while reading
static uint8_t s_ff_buffer[SD_BLOCK_SIZE] __attribute__((aligned(32)));
static bool s_ff_buffer_initialized = false;

/* ========================== Chip Select Control ========================== */

static inline void SD_CS_Select(SD_Handle *hsd) {
    HAL_GPIO_WritePin(hsd->cs_port, hsd->cs_pin, GPIO_PIN_RESET);
    // Small delay for CS setup time
    __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();
}

static inline void SD_CS_Deselect(SD_Handle *hsd) {
    // Small delay before CS release
    __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();
    HAL_GPIO_WritePin(hsd->cs_port, hsd->cs_pin, GPIO_PIN_SET);
    
    // Send dummy byte to release DO line
    uint8_t dummy = SD_DUMMY_BYTE;
    uint8_t rx;
    HAL_SPI_TransmitReceive(hsd->hspi, &dummy, &rx, 1, SD_SPI_TIMEOUT);
}

/* ========================== Basic SPI Operations ========================== */

static uint8_t SD_SendByte(SD_Handle *hsd, uint8_t byte) {
    uint8_t rx = 0xFF;
    HAL_SPI_TransmitReceive(hsd->hspi, &byte, &rx, 1, SD_SPI_TIMEOUT);
    return rx;
}

static inline uint8_t SD_ReadByte(SD_Handle *hsd) {
    return SD_SendByte(hsd, SD_DUMMY_BYTE);
}

/* ========================== Command Protocol ========================== */

static void SD_SendCommand(SD_Handle *hsd, uint8_t cmd, uint32_t arg) {
    // Send dummy byte before command
    SD_SendByte(hsd, SD_DUMMY_BYTE);
    
    // Command byte: 01xxxxxx where xxxxxx is command number
    SD_SendByte(hsd, 0x40 | cmd);
    
    // Argument (big endian)
    SD_SendByte(hsd, (arg >> 24) & 0xFF);
    SD_SendByte(hsd, (arg >> 16) & 0xFF);
    SD_SendByte(hsd, (arg >> 8) & 0xFF);
    SD_SendByte(hsd, arg & 0xFF);
    
    // CRC - only required for CMD0 and CMD8 in SPI mode
    if (cmd == SD_CMD0) {
        SD_SendByte(hsd, 0x95);  // Valid CRC for CMD0
    } else if (cmd == SD_CMD8) {
        SD_SendByte(hsd, 0x87);  // Valid CRC for CMD8 with 0x1AA arg
    } else {
        SD_SendByte(hsd, 0x01);  // Dummy CRC with stop bit
    }
}

static uint8_t SD_GetResponse(SD_Handle *hsd) {
    uint32_t start = Perf_GetCycles();
    uint8_t response;
    
    // Wait for response (MSB = 0)
    do {
        response = SD_ReadByte(hsd);
        if (Perf_CyclesToMicros(Perf_GetCycles() - start) > SD_RESPONSE_TIMEOUT_US) {
            return 0xFF;  // Timeout
        }
    } while (response & 0x80);
    
    return response;
}

static SD_Status SD_WaitReady(SD_Handle *hsd, uint32_t timeout_us) {
    uint32_t start = Perf_GetCycles();
    
    // Wait for card to release DO line (0xFF = ready)
    while (SD_ReadByte(hsd) != 0xFF) {
        if (Perf_CyclesToMicros(Perf_GetCycles() - start) > timeout_us) {
            return SD_ERROR_TIMEOUT;
        }
    }
    return SD_OK;
}

static SD_Status SD_WaitDataToken(SD_Handle *hsd) {
    uint32_t start = Perf_GetCycles();
    uint8_t token;
    
    do {
        token = SD_ReadByte(hsd);
        
        if (token == SD_START_TOKEN) {
            return SD_OK;
        }
        
        // Check for error token (0x0X)
        if ((token & 0xF0) == 0x00) {
            return SD_ERROR;
        }
        
        if (Perf_CyclesToMicros(Perf_GetCycles() - start) > SD_DATA_TIMEOUT_US) {
            return SD_ERROR_TIMEOUT;
        }
    } while (1);
}

/* ========================== DMA Block Read ========================== */

/**
 * @brief Read block data using DMA
 * @param hsd    Handle
 * @param buffer Destination (512 bytes)
 * @return SD_OK on success
 * 
 * Assumes data token already received. Reads 512 bytes + 2 CRC bytes.
 * Uses DMA for the 512-byte transfer, polling for CRC.
 */
static SD_Status SD_ReadBlockData_DMA(SD_Handle *hsd, uint8_t *buffer) {
    // Start DMA transfer - transmit 0xFF buffer, receive to user buffer
    hsd->dma_busy = true;
    hsd->dma_error = false;
    
    HAL_StatusTypeDef hal_status = HAL_SPI_TransmitReceive_DMA(
        hsd->hspi,
        s_ff_buffer,
        buffer,
        SD_BLOCK_SIZE
    );
    
    if (hal_status != HAL_OK) {
        hsd->dma_busy = false;
        return SD_ERROR;
    }
    
    // Wait for DMA completion
    uint32_t start = Perf_GetCycles();
    while (hsd->dma_busy) {
        if (Perf_CyclesToMicros(Perf_GetCycles() - start) > SD_DMA_TIMEOUT_US) {
            // Abort DMA on timeout
            HAL_SPI_DMAStop(hsd->hspi);
            hsd->dma_busy = false;
            return SD_ERROR_TIMEOUT;
        }
    }
    
    if (hsd->dma_error) {
        return SD_ERROR;
    }
    
    // Discard CRC (2 bytes) - use polling, it's fast
    SD_ReadByte(hsd);
    SD_ReadByte(hsd);
    
    return SD_OK;
}

/* ========================== Initialization Sequence ========================== */

static void SD_PowerUpSequence(SD_Handle *hsd) {
    // Send 80+ clock pulses with CS high
    HAL_GPIO_WritePin(hsd->cs_port, hsd->cs_pin, GPIO_PIN_SET);
    for (int i = 0; i < 10; i++) {
        SD_SendByte(hsd, SD_DUMMY_BYTE);
    }
}

static SD_Status SD_GoIdleState(SD_Handle *hsd) {
    for (int attempts = 0; attempts < 10; attempts++) {
        SD_CS_Select(hsd);
        SD_SendCommand(hsd, SD_CMD0, 0);
        uint8_t r1 = SD_GetResponse(hsd);
        SD_CS_Deselect(hsd);
        
        if (r1 == SD_R1_IDLE_STATE) {
            return SD_OK;
        }
        Perf_DelayMicros(100);
    }
    return SD_ERROR_NO_CARD;
}

static SD_Status SD_CheckVoltage(SD_Handle *hsd) {
    SD_CS_Select(hsd);
    SD_SendCommand(hsd, SD_CMD8, 0x000001AA);
    uint8_t r1 = SD_GetResponse(hsd);
    
    if (r1 == 0xFF) {
        SD_CS_Deselect(hsd);
        return SD_ERROR_TIMEOUT;
    }
    
    // Read R7 response (4 bytes)
    uint8_t r7[4];
    for (int i = 0; i < 4; i++) {
        r7[i] = SD_ReadByte(hsd);
    }
    SD_CS_Deselect(hsd);
    
    if (r1 != SD_R1_IDLE_STATE) {
        return SD_ERROR;
    }
    
    // Check voltage accepted and echo pattern
    if ((r7[2] & 0x0F) != 0x01 || r7[3] != 0xAA) {
        return SD_ERROR;
    }
    
    hsd->info.type = SD_TYPE_V2;
    return SD_OK;
}

static SD_Status SD_InitializeCard(SD_Handle *hsd) {
    // HCS bit for SDHC support
    uint32_t arg = (hsd->info.type == SD_TYPE_V2) ? 0x40000000 : 0;
    
    for (int attempts = 0; attempts < 1000; attempts++) {
        // CMD55 - Application command prefix
        SD_CS_Select(hsd);
        SD_SendCommand(hsd, SD_CMD55, 0);
        SD_GetResponse(hsd);
        SD_CS_Deselect(hsd);
        
        // ACMD41 - Initialize card
        SD_CS_Select(hsd);
        SD_SendCommand(hsd, SD_ACMD41, arg);
        uint8_t r1 = SD_GetResponse(hsd);
        SD_CS_Deselect(hsd);
        
        if (r1 == SD_R1_READY) {
            break;
        }
        
        if (attempts >= 999) {
            return SD_ERROR_TIMEOUT;
        }
        
        Perf_DelayMicros(1000);
    }
    
    // Check for SDHC (read OCR)
    if (hsd->info.type == SD_TYPE_V2) {
        SD_CS_Select(hsd);
        SD_SendCommand(hsd, SD_CMD58, 0);
        SD_GetResponse(hsd);
        
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) {
            ocr[i] = SD_ReadByte(hsd);
        }
        SD_CS_Deselect(hsd);
        
        // Check CCS bit (Card Capacity Status)
        if (ocr[0] & 0x40) {
            hsd->info.type = SD_TYPE_V2HC;
            hsd->info.high_capacity = true;
        }
    }
    
    return SD_OK;
}

static SD_Status SD_ReadCSD(SD_Handle *hsd) {
    SD_CS_Select(hsd);
    SD_SendCommand(hsd, SD_CMD9, 0);
    
    if (SD_GetResponse(hsd) != 0x00) {
        SD_CS_Deselect(hsd);
        return SD_ERROR;
    }
    
    if (SD_WaitDataToken(hsd) != SD_OK) {
        SD_CS_Deselect(hsd);
        return SD_ERROR;
    }
    
    // Read 16 bytes of CSD register
    for (int i = 0; i < 16; i++) {
        hsd->info.csd[i] = SD_ReadByte(hsd);
    }
    
    // Discard CRC (2 bytes)
    SD_ReadByte(hsd);
    SD_ReadByte(hsd);
    
    SD_CS_Deselect(hsd);
    
    // Parse capacity for SDHC
    if (hsd->info.high_capacity) {
        uint32_t c_size = ((uint32_t)(hsd->info.csd[7] & 0x3F) << 16) |
                          ((uint32_t)hsd->info.csd[8] << 8) |
                          hsd->info.csd[9];
        hsd->info.capacity = (c_size + 1) * 1024;  // In 512-byte blocks
    }
    
    hsd->info.block_size = SD_BLOCK_SIZE;
    return SD_OK;
}

/* ========================== Public API ========================== */

SD_Status SD_Init(SD_Handle *hsd, SPI_HandleTypeDef *hspi,
                  GPIO_TypeDef *cs_port, uint16_t cs_pin) {
    if (!hsd || !hspi) return SD_ERROR;
    
    // Initialize 0xFF buffer for DMA transmit
    if (!s_ff_buffer_initialized) {
        memset(s_ff_buffer, 0xFF, sizeof(s_ff_buffer));
        s_ff_buffer_initialized = true;
    }
    
    // Clear handle
    memset(hsd, 0, sizeof(SD_Handle));
    hsd->hspi = hspi;
    hsd->cs_port = cs_port;
    hsd->cs_pin = cs_pin;
    
    // CS high initially
    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);
    
    // Power-up delay
    Perf_DelayMicros(100000);  // 100ms
    
    // Send clock pulses with CS high
    SD_PowerUpSequence(hsd);
    
    // Enter SPI mode
    if (SD_GoIdleState(hsd) != SD_OK) {
        return SD_ERROR_NO_CARD;
    }
    
    // Check card version
    if (SD_CheckVoltage(hsd) != SD_OK) {
        hsd->info.type = SD_TYPE_V1;
    }
    
    // Initialize card
    if (SD_InitializeCard(hsd) != SD_OK) {
        return SD_ERROR;
    }
    
    // Read card information
    if (SD_ReadCSD(hsd) != SD_OK) {
        return SD_ERROR;
    }
    
    hsd->initialized = true;
    return SD_OK;
}

SD_Status SD_ReadBlock(SD_Handle *hsd, uint8_t *buffer, uint32_t block) {
    if (!hsd || !hsd->initialized || !buffer) return SD_ERROR;
    
    // SDHC uses block addressing, standard SD uses byte addressing
    uint32_t addr = hsd->info.high_capacity ? block : (block * SD_BLOCK_SIZE);
    
    SD_CS_Select(hsd);
    SD_SendCommand(hsd, SD_CMD17, addr);
    
    if (SD_GetResponse(hsd) != 0x00) {
        SD_CS_Deselect(hsd);
        return SD_ERROR;
    }
    
    if (SD_WaitDataToken(hsd) != SD_OK) {
        SD_CS_Deselect(hsd);
        return SD_ERROR_TIMEOUT;
    }
    
    // Read block data using DMA
    SD_Status status = SD_ReadBlockData_DMA(hsd, buffer);
    
    SD_CS_Deselect(hsd);
    return status;
}

SD_Status SD_ReadMultipleBlocks(SD_Handle *hsd, uint8_t *buffer,
                                 uint32_t start_block, uint32_t count) {
    if (!hsd || !hsd->initialized || !buffer || count == 0) return SD_ERROR;
    
    // Single block - use simpler function
    if (count == 1) {
        return SD_ReadBlock(hsd, buffer, start_block);
    }
    
    uint32_t addr = hsd->info.high_capacity ? start_block : (start_block * SD_BLOCK_SIZE);
    
    // CMD18 - Read Multiple Blocks
    SD_CS_Select(hsd);
    SD_SendCommand(hsd, SD_CMD18, addr);
    
    if (SD_GetResponse(hsd) != 0x00) {
        SD_CS_Deselect(hsd);
        return SD_ERROR;
    }
    
    // Read all blocks
    SD_Status status = SD_OK;
    for (uint32_t i = 0; i < count; i++) {
        if (SD_WaitDataToken(hsd) != SD_OK) {
            status = SD_ERROR_TIMEOUT;
            break;
        }
        
        status = SD_ReadBlockData_DMA(hsd, buffer + (i * SD_BLOCK_SIZE));
        if (status != SD_OK) {
            break;
        }
    }
    
    // CMD12 - Stop Transmission
    SD_SendByte(hsd, SD_DUMMY_BYTE);  // Stuff byte
    SD_SendCommand(hsd, SD_CMD12, 0);
    SD_GetResponse(hsd);
    SD_WaitReady(hsd, SD_READY_TIMEOUT_US);
    
    SD_CS_Deselect(hsd);
    return status;
}

/* ========================== DMA Callbacks ========================== */

void SD_DMA_RxComplete(SD_Handle *hsd) {
    if (hsd) {
        hsd->dma_busy = false;
    }
}

void SD_DMA_Error(SD_Handle *hsd) {
    if (hsd) {
        hsd->dma_busy = false;
        hsd->dma_error = true;
    }
}
