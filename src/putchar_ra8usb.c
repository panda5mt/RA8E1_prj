#include "putchar_ra8usb.h"
#include "portmacro.h"

#define USB_BUFFER_SIZE (1024)
#define USB_SEND_TIMEOUT pdMS_TO_TICKS(5000)

void putchar_ra8usb(uint8_t c)
{
    static char buffer[USB_BUFFER_SIZE];
    static size_t index = 0;
    static bool skip = false;

    if (skip)
        return;

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
            // 送信に失敗 → 次回以降スキップ
            skip = true;
        }
    }
}