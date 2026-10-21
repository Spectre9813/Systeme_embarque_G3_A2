/**
 * @file rtc.c
 * @brief Horloge DS1307 : lecture contrôlée et réglage champ par champ.
 */
#include "rtc.h"
#include "board.h"
#include "constants.h"
#include "grove_rtc_ds1307.h"
#include "board_pins.h"
#include "stm32l4xx_hal.h"

extern I2C_HandleTypeDef GROVE_I2C_HANDLE;

#define DS1307_ADDR     (0x68U << 1)
#define DS1307_REG_SEC  0x00U
#define DS1307_CH_BIT   0x80U      /* 1 : oscillateur arrêté */

bool rtc_isValidDate(uint8_t month, uint8_t day, uint16_t year)
{
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint8_t max;

    if (year < 2000U || year > 2099U || month < 1U || month > 12U || day < 1U)
        return false;
    max = days[month - 1U];
    if (month == 2U && (year % 4U) == 0U)   /* suffisant entre 2000 et 2099 */
        max = 29U;
    return day <= max;
}

static bool rtc_isValid(const RTC_DateTime *dt)
{
    return dt->hours < 24U && dt->minutes < 60U && dt->seconds < 60U &&
           dt->dayOfWeek >= 1U && dt->dayOfWeek <= 7U &&
           rtc_isValidDate(dt->month, dt->day, dt->year);
}

bool rtc_init(void)
{
    uint8_t sec;

    if (!GroveRTC_Init())
        return false;

    /* Pile neuve ou retirée : le bit CH est à 1 et l'heure n'avance plus. */
    if (HAL_I2C_Mem_Read(&GROVE_I2C_HANDLE, DS1307_ADDR, DS1307_REG_SEC,
                         I2C_MEMADD_SIZE_8BIT, &sec, 1, I2C_TIMEOUT_MS) != HAL_OK)
        return false;
    if (sec & DS1307_CH_BIT)
    {
        sec &= (uint8_t)~DS1307_CH_BIT;
        if (HAL_I2C_Mem_Write(&GROVE_I2C_HANDLE, DS1307_ADDR, DS1307_REG_SEC,
                              I2C_MEMADD_SIZE_8BIT, &sec, 1, I2C_TIMEOUT_MS) != HAL_OK)
            return false;
    }
    return true;
}

bool rtc_now(RTC_DateTime *dt)
{
    if (!GroveRTC_GetDateTime(dt))
    {
        board_i2cRecover();
        return false;
    }
    /* Module débranché : le bus lit 0xFF partout, d'où une date impossible. */
    return rtc_isValid(dt);
}

/** @brief Lit l'heure, en remplaçant une valeur impossible par une valeur sûre. */
static bool rtc_readForUpdate(RTC_DateTime *dt)
{
    if (!GroveRTC_GetDateTime(dt))
    {
        board_i2cRecover();
        return false;
    }
    if (dt->hours > 23U || dt->minutes > 59U || dt->seconds > 59U)
    {
        dt->hours = 0; dt->minutes = 0; dt->seconds = 0;
    }
    if (dt->dayOfWeek < 1U || dt->dayOfWeek > 7U)
        dt->dayOfWeek = 1;
    if (!rtc_isValidDate(dt->month, dt->day, dt->year))
    {
        dt->day = 1; dt->month = 1; dt->year = 2026;
    }
    return true;
}

bool rtc_setTime(uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    RTC_DateTime dt;
    if (!rtc_readForUpdate(&dt))
        return false;
    dt.hours = hours;
    dt.minutes = minutes;
    dt.seconds = seconds;
    return GroveRTC_SetDateTime(&dt);
}

bool rtc_setDate(uint8_t month, uint8_t day, uint16_t year)
{
    RTC_DateTime dt;
    if (!rtc_readForUpdate(&dt))
        return false;
    dt.month = month;
    dt.day = day;
    dt.year = year;
    return GroveRTC_SetDateTime(&dt);
}

bool rtc_setDay(uint8_t dayOfWeek)
{
    RTC_DateTime dt;
    if (!rtc_readForUpdate(&dt))
        return false;
    dt.dayOfWeek = dayOfWeek;
    return GroveRTC_SetDateTime(&dt);
}
