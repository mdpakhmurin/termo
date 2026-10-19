#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bme280.h"

#define ADDRESS 0x76   // адрес модуля

// Регистры датчика
#define REG_CHIP_ID   0xD0   // тип: BME280 или BMP280
#define REG_CALIB_T   0x88   // заводская калибровка температуры (6 байт)

#define REG_CALIB_P   0x8E   // заводская калибровка давления (18 байт)

#define REG_CALIB_H1  0xA1   // калибровка влажности, часть 1 (1 байт)
#define REG_CALIB_H2  0xE1   // калибровка влажности, часть 2 (7 байт)

#define REG_CTRL_HUM  0xF2   // настройка замера влажности
#define REG_CTRL_MEAS 0xF4   // команда «измерь» + настройка замера температуры

#define REG_DATA      0xF7   // сырые данные: давление (3 байта), температура (3), влажность (2)

#define CHIP_ID_BME280 0x60
#define CHIP_ID_BMP280 0x58

#define TIMEOUT_MS 100

static i2c_master_dev_handle_t device = NULL;
static bool has_humidity = false;

// калибровочные числа с датчика
static uint16_t dig_T1;
static int16_t  dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t  dig_H1, dig_H3;
static int16_t  dig_H2, dig_H4, dig_H5;
static int8_t   dig_H6;

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

// Склеить два байта в 16-битное число: младший байт первым
static uint16_t u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[1] << 8 | bytes[0]);
}

static bool read_calibration(void)
{
    uint8_t t[6];
    if (!read_registers(REG_CALIB_T, t, sizeof(t))) {
        return false;
    }
    dig_T1 = u16(&t[0]);
    dig_T2 = (int16_t)u16(&t[2]);
    dig_T3 = (int16_t)u16(&t[4]);

    uint8_t p[18];
    if (!read_registers(REG_CALIB_P, p, sizeof(p))) {
        return false;
    }
    dig_P1 = u16(&p[0]);
    dig_P2 = (int16_t)u16(&p[2]);
    dig_P3 = (int16_t)u16(&p[4]);
    dig_P4 = (int16_t)u16(&p[6]);
    dig_P5 = (int16_t)u16(&p[8]);
    dig_P6 = (int16_t)u16(&p[10]);
    dig_P7 = (int16_t)u16(&p[12]);
    dig_P8 = (int16_t)u16(&p[14]);
    dig_P9 = (int16_t)u16(&p[16]);

    if (!has_humidity) {
        return true;
    }

    uint8_t h[7];
    if (!read_registers(REG_CALIB_H1, &dig_H1, 1) || !read_registers(REG_CALIB_H2, h, sizeof(h))) {
        return false;
    }
    dig_H2 = (int16_t)u16(&h[0]);
    dig_H3 = h[2];
    dig_H4 = (int16_t)(((int8_t)h[3] * 16) | (h[4] & 0x0F));
    dig_H5 = (int16_t)(((int8_t)h[5] * 16) | (h[4] >> 4));
    dig_H6 = (int8_t)h[6];

    return true;
}

bool bme280_init(i2c_master_bus_handle_t bus)
{
    if (!add_device(bus, ADDRESS)) {
        printf("Датчик не отвечает. Проверить: VIN->3V3, GND->GND, SDA->D4, SCL->D5\n");
        return false;
    }

    uint8_t chip_id;
    if (!read_registers(REG_CHIP_ID, &chip_id, 1)) {
        printf("Не удалось прочитать ID датчика\n");
        return false;
    }

    if (chip_id == CHIP_ID_BME280) {
        printf("Датчик: BME280\n");
        has_humidity = true;
    } else if (chip_id == CHIP_ID_BMP280) {
        printf("Датчик: BMP280 (без влажности)\n");
        has_humidity = false;
    } else {
        printf("Неизвестный датчик, ID = 0x%02X\n", chip_id);
        return false;
    }

    if (!read_calibration()) {
        printf("Не удалось прочитать калибровку\n");
        return false;
    }

    return true;
}

/* Формулы пересчёта из документации Bosch (datasheet BME280, раздел 4.2.3 и 8.2)
   https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf */

// Температура → сотые доли градуса
static int32_t compensate_temperature(int32_t adc_T, int32_t *t_fine)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    *t_fine = var1 + var2;
    return (*t_fine * 5 + 128) >> 8;
}

// Давление → паскали × 256
static uint32_t compensate_pressure(int32_t adc_P, int32_t t_fine)
{
    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
    if (var1 == 0) {
        return 0;
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
    return (uint32_t)p;
}

// Влажность → проценты × 1024
static uint32_t compensate_humidity(int32_t adc_H, int32_t t_fine)
{
    int32_t v = t_fine - ((int32_t)76800);
    v = (((((adc_H << 14) - (((int32_t)dig_H4) << 20) - (((int32_t)dig_H5) * v)) + ((int32_t)16384)) >> 15) *
         (((((((v * ((int32_t)dig_H6)) >> 10) * (((v * ((int32_t)dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
            ((int32_t)2097152)) * ((int32_t)dig_H2) + 8192) >> 14));
    v = (v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)dig_H1)) >> 4));
    v = (v < 0 ? 0 : v);
    v = (v > 419430400 ? 419430400 : v);
    return (uint32_t)(v >> 12);
}

bool bme280_read(bme280_data_t *data)
{
    if (device == NULL) {
        return false;
    }

    // Влажность ×1
    if (has_humidity && !write_register(REG_CTRL_HUM, 0x01)) {
        return false;
    }

    // температура ×2 (усреднить 2 замера), давление ×1, режим forced (один замер)
    if (!write_register(REG_CTRL_MEAS, 0x45)) {
        return false;
    }

    // Ожидание замера
    vTaskDelay(pdMS_TO_TICKS(30));

    // Все сырые данные одним чтением: давление (3 байта), температура (3), влажность (2)
    uint8_t raw[8];
    if (!read_registers(REG_DATA, raw, sizeof(raw))) {
        return false;
    }
    int32_t adc_P = (raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = (raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_H = (raw[6] << 8) | raw[7];

    int32_t t_fine;
    data->temperature = compensate_temperature(adc_T, &t_fine) / 100.0f;
    data->pressure = compensate_pressure(adc_P, t_fine) / 256.0f / 100.0f;
    data->humidity = has_humidity ? compensate_humidity(adc_H, t_fine) / 1024.0f : NAN;

    return true;
}
