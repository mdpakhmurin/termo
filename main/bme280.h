#pragma once

#include <stdbool.h>
#include "driver/i2c_master.h"

typedef struct {
    float temperature;   // Температура, °C
    float humidity;      // Влажность, %
    float pressure;      // Давление, гПа
} bme280_data_t;

// Инициалиация датчика
bool bme280_init(i2c_master_bus_handle_t bus);

// Выполнить измерение
bool bme280_read(bme280_data_t *data);
