#pragma once

#include <stdbool.h>
#include "driver/i2c_master.h"

bool bme280_init(i2c_master_bus_handle_t bus);

// Измерить температуру в °C
bool bme280_read_temperature(float *temperature);
