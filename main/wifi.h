#pragma once

#include <stdbool.h>
#include <stddef.h>

// Инициалищация Wi-Fi
void wifi_init(void);

// Подключение к роутеру
void wifi_connect(const char *ssid, const char *password);

// Точка доступа - раздача сети 
void wifi_start_access_point(void);

// Есть ли сейчас подключение к Wi-Fi
bool wifi_is_connected(void);

// Сохранить имя сети и пароль (NVS)
bool wifi_save_credentials(const char *ssid, const char *password);

// Прочитать сохранённую сеть. false — если ничего не найдено
bool wifi_load_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size);

// Стереть сохранённую сеть
void wifi_clear_credentials(void);
