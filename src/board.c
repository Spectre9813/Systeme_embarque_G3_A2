/**
 * @file board.c
 * @brief Démarrage de la carte : horloge 80 MHz et bus recalés.
 *
 * setup.c (dépôt de base) laisse le processeur sur le MSI à 4 MHz. À cette
 * fréquence, une interruption UART coûte presque la durée d'un octet à
 * 115 200 bauds et le SPI de la carte SD plafonne à 2 MHz. On passe donc à
 * 80 MHz par la PLL, puis on recalcule ce qui dépend de l'horloge.
 */
#include "board.h"
#include "setup.h"
#include "board_pins.h"
#include "stm32l4xx_hal.h"

extern I2C_HandleTypeDef hi2c1;
extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart2;

/* Valeurs de TIMINGR pour 100 kHz (outil CubeMX, I2CCLK = PCLK1). */
#define I2C_TIMING_4MHZ   0x00707CBBUL   /* celle de setup.c */
#define I2C_TIMING_80MHZ  0x10909CECUL

#define I2C_SCL_PIN       GPIO_PIN_8
#define I2C_SDA_PIN       GPIO_PIN_9
#define I2C_GPIO_PORT     GPIOB

static uint32_t s_i2cTiming = I2C_TIMING_4MHZ;

/**
 * @brief MSI 4 MHz -> PLL (M=1, N=40, R=2) = 80 MHz, 4 états d'attente Flash.
 * @return false si la PLL ne démarre pas (on reste alors à 4 MHz)
 */
static bool board_clock80MHz(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    osc.MSIState = RCC_MSI_ON;
    osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc.MSIClockRange = RCC_MSIRANGE_6;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    osc.PLL.PLLM = 1;
    osc.PLL.PLLN = 40;
    osc.PLL.PLLP = RCC_PLLP_DIV7;
    osc.PLL.PLLQ = RCC_PLLQ_DIV2;
    osc.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        return false;

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    /* HAL_RCC_ClockConfig recale aussi le SysTick sur 1 ms. */
    return HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) == HAL_OK;
}

void board_busyWaitUs(uint32_t us)
{
    /* Boucle d'attente approximative (environ 4 cycles par tour). */
    volatile uint32_t n = (SystemCoreClock / 4000000UL) * us + 1U;
    while (n--) { }
}

void board_init(void)
{
    Global_Init();                     /* dépôt de base : HAL, I2C, SPI, USART2 */

    if (board_clock80MHz())
        s_i2cTiming = I2C_TIMING_80MHZ;

    /* L'état des périphériques est READY : HAL_xxx_Init ne refait pas le
       câblage des broches, il recalcule seulement les diviseurs. */
    hi2c1.Init.Timing = s_i2cTiming;
    HAL_I2C_Init(&hi2c1);

    HAL_UART_Init(&huart2);            /* BRR recalculé pour 115 200 bauds */

    board_sdSpiFast(false);            /* 80 MHz / 256 = 312 kHz */
}

void board_sdSpiFast(bool fast)
{
    uint32_t presc;

    if (fast)
        presc = (SystemCoreClock >= 40000000UL) ? SPI_BAUDRATEPRESCALER_16
                                                : SPI_BAUDRATEPRESCALER_2;
    else
        presc = (SystemCoreClock >= 40000000UL) ? SPI_BAUDRATEPRESCALER_256
                                                : SPI_BAUDRATEPRESCALER_16;

    if (hspi1.Init.BaudRatePrescaler == presc && hspi1.State == HAL_SPI_STATE_READY)
        return;
    hspi1.Init.BaudRatePrescaler = presc;
    HAL_SPI_Init(&hspi1);
}

void board_i2cRecover(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t i;

    HAL_I2C_DeInit(&hi2c1);

    /* Un esclave peut garder SDA à 0 s'il a été coupé au milieu d'un octet :
       9 impulsions sur SCL puis une condition STOP le libèrent. */
    gpio.Pin = I2C_SCL_PIN | I2C_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(I2C_GPIO_PORT, &gpio);
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SDA_PIN, GPIO_PIN_SET);

    for (i = 0; i < 9U; ++i)
    {
        HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
        board_busyWaitUs(5);
        HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
        board_busyWaitUs(5);
    }
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
    board_busyWaitUs(5);
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    board_busyWaitUs(5);
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SDA_PIN, GPIO_PIN_SET);

    /* Broches rendues à l'I2C, comme dans MX_I2C1_Init (setup.c). */
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(I2C_GPIO_PORT, &gpio);

    hi2c1.Init.Timing = s_i2cTiming;
    HAL_I2C_Init(&hi2c1);
}

unsigned long board_cpuHz(void)
{
    return SystemCoreClock;
}
