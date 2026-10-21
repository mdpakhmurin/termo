#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "wifi.h"
#include "mqtt.h"
#include "bme280.h"
#include "setup_page.h"

// ---------- Настройки ----------

#define BUTTON_GPIO 9

#define LED_GPIO     15
#define LED_ON_LEVEL 0

// Датчик BME280: SDA → D4, SCL → D5
#define I2C_SDA_GPIO 22
#define I2C_SCL_GPIO 23

#define ANTENNA_SWITCH_GPIO     3  // питание переключателя антенн: 0 — включён
#define ANTENNA_SELECT_GPIO     14 // выбор антенны: 0 — встроенная, 1 — внешняя

// Сон между замерами
#define SLEEP_SECONDS (10 * 60)

// Сколько пытаемся отправить после пробуждения, прежде чем сдаться и уснуть
#define SEND_TIMEOUT_MS (60 * 1000)

// Сколько держать кнопку, чтобы перейти в режим настройки
#define LONG_PRESS_MS 3000

// Давление в мм рт. ст. 1 гПа = 0,750 мм рт. ст.
#define HPA_TO_MMHG 0.750062f

// Количество сообщений брокеру: температура, давление, влажность, «ощущается как»
#define MAX_MESSAGES 4

// ---------- Типы ----------

typedef enum {
    LED_OFF,       // не горит
    LED_ON,        // горит
    LED_BLINK,     // мигает
    LED_FLICKER,   // мигает быстро
} led_mode_t;

typedef struct {
    const char *topic;
    char value[16];
} message_t;

// ---------- Общие переменные ----------

static volatile led_mode_t led_mode = LED_BLINK;

i2c_master_bus_handle_t setup_i2c(void)
{
    // Настройки шины I2C
    i2c_master_bus_config_t bus_config = {
        .sda_io_num = I2C_SDA_GPIO,             // провод данных
        .scl_io_num = I2C_SCL_GPIO,             // провод-метроном
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

float feels_like(float temperature, float humidity)
{
    float vapor = humidity / 100.0f * 6.105f * expf(17.27f * temperature / (237.7f + temperature));
    return temperature + 0.33f * vapor - 4.0f;
}

// Разложить замер по сообщениям
int make_messages(const bme280_data_t *data, message_t *messages)
{
    int count = 0;

    messages[count].topic = "termo/temperature";
    snprintf(messages[count].value, sizeof(messages[count].value), "%.1f", data->temperature);
    count++;

    messages[count].topic = "termo/pressure";
    snprintf(messages[count].value, sizeof(messages[count].value), "%.0f", data->pressure * HPA_TO_MMHG);
    count++;

    if (!isnan(data->humidity)) {
        messages[count].topic = "termo/humidity";
        snprintf(messages[count].value, sizeof(messages[count].value), "%.0f", data->humidity);
        count++;

        messages[count].topic = "termo/feels_like";
        snprintf(messages[count].value, sizeof(messages[count].value), "%.1f",
                 feels_like(data->temperature, data->humidity));
        count++;
    }

    return count;
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
        gpio_set_level(LED_GPIO, led_on ? LED_ON_LEVEL : !LED_ON_LEVEL);

        step++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Настроить антенну
void setup_antenna(void)
{
    gpio_set_direction(ANTENNA_SWITCH_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ANTENNA_SWITCH_GPIO, 0);   // 0 — включить переключатель

    gpio_set_direction(ANTENNA_SELECT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ANTENNA_SELECT_GPIO, 0);   // 0 — встроенная антенна (1 — внешняя)
}

// Кнопка BOOT
void setup_button(void)
{
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

void check_setup_button(void)
{
    static int64_t pressed_since = 0;

    if (gpio_get_level(BUTTON_GPIO) != 0) {
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
    uint32_t causes = esp_sleep_get_wakeup_causes();

    if (causes & (1 << ESP_SLEEP_WAKEUP_TIMER)) {
        printf("Проснулись по таймеру\n");
    } else {
        printf("Обычное включение или RESET (не из сна)\n");
    }
}

void go_to_sleep(void)
{
    while (gpio_get_level(BUTTON_GPIO) == 0) {
        check_setup_button();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    led_mode = LED_OFF;
    gpio_set_level(LED_GPIO, !LED_ON_LEVEL);

    mqtt_stop();
    wifi_stop();

    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000 * 1000);

    printf("Сплю %d с\n", SLEEP_SECONDS);
    esp_deep_sleep_start(); 
}

void measure_send_and_sleep(const wifi_credentials_t *wifi, const mqtt_settings_t *mqtt)
{
    bme280_data_t data;
    bool measured = bme280_read(&data);

    message_t messages[MAX_MESSAGES];
    int total = 0;
    if (measured) {
        printf("Температура %.2f °C, давление %.1f гПа, влажность %.1f %%\n",
               data.temperature, data.pressure, data.humidity);
        total = make_messages(&data, messages);
    } else {
        printf("Не удалось произвести измерения\n");
    }

    led_mode = LED_BLINK;
    wifi_connect(wifi);
    mqtt_start(mqtt);

    bool delivered = false;
    int current = 0;    
    int message_id = -1;
    int64_t deadline = esp_timer_get_time() + (int64_t)SEND_TIMEOUT_MS * 1000;

    while (measured && esp_timer_get_time() < deadline) {
        check_setup_button();

        if (message_id < 0 && mqtt_is_connected()) {
            message_id = mqtt_publish(messages[current].topic, messages[current].value);
            if (message_id >= 0) {
                printf("Отправили %s = %s\n", messages[current].topic, messages[current].value);
            }
        }

        if (mqtt_is_delivered(message_id)) {
            current++;
            message_id = -1;
            if (current == total) {
                delivered = true;
                break;
            }
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (delivered) {
        printf("Брокер получил все данные\n");
        led_mode = LED_ON;
        vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
        if (measured) {
            printf("Не дошло %s (и всё после него) \n",
                   messages[current].topic);
        } else {
            printf("Датчик не ответил. Замер пропущен\n");
        }
        led_mode = LED_FLICKER;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    go_to_sleep();
}

void app_main(void)
{
    printf("Hello world!\n");

    setup_antenna();
    print_wakeup_cause();

    setup_button();

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
