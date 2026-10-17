#pragma once

#include <stdbool.h>
#include <stddef.h>

#define MQTT_URI_SIZE      128
#define MQTT_USERNAME_SIZE 64
#define MQTT_PASSWORD_SIZE 64    

typedef struct {
    char uri[MQTT_URI_SIZE];             // uri: "mqtt://m4.wqtt.ru:99999"
    char username[MQTT_USERNAME_SIZE];   // имя пользователя
    char password[MQTT_PASSWORD_SIZE];   // пароль
} mqtt_settings_t;

// Сохранить настройки брокера во флеш-память (NVS, раздел "mqtt")
bool mqtt_save_settings(const mqtt_settings_t *settings);

// Прочитать настройки брокера. false — если ничего не сохраняли
bool mqtt_load_settings(mqtt_settings_t *settings);

// Подключиться к MQTT-брокеру
void mqtt_start(const mqtt_settings_t *settings);

// Есть ли связь с брокером
bool mqtt_is_connected(void);

// Отправить строку в топик. Возвращает false, если сейчас нет связи с брокером.
bool mqtt_publish(const char *topic, const char *value);
