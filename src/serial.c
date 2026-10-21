/**
 * @file serial.c
 * @brief Réception par interruption dans deux tampons circulaires.
 *
 * L'interruption range l'octet et relance la réception, rien de plus. Un
 * seul écrivain par index (head : interruption, tail : boucle), donc pas de
 * section critique : sur Cortex-M4, la lecture d'un uint16_t est atomique.
 */
#include "serial.h"
#include "constants.h"
#include "stm32l4xx_hal.h"
#include <string.h>

extern UART_HandleTypeDef huart2;            /* setup.c : console */
extern UART_HandleTypeDef GPS_UART_HANDLE;   /* grove_gps_air530z.c : UART4 */

#define RX_MASK (RX_BUFFER_SIZE - 1U)
#if (RX_BUFFER_SIZE & RX_MASK) != 0
#error "RX_BUFFER_SIZE doit être une puissance de 2"
#endif

typedef struct
{
    uint8_t buf[RX_BUFFER_SIZE];
    volatile uint16_t head;      /* écrit par l'interruption */
    volatile uint16_t tail;      /* écrit par la boucle */
    volatile uint32_t overflows;
    uint8_t byte;                /* octet en cours de réception */
} RxRing;

static RxRing s_rx[SERIAL_PORT_COUNT];

/* Ligne en cours d'assemblage, par port. */
static char s_partial[SERIAL_PORT_COUNT][NMEA_LINE_MAX];
static uint8_t s_partialLen[SERIAL_PORT_COUNT];
static bool s_partialTooLong[SERIAL_PORT_COUNT];

static UART_HandleTypeDef *serial_handle(SerialPort port)
{
    return (port == SERIAL_CONSOLE) ? &huart2 : &GPS_UART_HANDLE;
}

static IRQn_Type serial_irq(SerialPort port)
{
    return (port == SERIAL_CONSOLE) ? USART2_IRQn : UART4_IRQn;
}

void serial_init(void)
{
    memset(s_rx, 0, sizeof(s_rx));
    serial_enable(SERIAL_CONSOLE);
}

void serial_enable(SerialPort port)
{
    HAL_NVIC_SetPriority(serial_irq(port), 1, 0);
    HAL_NVIC_EnableIRQ(serial_irq(port));
    HAL_UART_Receive_IT(serial_handle(port), &s_rx[port].byte, 1);
}

static void serial_push(SerialPort port)
{
    RxRing *r = &s_rx[port];
    uint16_t next = (uint16_t)((r->head + 1U) & RX_MASK);

    if (next == r->tail)
        r->overflows++;
    else
    {
        r->buf[r->head] = r->byte;
        r->head = next;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    SerialPort p;
    for (p = SERIAL_CONSOLE; p < SERIAL_PORT_COUNT; p++)
    {
        if (huart == serial_handle(p))
        {
            serial_push(p);
            HAL_UART_Receive_IT(huart, &s_rx[p].byte, 1);
        }
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    /* Débordement ou bruit : la HAL a arrêté la réception, on la relance. */
    SerialPort p;
    for (p = SERIAL_CONSOLE; p < SERIAL_PORT_COUNT; p++)
        if (huart == serial_handle(p))
            HAL_UART_Receive_IT(huart, &s_rx[p].byte, 1);
}

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

void UART4_IRQHandler(void)
{
    HAL_UART_IRQHandler(&GPS_UART_HANDLE);
}

bool serial_readLine(SerialPort port, char *buf, size_t len)
{
    RxRing *r = &s_rx[port];
    size_t max = sizeof(s_partial[port]) - 1U;

    while (r->tail != r->head)
    {
        char c = (char)r->buf[r->tail];
        r->tail = (uint16_t)((r->tail + 1U) & RX_MASK);

        if (c == '\r' || c == '\n')
        {
            bool tooLong = s_partialTooLong[port];
            size_t n = s_partialLen[port];

            s_partialLen[port] = 0;
            s_partialTooLong[port] = false;
            if (n == 0U || tooLong)
                continue;          /* ligne vide, ou trop longue : ignorée */
            if (n >= len)
                n = len - 1U;
            memcpy(buf, s_partial[port], n);
            buf[n] = '\0';
            return true;
        }

        if (s_partialLen[port] < max)
            s_partial[port][s_partialLen[port]++] = c;
        else
            s_partialTooLong[port] = true;
    }
    return false;
}

void serial_write(const char *text)
{
    size_t n = strlen(text);
    if (n > 0U)
        HAL_UART_Transmit(&huart2, (const uint8_t *)text, (uint16_t)n, UART_TX_TIMEOUT_MS);
}

uint32_t serial_overflows(SerialPort port)
{
    return s_rx[port].overflows;
}
