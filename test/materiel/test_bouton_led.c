/**
 * Test boutons + LED chainable (projet 3W, groupe 3)
 *
 * À mettre à la place du contenu de src/main.c le temps du test.
 * Moniteur série : 115200 bauds (ajouter "monitor_speed = 115200" dans platformio.ini).
 *
 * Comportement :
 *  - Au démarrage (sans toucher aux boutons) : le niveau de repos de chaque bouton
 *    est mesuré, ce qui permet de savoir si "appuyé" = niveau haut ou bas.
 *  - LED verte  = mode standard.
 *  - Appui en cours : B1 -> rouge, B2 -> jaune, les deux -> violet.
 *  - Appui de 5 s sur B1 : standard <-> maintenance (orange).
 *  - Appui de 5 s sur B2 : standard <-> économique (bleu).
 *    (règle de l'avenant : chaque bouton ouvre et ferme son mode, depuis standard uniquement)
 *  - Toutes les 2 s, le niveau brut de PA10 et PB3 est affiché.
 */

#include <stdio.h>
#include <stdbool.h>
#include "setup.h"
#include "board_pins.h"
#include "grove_rgb.h"
#include "grove_button.h"

#define LONG_PRESS_MS   5000U
#define DEBOUNCE_MS     30U
#define HEARTBEAT_MS    2000U

typedef enum { MODE_STANDARD, MODE_MAINTENANCE, MODE_ECO } TestMode;

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t      pin;
    const char   *name;
    GPIO_PinState idle;       /* niveau mesuré au repos */
    GPIO_PinState lastRaw;    /* dernier niveau lu */
    uint32_t      edgeTick;   /* instant du dernier changement brut */
    bool          pressed;    /* état filtré (anti-rebond) */
    uint32_t      pressTick;  /* début de l'appui */
    bool          longDone;   /* appui de 5 s déjà traité */
} Btn;

static Btn s_btn[2] = {
    { BUTTON1_PORT, BUTTON1_PIN, "B1 (PA10)" },
    { BUTTON2_PORT, BUTTON2_PIN, "B2 (PB3)"  },
};

static TestMode s_mode = MODE_STANDARD;
static uint8_t  s_r = 1, s_g = 1, s_b = 1;   /* force la 1re écriture */

static const char *mode_name(TestMode m)
{
    switch (m)
    {
        case MODE_MAINTENANCE: return "MAINTENANCE";
        case MODE_ECO:         return "ECONOMIQUE";
        default:               return "STANDARD";
    }
}

static void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (r == s_r && g == s_g && b == s_b) return;   /* évite de renvoyer la même couleur */
    s_r = r; s_g = g; s_b = b;
    GroveRGB_SetColor(r, g, b);
}

static void btn_calibrate(void)
{
    HAL_Delay(50);  /* laisse les niveaux se stabiliser */
    for (int i = 0; i < 2; i++)
    {
        s_btn[i].idle    = HAL_GPIO_ReadPin(s_btn[i].port, s_btn[i].pin);
        s_btn[i].lastRaw = s_btn[i].idle;
        printf("%s : niveau au repos = %d -> appuye = niveau %s\r\n",
               s_btn[i].name, s_btn[i].idle,
               s_btn[i].idle == GPIO_PIN_SET ? "BAS" : "HAUT");
    }
}

static void btn_update(Btn *b, uint32_t now)
{
    GPIO_PinState raw = HAL_GPIO_ReadPin(b->port, b->pin);

    if (raw != b->lastRaw)
    {
        b->lastRaw  = raw;
        b->edgeTick = now;
    }

    bool p = (raw != b->idle);
    if (p != b->pressed && (now - b->edgeTick) >= DEBOUNCE_MS)
    {
        b->pressed = p;
        if (p)
        {
            b->pressTick = now;
            b->longDone  = false;
            printf("%s appuye (niveau %d)\r\n", b->name, raw);
        }
        else
        {
            printf("%s relache apres %lu ms\r\n", b->name, now - b->pressTick);
        }
    }
}

static void on_long_press(int index)
{
    TestMode target = (index == 0) ? MODE_MAINTENANCE : MODE_ECO;
    TestMode before = s_mode;

    if (s_mode == MODE_STANDARD)  s_mode = target;
    else if (s_mode == target)    s_mode = MODE_STANDARD;
    else
    {
        printf("%s 5 s : ignore en mode %s\r\n", s_btn[index].name, mode_name(s_mode));
        return;
    }

    printf("%s 5 s : %s -> %s\r\n", s_btn[index].name, mode_name(before), mode_name(s_mode));
}

static void led_refresh(void)
{
    bool b1 = s_btn[0].pressed && !s_btn[0].longDone;
    bool b2 = s_btn[1].pressed && !s_btn[1].longDone;

    if (b1 && b2)  { led_set(128, 0, 255); return; }   /* violet */
    if (b1)        { led_set(255, 0, 0);   return; }   /* rouge  */
    if (b2)        { led_set(255, 200, 0); return; }   /* jaune  */

    switch (s_mode)
    {
        case MODE_MAINTENANCE: led_set(255, 80, 0); break;   /* orange */
        case MODE_ECO:         led_set(0, 0, 255);  break;   /* bleu   */
        default:               led_set(0, 255, 0);  break;   /* vert   */
    }
}

int main(void)
{
    Global_Init();
    GroveRGB_Init();
    GroveButton_Init();

    printf("\r\n=== Test boutons + LED ===\r\n");
    printf("Ne touchez pas aux boutons pendant le demarrage.\r\n");
    btn_calibrate();

    uint32_t lastBeat = HAL_GetTick();

    while (1)
    {
        uint32_t now = HAL_GetTick();

        for (int i = 0; i < 2; i++)
        {
            btn_update(&s_btn[i], now);

            if (s_btn[i].pressed && !s_btn[i].longDone &&
                (now - s_btn[i].pressTick) >= LONG_PRESS_MS)
            {
                s_btn[i].longDone = true;
                on_long_press(i);
            }
        }

        led_refresh();

        if ((now - lastBeat) >= HEARTBEAT_MS)
        {
            lastBeat = now;
            printf("[brut] PA10=%d PB3=%d | mode %s\r\n",
                   HAL_GPIO_ReadPin(BUTTON1_PORT, BUTTON1_PIN),
                   HAL_GPIO_ReadPin(BUTTON2_PORT, BUTTON2_PIN),
                   mode_name(s_mode));
        }
    }
}
