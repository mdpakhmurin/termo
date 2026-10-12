#pragma once

#include <stdbool.h>

// Подключиться к Wi-Fi-сети (2.4 ГГц).
void wifi_connect(const char *ssid, const char *password);

// Есть ли сейчас подключение к Wi-Fi
bool wifi_is_connected(void);

// Сохранить имя сети и пароль (NVS)
bool wifi_save_credentials(const char *ssid, const char *password);
