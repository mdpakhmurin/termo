#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#define BOOT_BUTTON_GPIO 0

void setup_i2c(void)
{
    // Настройки шины I2C
    i2c_master_bus_config_t bus_config = {
        .sda_io_num = 21,                       // провод данных
        .scl_io_num = 22,                       // провод-метроном
        .i2c_port = -1,                         // какой I2C-блок взять: -1 = любой свободный
        .clk_source = I2C_CLK_SRC_DEFAULT,      // от каких внутренних часов тикать: стандартные
        .glitch_ignore_cnt = 7,                 // фильтр помех: стандартное значение
        .flags.enable_internal_pullup = true,   // включить подтяжки
    };

    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));

    printf("Шина I2C настроена\n");
}

void watch_button(void)
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

void app_main(void)
{
    printf("Hello world!\n");
    // setup_i2c();
    watch_button();
}
