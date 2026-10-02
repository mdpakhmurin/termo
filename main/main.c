#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define BOOT_BUTTON_GPIO 0

void app_main(void)
{
    gpio_set_direction(BOOT_BUTTON_GPIO, GPIO_MODE_INPUT);

    int last_button_state = 1;
    while (true) {
        int current_button_state = gpio_get_level(BOOT_BUTTON_GPIO);
        if (current_button_state != last_button_state) {
            if (current_button_state == 0) {
                printf("Кнопка нажата\n");
            } else {
                printf("Кнопка отпущена\n");
            }
        }
        last_button_state = current_button_state;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
