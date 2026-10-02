#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define I2C_SDA_GPIO        21
#define I2C_SCL_GPIO        22
#define I2C_FREQ_HZ         100000  // 100 кГц — стандартный режим, самый надёжный для начала

#define BME280_REG_CHIP_ID  0xD0    // регистр "кто ты?"; у BME280 там всегда 0x60

static const char *TAG = "termo";

void app_main(void)
{
    // 1. Настраиваем контроллер I2C (шину) на ножках 21/22
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,                     // -1 = драйвер сам выберет свободный I2C-контроллер
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,             // фильтр коротких помех на линиях
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    // 2. Сканируем шину: стучимся по каждому адресу и смотрим, кто ответит ACK
    uint8_t bme_addr = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "Устройство на адресе 0x%02X", addr);
            if (addr == 0x76 || addr == 0x77) {
                bme_addr = addr;
            }
        }
    }
    if (bme_addr == 0) {
        ESP_LOGE(TAG, "BME280 не найден. Проверь провода, питание 3V3 и GND");
        return;
    }

    // 3. Регистрируем датчик как устройство на шине
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = bme_addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t bme;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &bme));

    // 4. Читаем один байт из регистра 0xD0: пишем номер регистра, затем читаем ответ
    uint8_t reg = BME280_REG_CHIP_ID;
    uint8_t chip_id = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(bme, &reg, 1, &chip_id, 1, 100));

    switch (chip_id) {
    case 0x60: ESP_LOGI(TAG, "Chip ID 0x60: это настоящий BME280"); break;
    case 0x58: ESP_LOGW(TAG, "Chip ID 0x58: это BMP280, влажности не будет"); break;
    default:   ESP_LOGW(TAG, "Неожиданный Chip ID 0x%02X", chip_id); break;
    }
}
