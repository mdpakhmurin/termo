#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bme280.h"

#define ADDRESS 0x76   // адрес модуля

// Регистры датчика
#define REG_CHIP_ID   0xD0   // тип: BME280 или BMP280
#define REG_CALIB_T   0x88   // заводская калибровка температуры (6 байт)
#define REG_CTRL_MEAS 0xF4   // команда «измерь»
#define REG_TEMP      0xFA   // сырая температура (3 байта)

#define CHIP_ID_BME280 0x60
#define CHIP_ID_BMP280 0x58

#define TIMEOUT_MS 100

static i2c_master_dev_handle_t device = NULL;

// калибровочные числа с датчика
static uint16_t dig_T1;
static int16_t  dig_T2;
static int16_t  dig_T3;

static bool read_registers(uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(device, &reg, 1, data, length, TIMEOUT_MS) == ESP_OK;
}

static bool write_register(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = { reg, value };
    return i2c_master_transmit(device, data, sizeof(data), TIMEOUT_MS) == ESP_OK;
}

static bool add_device(i2c_master_bus_handle_t bus, uint16_t address)
{
    if (i2c_master_probe(bus, address, TIMEOUT_MS) != ESP_OK) {
        return false;
    }

    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,   // 100 кГц
    };
    return i2c_master_bus_add_device(bus, &config, &device) == ESP_OK;
}

bool bme280_init(i2c_master_bus_handle_t bus)
{
    if (!add_device(bus, ADDRESS)) {
        printf("Датчик не отвечает. Проверь провода: VIN->3V3, GND->GND, SDA->D21, SCL->D22\n");
        return false;
    }

    uint8_t chip_id;
    if (!read_registers(REG_CHIP_ID, &chip_id, 1)) {
        printf("Не удалось прочитать ID датчика\n");
        return false;
    }

    if (chip_id == CHIP_ID_BME280) {
        printf("Датчик: BME280\n");
    } else if (chip_id == CHIP_ID_BMP280) {
        printf("Датчик: BMP280\n");
    } else {
        printf("Неизвестный датчик, ID = 0x%02X\n", chip_id);
        return false;
    }

    uint8_t calib[6];
    if (!read_registers(REG_CALIB_T, calib, sizeof(calib))) {
        printf("Не удалось прочитать калибровку\n");
        return false;
    }
    dig_T1 = (uint16_t)(calib[1] << 8 | calib[0]);
    dig_T2 = (int16_t)(calib[3] << 8 | calib[2]);
    dig_T3 = (int16_t)(calib[5] << 8 | calib[4]);

    return true;
}

bool bme280_read_temperature(float *temperature)
{
    if (device == NULL) {
        return false;
    }

    // 0x41 = 010 000 01:
    // температура ×2 (усреднить 2 замера), давление не мерить, режим forced
    if (!write_register(REG_CTRL_MEAS, 0x41)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));   // измерение занимает несколько миллисекунд

    uint8_t raw[3];
    if (!read_registers(REG_TEMP, raw, sizeof(raw))) {
        return false;
    }
    int32_t adc_T = (raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4);

    // Формула пересчёта из документации Bosch (datasheet BME280, раздел 4.2.3)
    // https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
    
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    int32_t t_fine = var1 + var2;
    int32_t hundredths = (t_fine * 5 + 128) >> 8;   // температура в сотых долях градуса

    *temperature = hundredths / 100.0f;
    return true;
}
