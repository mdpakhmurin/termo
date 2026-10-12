#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "wifi.h"
#include "mqtt.h"
#include "mqtt_secrets.h"
#include "bme280.h"
#include "setup_page.h"

#define BOOT_BUTTON_GPIO 0
#define LED_GPIO         2

static volatile float temperature = 0;

static volatile bool need_blink = false;
static volatile bool setup_mode = false;

i2c_master_bus_handle_t setup_i2c(void)
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
    return bus;
}

void setup_bme280(void)
{
    i2c_master_bus_handle_t bus = setup_i2c();
    if (bme280_init(bus)) {
        printf("Датчик BME280 готов\n");
    }
}

void publish_temperature(void)
{
    char text[16];
    snprintf(text, sizeof(text), "%.1f", temperature);

    if (mqtt_publish("termo/temperature", text)) {
        printf("Отправили температуру: %s\n", text);
        need_blink = true;
    } else {
        printf("Нет связи с брокером, температура не отправлена\n");
    }
}

void measure_and_publish(void)
{
    float measured;
    if (bme280_read_temperature(&measured)) {
        temperature = measured;
        publish_temperature();
    } else {
        printf("Не удалось прочитать температуру\n");
    }
}

void watch_button(void)
{
    gpio_set_direction(BOOT_BUTTON_GPIO, GPIO_MODE_INPUT);

    int pressed_ticks = 0;

    while (true) {
        int current_button_state = gpio_get_level(BOOT_BUTTON_GPIO);
        if (current_button_state == 0) {
            pressed_ticks++;
            if (pressed_ticks == 30) {   // 30 × 100 мс
                printf("Долгое нажатие: сброс сети, переход в режим настройки\n");
                printf("Устройство будет перезаружено\n");
                wifi_clear_credentials();
                esp_restart();
            }
        } else {
            pressed_ticks = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup_nvs(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    printf("NVS готова\n");
}

void send_temperature_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(10000));   // время подключиться к брокеру

    while (true) {
        measure_and_publish();
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

/* Светодиод-индикатор:
   - горит постоянно в режиме настройки;
   - мигает, пока нет связи с роутером или брокером;
   - не горит, когда всё подключено;
   - мигает при каждой отправке данных */
void status_led_task(void *arg)
{
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    bool led_on = false;
    while (true) {
        bool online = wifi_is_connected() && mqtt_is_connected();

        if (setup_mode) {
            led_on = true;
            gpio_set_level(LED_GPIO, 1);
        } else if (!online) {
            led_on = !led_on;
            gpio_set_level(LED_GPIO, led_on);
        } else if (need_blink) {
            need_blink = false;
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(LED_GPIO, 0);
            led_on = false;
        } else if (led_on) {
            led_on = false;
            gpio_set_level(LED_GPIO, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void app_main(void)
{
    printf("Hello world!\n");

    setup_bme280();

    setup_nvs();
    xTaskCreate(status_led_task, "status_led", 2048, NULL, 5, NULL);
    wifi_init();

    char ssid[33];
    char password[64];
    if (wifi_load_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        wifi_connect(ssid, password);
        mqtt_start(MQTT_URI, MQTT_USERNAME, MQTT_PASSWORD);
        xTaskCreate(send_temperature_task, "send_temp", 4096, NULL, 5, NULL);
    } else {
        setup_mode = true;
        wifi_start_access_point();
        setup_page_start();
    }

    watch_button();
}
