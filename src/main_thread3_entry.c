#include "main_thread3.h"
                /* Main Thread3 entry function */
                /* pvParameters contains TaskHandle_t */
                void main_thread3_entry(void * pvParameters)
                {
                    FSP_PARAMETER_NOT_USED(pvParameters);

                    /* TODO: add your own code here */
                    while(1)
                    {
                        vTaskDelay(1);
                    }
                }
