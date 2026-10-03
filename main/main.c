#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"

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

void on_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        printf("Wi-Fi запустился\n");
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("Отключились от роутера\n");
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        printf("Получили IP!\n");
    }
}

void setup_nvs(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    printf("NVS готова\n");
}

void setup_wifi_events(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    printf("Обработчик событий Wi-Fi подключён\n");
}

void setup_network(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    printf("Сетевой стек готов\n");
}

void start_wifi(void)
{
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    printf("Hello world!\n");

    // setup_i2c();

    setup_nvs();
    setup_wifi_events();
    setup_network();

    start_wifi();
    watch_button();
}
