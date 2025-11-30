/**
 * @file    main.c
 * @brief   Bad Apple - Full A/V Playback
 * @author  David Leathers
 * @date    November 2025 
 * Plays synchronized video and audio from SD card on STM32L476RG.
 * 
 * Hardware:
 *   - 128x64 OLED (SSD1306) via I2C2 with DMA
 *   - SD Card via SPI3 with DMA
 *   - Stereo DAC output (PA4/PA5) via DMA
 *   - TIM6 triggers DAC at 32kHz
 * 
 * Architecture:
 *   - Audio-master synchronization (video follows audio timing)
 *   - Triple-buffered display for tear-free rendering
 *   - Double-buffered audio with half-transfer interrupts
 */

#include "main.h"
#include "ssd1306.h"
#include "buffers.h"
#include "fatfs.h"
#include "sd_card.h"
#include "audio_dac.h"
#include "av_sync.h"
#include "media_file_reader.h"
#include "perf.h"
#include <string.h>
#include <stdio.h>

/* ========================== Configuration ========================== */

#define VIDEO_FPS               30
#define TIM6_PERIOD             ((80000000 / AUDIO_SAMPLE_RATE) - 1)

/* ========================== HAL Handles ========================== */

I2C_HandleTypeDef hi2c2;
SPI_HandleTypeDef hspi3;
DAC_HandleTypeDef hdac1;
TIM_HandleTypeDef htim6;

DMA_HandleTypeDef hdma_dac_ch1;
DMA_HandleTypeDef hdma_dac_ch2;
DMA_HandleTypeDef hdma_i2c2_tx;
DMA_HandleTypeDef hdma_i2c2_rx;
DMA_HandleTypeDef hdma_spi3_tx;
DMA_HandleTypeDef hdma_spi3_rx;

/* ========================== Application Handles ========================== */

SSD1306_Handle g_display;
SD_Handle g_sd;
FAT_Volume g_volume;
Audio_Handle g_audio;
MediaFile g_media;
AVSync_Handle g_avsync;

/* ========================== Statistics ========================== */

static volatile uint32_t g_max_audio_fill_us = 0;
static volatile uint32_t g_frames_rendered = 0;
static volatile uint32_t g_frames_repeated = 0;

/* ========================== Function Prototypes ========================== */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C2_Init(void);
static void MX_SPI3_Init(void);
static void MX_DAC1_Init(void);
static void MX_TIM6_Init(void);
void Error_Handler(void);

/* ========================== HAL Callbacks ========================== */

// SPI DMA complete - SD card block read finished
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi->Instance == SPI3) {
        SD_DMA_RxComplete(&g_sd);
    }
}

// SPI error - SD card DMA error
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
    if (hspi->Instance == SPI3) {
        SD_DMA_Error(&g_sd);
    }
}

// I2C DMA complete - display frame sent
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C2) {
        SSD1306_DMA_CompleteCallback(&g_display, hi2c);
    }
}

// I2C error
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C2) {
        SSD1306_DMA_ErrorCallback(&g_display, hi2c);
    }
}

/* ========================== Audio Buffer Refill ========================== */

/**
 * @brief Refill audio buffers when needed
 * 
 * Called from main loop. Checks if audio DMA has consumed a half-buffer
 * and refills it with next audio samples from media file.
 */
static void RefillAudioBuffers(void) {
    if (!audio_NeedsRefill(&g_audio)) return;
    
    uint32_t start = Perf_GetCycles();
    
    // Get buffer pointers
    Audio_BufferHalf fill_half = audio_GetFillHalf(&g_audio);
    uint16_t *left_base = audio_GetLeftBuffer(&g_audio);
    uint16_t *right_base = audio_GetRightBuffer(&g_audio);
    
    if (!left_base || !right_base) {
        return;
    }
    
    // Calculate offset into circular buffer
    uint32_t offset = (fill_half == AUDIO_BUFFER_FIRST_HALF) ? 0 : AUDIO_HALF_BUFFER_SAMPLES;
    uint16_t *left = left_base + offset;
    uint16_t *right = right_base + offset;
    
    // Read and convert audio samples
    Media_ReadAudioStereo(&g_media, left, right, AUDIO_HALF_BUFFER_SAMPLES);
    
    // Mark buffer as filled
    audio_BufferFilled(&g_audio);
    
    // Track maximum fill time
    uint32_t elapsed_us = Perf_CyclesToMicros(Perf_GetCycles() - start);
    if (elapsed_us > g_max_audio_fill_us) {
        g_max_audio_fill_us = elapsed_us;
    }
}

/* ========================== SPI Speed Control ========================== */

static void SPI3_SetSlowSpeed(void) {
    __HAL_SPI_DISABLE(&hspi3);
    hspi3.Instance->CR1 &= ~SPI_CR1_BR_Msk;
    hspi3.Instance->CR1 |= SPI_BAUDRATEPRESCALER_256;  /* ~312kHz */
    __HAL_SPI_ENABLE(&hspi3);
}

static void SPI3_SetFastSpeed(void) {
    __HAL_SPI_DISABLE(&hspi3);
    hspi3.Instance->CR1 &= ~SPI_CR1_BR_Msk;
    hspi3.Instance->CR1 |= SPI_BAUDRATEPRESCALER_8;    /* ~10MHz */
    __HAL_SPI_ENABLE(&hspi3);
}

/* ========================== Video Rendering ========================== */

/**
 * @brief Render video frame to triple buffer
 */
static void RenderVideoFrame(uint32_t frame_number) {
    uint8_t *render_buffer = Display_GetRenderBuffer();
    
    if (Media_ReadFrameAt(&g_media, frame_number, render_buffer) != FAT_OK) {
        memset(render_buffer, 0, FRAMEBUFFER_SIZE);
    }
    
    Display_SwapBuffers();
}

/**
 * @brief Start DMA transfer if frame ready
 */
static void UpdateDisplay(void) {
    if (SSD1306_IsDMABusy(&g_display)) return;
    if (!Display_HasFrame()) return;
    SSD1306_UpdateScreen_DMA(&g_display);
}

/* ========================== Main ========================== */

int main(void) {
    char buf[64];
    
    // HAL and clock init
    HAL_Init();
    SystemClock_Config();
    
    // Peripheral init
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_I2C2_Init();
    MX_SPI3_Init();
    MX_DAC1_Init();
    MX_TIM6_Init();
    
    // Initialize performance counter
    Perf_Init();
    
    // Initialize display
    if (SSD1306_Init(&g_display, &hi2c2, NULL) != SSD1306_OK) {
        while (1) {
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            HAL_Delay(100);
        }
    }
    
    // Show startup message
    SSD1306_Clear(&g_display);
    SSD1306_SetCursor(&g_display, 0, 0);
    SSD1306_WriteString(&g_display, "Bad Apple Video Player", &Font_5x7, SSD1306_COLOR_WHITE);
    SSD1306_SetCursor(&g_display, 0, 10);
    SSD1306_WriteString(&g_display, "STM32L476RG + SSD1306", &Font_5x7, SSD1306_COLOR_WHITE);
    SSD1306_UpdateScreen(&g_display);
    HAL_Delay(1000);
    
    // Initialize buffer system
    Buffers_Init();
    
    // Initialize SD card
    SSD1306_SetCursor(&g_display, 0, 20);
    SSD1306_WriteString(&g_display, "SD Init...", &Font_5x7, SSD1306_COLOR_WHITE);
    SSD1306_UpdateScreen(&g_display);
    
    SPI3_SetSlowSpeed();
    if (SD_Init(&g_sd, &hspi3, SD_CS_GPIO_Port, SD_CS_Pin) != SD_OK) {
        SSD1306_WriteString(&g_display, " FAIL", &Font_5x7, SSD1306_COLOR_WHITE);
        SSD1306_UpdateScreen(&g_display);
        while(1);
    }
    SPI3_SetFastSpeed();
    
    // Mount FAT32 filesystem
    if (FAT_Mount(&g_volume, &g_sd) != FAT_OK) {
        SSD1306_SetCursor(&g_display, 0, 30);
        SSD1306_WriteString(&g_display, "FAT FAIL", &Font_5x7, SSD1306_COLOR_WHITE);
        SSD1306_UpdateScreen(&g_display);
        while(1);
    }
    
    // Find media file
    FAT_FileInfo file_info;
    if (FAT_FindFile(&g_volume, "BADAPPLE.BIN", &file_info) != FAT_OK) {
        SSD1306_SetCursor(&g_display, 0, 30);
        SSD1306_WriteString(&g_display, "NO FILE", &Font_5x7, SSD1306_COLOR_WHITE);
        SSD1306_UpdateScreen(&g_display);
        while(1);
    }
    
    // Open media file (reads header, checks contiguity)
    if (Media_Open(&g_media, &g_volume, &file_info) != FAT_OK) {
        SSD1306_SetCursor(&g_display, 0, 30);
        SSD1306_WriteString(&g_display, "OPEN FAIL", &Font_5x7, SSD1306_COLOR_WHITE);
        SSD1306_UpdateScreen(&g_display);
        while(1);
    }
    
    // Set volume
    Media_SetVolume(&g_media, 50);
    
    // Show file info
    SSD1306_Clear(&g_display);
    SSD1306_SetCursor(&g_display, 0, 0);
    snprintf(buf, sizeof(buf), "%lu frames", (unsigned long)g_media.frame_count);
    SSD1306_WriteString(&g_display, buf, &Font_5x7, SSD1306_COLOR_WHITE);
    
    SSD1306_SetCursor(&g_display, 0, 10);
    snprintf(buf, sizeof(buf), "%luHz %luch", 
             (unsigned long)g_media.sample_rate, 
             (unsigned long)g_media.channels);
    SSD1306_WriteString(&g_display, buf, &Font_5x7, SSD1306_COLOR_WHITE);
    
    SSD1306_SetCursor(&g_display, 0, 20);
    uint32_t duration = Media_GetDurationSeconds(&g_media, VIDEO_FPS);
    snprintf(buf, sizeof(buf), "Duration: %lu:%02lu", 
             (unsigned long)(duration / 60), 
             (unsigned long)(duration % 60));
    SSD1306_WriteString(&g_display, buf, &Font_5x7, SSD1306_COLOR_WHITE);
    
    SSD1306_SetCursor(&g_display, 0, 30);
    SSD1306_WriteString(&g_display, 
                        Media_IsContiguous(&g_media) ? "CONTIGUOUS" : "FRAGMENTED", 
                        &Font_5x7, SSD1306_COLOR_WHITE);
    
    SSD1306_SetCursor(&g_display, 0, 45);
    SSD1306_WriteString(&g_display, "Starting...", &Font_5x7, SSD1306_COLOR_WHITE);
    SSD1306_UpdateScreen(&g_display);
    HAL_Delay(2000);
    
    // Initialize A/V sync (audio-master, 2-frame drift threshold)
    AVSync_Init(&g_avsync, g_media.sample_rate, VIDEO_FPS, 0);
    
    // Initialize audio driver
    audio_Init(&g_audio, &hdac1, &htim6);
    audio_SetAVSync(&g_audio, &g_avsync);
    
    // Pre-fill both audio buffer halves
    uint16_t *left_base = audio_GetLeftBuffer(&g_audio);
    uint16_t *right_base = audio_GetRightBuffer(&g_audio);
    if (left_base && right_base) {
        // Fill first half
        Media_ReadAudioStereo(&g_media, left_base, right_base, AUDIO_HALF_BUFFER_SAMPLES);
        // Fill second half
        Media_ReadAudioStereo(&g_media, 
                              left_base + AUDIO_HALF_BUFFER_SAMPLES, 
                              right_base + AUDIO_HALF_BUFFER_SAMPLES, 
                              AUDIO_HALF_BUFFER_SAMPLES);
    }
    
    // Pre-render first video frame
    RenderVideoFrame(0);
    
    // Start playback
    AVSync_Start(&g_avsync);
    audio_Start(&g_audio);
    
    /* ========================== Main Playback Loop ========================== */
    
    uint32_t last_frame = 0xFFFFFFFF;
    uint32_t frame_count = g_media.frame_count;
    bool playback_complete = false;
    
    while (!playback_complete) {
        // Always check audio first - highest priority
        RefillAudioBuffers();
        
        // Check if playback complete
        uint32_t audio_frame = AVSync_GetCurrentFrame(&g_avsync);
        if (audio_frame >= frame_count) {
            playback_complete = true;
            break;
        }
        
        // Get sync decision
        AVSync_Decision decision = AVSync_GetFrameDecision(&g_avsync);
        
        switch (decision) {
            case AVSYNC_RENDER_FRAME: {
                uint32_t current_frame = AVSync_GetCurrentFrame(&g_avsync);
                if (current_frame != last_frame && current_frame < frame_count) {
                    RenderVideoFrame(current_frame);
                    AVSync_FrameRendered(&g_avsync);
                    g_frames_rendered++;
                    last_frame = current_frame;
                }
                break;
            }
            
            case AVSYNC_SKIP_FRAME:
                AVSync_FrameSkipped(&g_avsync);
                // Skip count tracked in avsync stats
                break;
                
            case AVSYNC_REPEAT_FRAME:
                g_frames_repeated++;
                // Brief pause - video ahead of audio
                __NOP(); __NOP(); __NOP(); __NOP();
                break;
                
            default:
                break;
        }
        
        // Update display via DMA
        UpdateDisplay();
        
        // Refill audio again (do it often to avoid underruns)
        RefillAudioBuffers();
        
        // LED heartbeat
        static uint32_t led_timer = 0;
        if (HAL_GetTick() - led_timer > 500) {
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            led_timer = HAL_GetTick();
        }
    }
    
    /* ========================== Playback Complete ========================== */
    
    audio_Stop(&g_audio);
    AVSync_Stop(&g_avsync);
    Media_Close(&g_media);
    
    // Wait for display DMA to finish
    while (SSD1306_IsDMABusy(&g_display)) {
        HAL_Delay(1);
    }
    
    // Get statistics from modules
    const AVSync_Stats *sync_stats = AVSync_GetStats(&g_avsync);
    const Audio_Stats *audio_stats = audio_GetStats(&g_audio);
    
    // Show statistics
    SSD1306_Clear(&g_display);
    SSD1306_SetCursor(&g_display, 0, 0);
    SSD1306_WriteString(&g_display, "COMPLETE!", &Font_5x7, SSD1306_COLOR_WHITE);
    
    SSD1306_SetCursor(&g_display, 0, 12);
    snprintf(buf, sizeof(buf), "Rendered:%lu", (unsigned long)g_frames_rendered);
    SSD1306_WriteString(&g_display, buf, &Font_5x7, SSD1306_COLOR_WHITE);
    
    SSD1306_SetCursor(&g_display, 0, 22);
    snprintf(buf, sizeof(buf), "Skip:%lu Rep:%lu", 
             (unsigned long)(sync_stats ? sync_stats->frames_skipped : 0),
             (unsigned long)g_frames_repeated);
    SSD1306_WriteString(&g_display, buf, &Font_5x7, SSD1306_COLOR_WHITE);
    
    SSD1306_SetCursor(&g_display, 0, 32);
    snprintf(buf, sizeof(buf), "Refills:%lu", 
             (unsigned long)(audio_stats ? audio_stats->refill_count : 0));
    SSD1306_WriteString(&g_display, buf, &Font_5x7, SSD1306_COLOR_WHITE);
    
    SSD1306_SetCursor(&g_display, 0, 42);
    snprintf(buf, sizeof(buf), "Max fill:%luus", (unsigned long)g_max_audio_fill_us);
    SSD1306_WriteString(&g_display, buf, &Font_5x7, SSD1306_COLOR_WHITE);
    
    SSD1306_SetCursor(&g_display, 0, 52);
    snprintf(buf, sizeof(buf), "Underruns:%lu", 
             (unsigned long)(audio_stats ? audio_stats->underrun_count : 0));
    SSD1306_WriteString(&g_display, buf, &Font_5x7, SSD1306_COLOR_WHITE);
    
    SSD1306_UpdateScreen(&g_display);
    
    // Idle loop
    while (1) {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        HAL_Delay(1000);
    }
}

/* ========================== System Clock Configuration ========================== */

/**
 * @brief Configure system clock to 80MHz from MSI + PLL
 */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 40;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);
}

/* ========================== GPIO Init ========================== */

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // LED
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = LED_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

    // SD CS
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = SD_CS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);
}

/* ========================== DMA Init ========================== */

static void MX_DMA_Init(void) {
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    // DAC DMA - highest priority
    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 0, 0);  // DAC Ch1
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
    HAL_NVIC_SetPriority(DMA2_Channel5_IRQn, 0, 0);  // DAC Ch2
    HAL_NVIC_EnableIRQ(DMA2_Channel5_IRQn);
    
    // I2C2 DMA - medium priority
    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 3, 0);  // I2C2 TX
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 3, 0);  // I2C2 RX
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
    
    // SPI3 DMA - lower priority
    HAL_NVIC_SetPriority(DMA2_Channel1_IRQn, 5, 0);  // SPI3 RX
    HAL_NVIC_EnableIRQ(DMA2_Channel1_IRQn);
    HAL_NVIC_SetPriority(DMA2_Channel2_IRQn, 5, 0);  // SPI3 TX
    HAL_NVIC_EnableIRQ(DMA2_Channel2_IRQn);
    
    // TIM6/DAC IRQ
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

/* ========================== I2C2 Init ========================== */

static void MX_I2C2_Init(void) {
    hi2c2.Instance = I2C2;
    hi2c2.Init.Timing = 0x00B10E9C;  // 400kHz Fast Mode
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c2);
    HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE);
    HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0);
    HAL_I2CEx_EnableFastModePlus(I2C_FASTMODEPLUS_I2C2);
}

/* ========================== SPI3 Init ========================== */

static void MX_SPI3_Init(void) {
    hspi3.Instance = SPI3;
    hspi3.Init.Mode = SPI_MODE_MASTER;
    hspi3.Init.Direction = SPI_DIRECTION_2LINES;
    hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi3.Init.NSS = SPI_NSS_SOFT;
    hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi3.Init.CRCPolynomial = 7;
    hspi3.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    hspi3.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
    HAL_SPI_Init(&hspi3);
}

/* ========================== DAC1 Init ========================== */

static void MX_DAC1_Init(void) {
    DAC_ChannelConfTypeDef sConfig = {0};
    
    __HAL_RCC_DAC1_CLK_ENABLE();
    
    hdac1.Instance = DAC1;
    HAL_DAC_Init(&hdac1);
    
    sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
    sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
    sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
    
    HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1);
    HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2);
}

/* ========================== TIM6 Init ========================== */

static void MX_TIM6_Init(void) {
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    
    __HAL_RCC_TIM6_CLK_ENABLE();
    
    htim6.Instance = TIM6;
    htim6.Init.Prescaler = 0;
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period = TIM6_PERIOD;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim6);
    
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig);
}

/* ========================== Error Handler ========================== */

void Error_Handler(void) {
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        for (volatile uint32_t i = 0; i < 500000; i++);
    }
}
