/**
 * @file buttons.c
 * @brief Boutons rouge et vert : interruption sur chaque front, filtrage et
 *        mesure de l'appui dans la boucle.
 *
 * L'interruption ne fait que noter l'instant du front. La boucle valide le
 * nouvel état quand il est stable depuis DEBOUNCE_MS, puis produit un
 * événement unique dès que l'appui atteint LONG_PRESS_MS (sans attendre le
 * relâchement : la LED change de couleur pendant que l'on appuie).
 *
 * Niveau électrique : comme le driver grove_button, entrées avec tirage vers
 * le bas. Le niveau "appuyé" est déduit au démarrage du niveau de repos du
 * bouton vert, que personne ne tient à ce moment (le rouge peut l'être pour
 * entrer en configuration) ; les deux boutons du module ont la même logique.
 */
#include "buttons.h"
#include "constants.h"
#include "board_pins.h"
#include "serial.h"
#include <stdio.h>
#include "stm32l4xx_hal.h"

#define BTN_RED   0U
#define BTN_GREEN 1U
#define BTN_COUNT 2U

typedef struct { GPIO_TypeDef *port; uint16_t pin; } BtnPin;

#if RED_IS_BUTTON1
static const BtnPin s_pins[BTN_COUNT] = {
    {BUTTON1_PORT, BUTTON1_PIN}, {BUTTON2_PORT, BUTTON2_PIN} };
#else
static const BtnPin s_pins[BTN_COUNT] = {
    {BUTTON2_PORT, BUTTON2_PIN}, {BUTTON1_PORT, BUTTON1_PIN} };
#endif

static volatile uint32_t s_edgeTick[BTN_COUNT];  /* écrit par l'interruption */
static bool s_stable[BTN_COUNT];                  /* état filtré */
static uint32_t s_pressStart[BTN_COUNT];
static bool s_reported[BTN_COUNT];
static Event s_pending = EVT_NONE;
static GPIO_PinState s_pressedLevel = GPIO_PIN_RESET;

static bool buttons_read(uint8_t i)
{
    return HAL_GPIO_ReadPin(s_pins[i].port, s_pins[i].pin) == s_pressedLevel;
}

static const char *buttons_name(uint8_t i)
{
    return (i == BTN_RED) ? "rouge" : "vert";
}

static void buttons_trace(const char *text)
{
#if TRACE_ENABLED
    serial_write(text);
#else
    (void)text;
#endif
}

static IRQn_Type buttons_irq(uint16_t pin)
{
    switch (pin)
    {
    case GPIO_PIN_0: return EXTI0_IRQn;
    case GPIO_PIN_1: return EXTI1_IRQn;
    case GPIO_PIN_2: return EXTI2_IRQn;
    case GPIO_PIN_3: return EXTI3_IRQn;
    case GPIO_PIN_4: return EXTI4_IRQn;
    default:         return (pin <= GPIO_PIN_9) ? EXTI9_5_IRQn : EXTI15_10_IRQn;
    }
}

void buttons_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t i;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLDOWN;              /* comme le driver grove_button */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    for (i = 0; i < BTN_COUNT; i++)
    {
        gpio.Pin = s_pins[i].pin;
        HAL_GPIO_Init(s_pins[i].port, &gpio);
        HAL_NVIC_SetPriority(buttons_irq(s_pins[i].pin), 2, 0);
        HAL_NVIC_EnableIRQ(buttons_irq(s_pins[i].pin));
    }

    HAL_Delay(BOOT_SAMPLE_MS);              /* niveaux stabilisés */
#if BUTTON_PRESSED_LEVEL < 0
    s_pressedLevel = (HAL_GPIO_ReadPin(s_pins[BTN_GREEN].port, s_pins[BTN_GREEN].pin) == GPIO_PIN_SET)
                     ? GPIO_PIN_RESET : GPIO_PIN_SET;
#else
    s_pressedLevel = BUTTON_PRESSED_LEVEL ? GPIO_PIN_SET : GPIO_PIN_RESET;
#endif
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "Boutons : appuye = niveau %s\r\n",
                 s_pressedLevel == GPIO_PIN_SET ? "haut" : "bas");
        buttons_trace(msg);
    }

    for (i = 0; i < BTN_COUNT; i++)
    {
        s_stable[i] = buttons_read(i);
        s_pressStart[i] = HAL_GetTick();
        s_reported[i] = true;   /* un bouton tenu au démarrage ne compte pas comme appui long */
    }
}

bool buttons_redHeldAtBoot(void)
{
    uint32_t i;
    for (i = 0; i < BOOT_SAMPLE_MS; i++)    /* appuyé pendant toute la durée */
    {
        if (!buttons_read(BTN_RED))
            return false;
        HAL_Delay(1);
    }
    return true;
}

void buttons_update(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t i;

    for (i = 0; i < BTN_COUNT; i++)
    {
        bool raw = buttons_read(i);

        if (raw != s_stable[i] && (now - s_edgeTick[i]) >= DEBOUNCE_MS)
        {
            char msg[48];
            s_stable[i] = raw;
            if (raw)
            {
                s_pressStart[i] = now;
                s_reported[i] = false;
                snprintf(msg, sizeof(msg), "[BOUTON] %s appuye\r\n", buttons_name(i));
            }
            else
                snprintf(msg, sizeof(msg), "[BOUTON] %s relache apres %lu ms\r\n",
                         buttons_name(i), (unsigned long)(now - s_pressStart[i]));
            buttons_trace(msg);
        }

        if (s_stable[i] && !s_reported[i] && (now - s_pressStart[i]) >= LONG_PRESS_MS)
        {
            s_reported[i] = true;
            s_pending = (i == BTN_RED) ? EVT_RED_LONG : EVT_GREEN_LONG;
        }
    }
}

Event buttons_getEvent(void)
{
    Event e = s_pending;
    s_pending = EVT_NONE;
    return e;
}

uint32_t buttons_heldMs(uint8_t index)
{
    if (index >= BTN_COUNT || !s_stable[index] || s_reported[index])
        return 0;
    return HAL_GetTick() - s_pressStart[index] + 1U;
}

bool buttons_isPressed(uint8_t index)
{
    return index < BTN_COUNT && s_stable[index];
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    uint8_t i;
    for (i = 0; i < BTN_COUNT; i++)
        if (s_pins[i].pin == pin)
            s_edgeTick[i] = HAL_GetTick();
}

void EXTI3_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);
}

void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);
}
