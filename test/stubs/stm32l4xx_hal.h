/**
 * @file stm32l4xx_hal.h (tests sur PC)
 * @brief Remplace la HAL de ST : juste ce qu'utilisent les modules testés.
 *        Les fonctions sont simulées dans fakes.c.
 */
#ifndef FAKE_STM32L4XX_HAL_H
#define FAKE_STM32L4XX_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef int IRQn_Type;

/* ---- Temps ---- */
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);

/* ---- GPIO ---- */
typedef struct { int id; } GPIO_TypeDef;
extern GPIO_TypeDef fakeGPIOA, fakeGPIOB;
#define GPIOA (&fakeGPIOA)
#define GPIOB (&fakeGPIOB)
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
#define GPIO_PIN_0  0x0001U
#define GPIO_PIN_1  0x0002U
#define GPIO_PIN_2  0x0004U
#define GPIO_PIN_3  0x0008U
#define GPIO_PIN_4  0x0010U
#define GPIO_PIN_5  0x0020U
#define GPIO_PIN_8  0x0100U
#define GPIO_PIN_9  0x0200U
#define GPIO_PIN_10 0x0400U
#define GPIO_MODE_IT_RISING_FALLING 1
#define GPIO_MODE_INPUT 0
#define GPIO_MODE_ANALOG_ADC_CONTROL 11
#define GPIO_NOPULL 0
#define GPIO_PULLUP 1
#define GPIO_PULLDOWN 2
#define GPIO_SPEED_FREQ_LOW 0
#define __HAL_RCC_GPIOA_CLK_ENABLE() do { } while (0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() do { } while (0)
enum { EXTI0_IRQn = 6, EXTI1_IRQn, EXTI2_IRQn, EXTI3_IRQn, EXTI4_IRQn, EXTI9_5_IRQn = 23, EXTI15_10_IRQn = 40 };
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState s);
void HAL_GPIO_EXTI_IRQHandler(uint16_t pin);
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t p, uint32_t s);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);

/* ---- I2C ---- */
typedef struct { int id; } I2C_HandleTypeDef;
#define I2C_MEMADD_SIZE_8BIT 1U
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t addr, uint16_t reg, uint16_t sz,
                                   uint8_t *data, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t addr, uint16_t reg, uint16_t sz,
                                    uint8_t *data, uint16_t len, uint32_t timeout);

/* ---- ADC ---- */
typedef struct { int id; } ADC_HandleTypeDef;
#define ADC_SINGLE_ENDED 0U
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h, uint32_t mode);

/* ---- Flash ---- */
typedef struct { uint32_t TypeErase, Banks, Page, NbPages; } FLASH_EraseInitTypeDef;
#define FLASH_TYPEERASE_PAGES 0U
#define FLASH_BANK_2 2U
#define FLASH_BASE 0U
#define FLASH_BANK_SIZE 0U
#define FLASH_PAGE_SIZE 0x800U
#define FLASH_TYPEPROGRAM_DOUBLEWORD 0U
#define FLASH_FLAG_ALL_ERRORS 0U
#define __HAL_FLASH_CLEAR_FLAG(f) do { } while (0)
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *err);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uintptr_t addr, uint64_t data);

#endif
