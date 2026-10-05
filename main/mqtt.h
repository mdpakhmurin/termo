#pragma once

#include <stdbool.h>

// Подключиться к MQTT-брокеру. Адрес вида "mqtt://сервер:порт".
void mqtt_start(const char *uri, const char *username, const char *password);

// Отправить строку в топик. Возвращает false, если сейчас нет связи с брокером.
bool mqtt_publish(const char *topic, const char *value);
