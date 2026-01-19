#include "putchar_ra8usb.h"
#include "portmacro.h"

#define USB_BUFFER_SIZE (1024)
/*
 * IMPORTANT:
 * Logging must not block real-time/network tasks.
 * If the USB TX queue is full, drop the pending line rather than waiting.
 */
#define USB_SEND_TIMEOUT (0)

void putchar_ra8usb(uint8_t c)
{
    static char buffer[USB_BUFFER_SIZE];
    static size_t index = 0;

    // 文字をバッファに追加
    if (index < USB_BUFFER_SIZE - 1)
    {
        buffer[index++] = c;
        buffer[index] = '\0';
    }

    // バッファがいっぱい→送信
    if (c == '\n' || c == '\r' || index >= USB_BUFFER_SIZE - 1)
    {
        if (xQueueSend(xQueueMes, buffer, USB_SEND_TIMEOUT) == pdPASS)
        {
            index = 0;
            buffer[0] = '\0';
        }
        else
        {
            /* Queue is full: drop this buffered line and continue. */
            index = 0;
            buffer[0] = '\0';
        }
    }
}