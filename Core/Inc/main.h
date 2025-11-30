/**
 * @file    main.h
 * @brief   Main header - extern declarations for peripheral handles
 * @author  David Leathers
 * @date    November 2025
 */

#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l4xx_hal.h"

// Peripheral handles - defined in main.c
extern I2C_HandleTypeDef hi2c2;
extern SPI_HandleTypeDef hspi3;
extern DAC_HandleTypeDef hdac1;
extern TIM_HandleTypeDef htim6;

extern DMA_HandleTypeDef hdma_dac_ch1;
extern DMA_HandleTypeDef hdma_dac_ch2;
extern DMA_HandleTypeDef hdma_i2c2_tx;
extern DMA_HandleTypeDef hdma_i2c2_rx;
extern DMA_HandleTypeDef hdma_spi3_tx;
extern DMA_HandleTypeDef hdma_spi3_rx;

/* ========================== Pin Definitions ========================== */

// SD Card SPI3 pins (directly used in HAL_SPI_MspInit)
#define SD_SCK_Pin          GPIO_PIN_10
#define SD_SCK_GPIO_Port    GPIOC
#define SD_MISO_Pin         GPIO_PIN_11
#define SD_MISO_GPIO_Port   GPIOC
#define SD_MOSI_Pin         GPIO_PIN_12
#define SD_MOSI_GPIO_Port   GPIOC

// SD Card CS pin (directly controlled in sd_card.c)
#define SD_CS_Pin           GPIO_PIN_9
#define SD_CS_GPIO_Port     GPIOA

// OLED I2C2 pins (directly used in HAL_I2C_MspInit)
#define OLED_SCL_Pin        GPIO_PIN_13
#define OLED_SCL_GPIO_Port  GPIOB
#define OLED_SDA_Pin        GPIO_PIN_14
#define OLED_SDA_GPIO_Port  GPIOB

// LED pin
#define LED_Pin             GPIO_PIN_3
#define LED_GPIO_Port       GPIOB

/* ========================== Function Prototypes ========================== */

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif // MAIN_H
