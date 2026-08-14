/* otPlatUart для Spinel/HDLC: UART0 JN516x, RX по прерыванию, кольцевой буфер */
#include <jendefs.h>
#include <AppHardwareApi.h>

#include <stddef.h>
#include <stdint.h>

#include <openthread/error.h>
#include <utils/uart.h>
#include "platform-jn5169.h"

#define RX_RING_SIZE 4096u

static uint8_t           sRxRing[RX_RING_SIZE];
static volatile uint16_t sRxHead; /* пишет ISR */
static volatile uint16_t sRxTail; /* читает главный цикл */

static const uint8_t *sTxData;
static uint16_t       sTxLen;
static uint16_t       sTxIdx; /* сколько байт кадра уже залито в FIFO */

static void uart0Isr(uint32 aDevice, uint32 aItemBitmap)
{
    (void)aDevice;
    (void)aItemBitmap;

    while (u8AHI_UartReadLineStatus(E_AHI_UART_0) & E_AHI_UART_LS_DR)
    {
        uint8_t  b    = u8AHI_UartReadData(E_AHI_UART_0);
        uint16_t next = (uint16_t)((sRxHead + 1) % RX_RING_SIZE);

        if (next != sRxTail)
        {
            sRxRing[sRxHead] = b;
            sRxHead          = next;
        }
        /* переполнение: байт молча теряем, HDLC переспросит кадр */
    }
}

void jn5169UartInit(void)
{
    sRxHead = sRxTail = 0;
    sTxData = NULL;
    sTxLen  = 0;
    sTxIdx  = 0;

    vAHI_UartEnable(E_AHI_UART_0);
    vAHI_UartReset(E_AHI_UART_0, TRUE, TRUE);
    vAHI_UartReset(E_AHI_UART_0, FALSE, FALSE);
    vAHI_UartSetClockDivisor(E_AHI_UART_0, E_AHI_UART_RATE_115200);
    vAHI_Uart0RegisterCallback(uart0Isr);
    vAHI_UartSetInterrupt(E_AHI_UART_0,
                          FALSE, /* modem status */
                          FALSE, /* rx line status */
                          FALSE, /* tx fifo empty */
                          TRUE,  /* rx data */
                          E_AHI_UART_FIFO_LEVEL_1);
}

otError otPlatUartEnable(void)
{
    return OT_ERROR_NONE;
}

otError otPlatUartDisable(void)
{
    return OT_ERROR_NONE;
}

otError otPlatUartSend(const uint8_t *aBuf, uint16_t aBufLength)
{
    sTxData = aBuf;
    sTxLen  = aBufLength;
    sTxIdx  = 0;
    return OT_ERROR_NONE;
}

otError otPlatUartFlush(void)
{
    while (!(u8AHI_UartReadLineStatus(E_AHI_UART_0) & E_AHI_UART_LS_THRE))
    {
    }
    return OT_ERROR_NONE;
}

void jn5169UartProcess(void)
{
    /* RX: отдать накопленное ядру OT (кусками до конца кольца) */
    while (sRxTail != sRxHead)
    {
        uint16_t head = sRxHead;
        uint16_t len;

        if (sRxTail < head)
        {
            len = (uint16_t)(head - sRxTail);
        }
        else
        {
            len = (uint16_t)(RX_RING_SIZE - sRxTail);
        }

        otPlatUartReceived(&sRxRing[sRxTail], len);
        sRxTail = (uint16_t)((sRxTail + len) % RX_RING_SIZE);
    }

    /* TX: НЕблокирующая отправка. При THRE (TX FIFO пуст) заливаем до 16 байт и
     * возвращаемся в главный цикл — иначе busy-wait на весь кадр (~22мс на 115200)
     * морозит разбор RX и радио, RX-кольцо переполняется под burst-ом → HDLC-десинк. */
    if (sTxData != NULL)
    {
        if (u8AHI_UartReadLineStatus(E_AHI_UART_0) & E_AHI_UART_LS_THRE)
        {
            uint16_t n = 0;
            while (sTxIdx < sTxLen && n < 16u)
            {
                vAHI_UartWriteData(E_AHI_UART_0, sTxData[sTxIdx]);
                sTxIdx++;
                n++;
            }
        }

        if (sTxIdx >= sTxLen)
        {
            sTxData = NULL;
            sTxLen  = 0;
            sTxIdx  = 0;
            otPlatUartSendDone();
        }
    }
}
