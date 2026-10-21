/**
 * @file test_bme280/test_main.c
 * @brief Compensation du BME280 comparée aux formules flottantes de la fiche
 *        technique Bosch (exemple de calibration de la fiche : 25,08 °C, 100 653 Pa).
 */
#include <unity.h>
#include "../../lib/bme280/bme280.c"

static const bme280_calib C = {
    27504, 26435, -1000,
    36477, -10685, 3024, 2855, 140, -7, 15500, -14600, 6000,
    75, 0, 362, 313, 50, 30 };

void setUp(void) { }
void tearDown(void) { }

static void test_temperature(void)
{
    int32_t tf;
    TEST_ASSERT_EQUAL_INT32(2508, bme280_compTemp(&C, 519888, &tf));   /* 25,08 °C */
}

static void test_pression(void)
{
    int32_t tf;
    bme280_compTemp(&C, 519888, &tf);
    /* fiche technique, version flottante : 100 653,27 Pa */
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 100653.27f, bme280_compPress(&C, 415148, tf) / 256.0f);
}

static void test_humidite(void)
{
    int32_t tf;
    bme280_compTemp(&C, 519888, &tf);
    /* formule flottante de la fiche (§8.1) avec ces coefficients : 55,0007 %HR */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 55.0f, bme280_compHum(&C, 30000, tf) / 1024.0f);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_temperature);
    RUN_TEST(test_pression);
    RUN_TEST(test_humidite);
    return UNITY_END();
}
