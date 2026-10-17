#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "wifi.h"
#include "mqtt.h"
#include "bme280.h"
#include "setup_page.h"

#define BOOT_BUTTON_GPIO 0
#define LED_GPIO         2

// Сон между замерами
#define SLEEP_SECONDS (1 * 60)

// Сколько пытаемся отправить после пробуждения, прежде чем сдаться и уснуть
#define SEND_TIMEOUT_MS (60 * 1000)

// Сколько держать BOOT, чтобы перейти в режим настройки
#define LONG_PRESS_MS 3000


typedef enum {
    LED_OFF,       // не горит
    LED_ON,        // горит
    LED_BLINK,     // мигает
    LED_FLICKER,   // мигает быстро
} led_mode_t;

static volatile led_mode_t led_mode = LED_BLINK;

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

int publish_temperature(float temperature)
{
    char text[16];
    snprintf(text, sizeof(text), "%.1f", temperature);

    int message_id = mqtt_publish("termo/temperature", text);
    if (message_id >= 0) {
        printf("Температура отправлена: %s\n", text);
    }
    return message_id;
}

/* Светодиод:
   - режим настройки        — горит
   - подключаемся/отправляем — мигает
   - отправили              — горит 1 секунду
   - не получилось          — быстро мерцает 2 секунды
   - сон                    — не горит */
void status_led_task(void *arg)
{
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    int step = 0;
    while (true) {
        led_mode_t mode = wifi_is_setup_mode() ? LED_ON : led_mode;

        bool led_on = false;
        switch (mode) {
            case LED_OFF:     led_on = false;                break;
            case LED_ON:      led_on = true;                 break;
            case LED_BLINK:   led_on = (step / 5) % 2 == 0;  break;
            case LED_FLICKER: led_on = step % 2 == 0;        break;
        }
        gpio_set_level(LED_GPIO, led_on);

        step++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Кнопка BOOT, пока плата не спит
void setup_boot_button(void)
{
    rtc_gpio_deinit(BOOT_BUTTON_GPIO);
    gpio_set_direction(BOOT_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

// Кнопка BOOT на время сна
void setup_boot_button_for_sleep(void)
{
    esp_sleep_enable_ext0_wakeup(BOOT_BUTTON_GPIO, 0);
    rtc_gpio_pullup_en(BOOT_BUTTON_GPIO);
    rtc_gpio_pulldown_dis(BOOT_BUTTON_GPIO);
}

void check_setup_button(void)
{
    static int64_t pressed_since = 0;

    if (gpio_get_level(BOOT_BUTTON_GPIO) != 0) {
        pressed_since = 0;
        return;
    }

    int64_t now = esp_timer_get_time();
    if (pressed_since == 0) {
        pressed_since = now;
    }

    if (now - pressed_since >= (int64_t)LONG_PRESS_MS * 1000) {
        printf("Долгое нажатие: переход в режим настройки\n");
        printf("Устройство будет перезагружено\n");
        wifi_request_setup_mode();
        esp_restart();
    }
}

void watch_button(void)
{
    while (true) {
        check_setup_button();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup_nvs(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    printf("NVS готова\n");
}

void print_wakeup_cause(void)
{
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER:
            printf("Проснулись по таймеру\n");
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
            printf("Проснулись от кнопки BOOT\n");
            break;
        default:
            printf("Обычное включение (не из сна)\n");
            break;
    }
}

void go_to_sleep(void)
{
    led_mode = LED_OFF;
    gpio_set_level(LED_GPIO, 0);

    mqtt_stop();
    wifi_stop();

    while (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000 * 1000);

    setup_boot_button_for_sleep();

    printf("Сплю %d с\n", SLEEP_SECONDS);
    esp_deep_sleep_start(); 
}

void measure_send_and_sleep(const wifi_credentials_t *wifi, const mqtt_settings_t *mqtt)
{
    float temperature;
    bool measured = bme280_read_temperature(&temperature);
    if (measured) {
        printf("Температура: %.2f °C\n", temperature);
    } else {
        printf("Не удалось прочитать температуру\n");
    }

    led_mode = LED_BLINK;
    wifi_connect(wifi);
    mqtt_start(mqtt);

    bool delivered = false;
    int message_id = -1;
    int64_t deadline = esp_timer_get_time() + (int64_t)SEND_TIMEOUT_MS * 1000;

    while (measured && esp_timer_get_time() < deadline) {
        check_setup_button();

        if (message_id < 0 && mqtt_is_connected()) {
            message_id = publish_temperature(temperature);
        }
        if (mqtt_is_delivered(message_id)) {
            delivered = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (delivered) {
        printf("Брокер получил температуру\n");
        led_mode = LED_ON;
        vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
        printf("Отправить не удалось, попробую после сна\n");
        led_mode = LED_FLICKER;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    go_to_sleep();
}

void app_main(void)
{
    printf("Hello world!\n");

    print_wakeup_cause();

    setup_boot_button();

    setup_bme280();

    setup_nvs();
    xTaskCreate(status_led_task, "status_led", 2048, NULL, 5, NULL);
    wifi_init();

    bool setup_requested = wifi_take_setup_request();

    wifi_credentials_t wifi;
    bool wifi_saved = wifi_load_credentials(&wifi);

    mqtt_settings_t mqtt;
    bool mqtt_saved = mqtt_load_settings(&mqtt);

    if (!setup_requested && wifi_saved && mqtt_saved) {
        measure_send_and_sleep(&wifi, &mqtt);
    } else {
        wifi_start_access_point();
        setup_page_start();
        watch_button();
    }
}
